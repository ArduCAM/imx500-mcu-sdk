import time
import _thread
import imx500_mcu_sdk as imx500

try:
    import ujson as json
except ImportError:
    import json

try:
    import ustruct as struct
except ImportError:
    import struct

from machine import Pin, UART


METADATA_BUFFER_SIZE = 64 * 1024
SCORE_THRESHOLD = 0.3
TENSOR_CAPTURE_BYTES = 4096
DISPLAY_PREVIEW_BYTES = 16
PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME = True
DATA_READ_TIMEOUT_MS = 1000
DATA_CAPTURE_TIMEOUT_MS = 10000
DATA_CAPTURE_MAX_TIMEOUT_MS = 20000
DATA_PENDING_POLL_AFTER_MS = 200
SPI_FORMAT = imx500.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR

UART_ID = 1
UART_TX_PIN = 4
UART_RX_PIN = 5
UART_BAUDRATE = 115200
UART_WRITE_CHUNK = 512
UART_READ_TIMEOUT_MS = 1000
UART_MAX_COMMAND_JSON_BYTES = 1024
UART_MAX_COMMAND_BINARY_BYTES = 0
UART_IDLE_SLEEP_MS = 10
CAMERA_CORE_IDLE_SLEEP_MS = 10
CAMERA_CORE_STARTUP_SLEEP_MS = 100
CAMERA_RESPONSE_WAIT_SLICE_MS = 5

UART_MAGIC = b"IMX5"
UART_VERSION = 2
UART_HEADER_FORMAT = "<4sBBBBIIII"
UART_HEADER_SIZE = struct.calcsize(UART_HEADER_FORMAT)
UART_MSG_STATUS_REQUEST = 0x10
UART_MSG_DATA_REQUEST = 0x11
UART_MSG_STATUS_RESPONSE = 0x90
UART_MSG_DATA_RESPONSE = 0x91
UART_MSG_ERROR_RESPONSE = 0x92
UART_CODE_OK = 0
UART_CODE_NG = 1
UART_CODE_ERROR = 2

MAX_UART_DETECTIONS = 8
NG_ON_ANY_DETECTION = True

CORE1_STATUS_STOPPED = 0
CORE1_STATUS_RUNNING = 1
CORE1_STATUS_READY = 2
CORE1_STATUS_FAILED = -1

CAMERA_OPERATION_NONE = 0
CAMERA_OPERATION_READ_AI_FRAME = 1


def _tensor_dims(tensor):
    return [dim["size"] for dim in tensor.get("dimensions", ())]


def _dims_text(tensor):
    dims = _tensor_dims(tensor)
    if not dims:
        return "[]"
    return "[" + " x ".join(str(dim) for dim in dims) + "]"


def _preview_hex(preview, limit=DISPLAY_PREVIEW_BYTES):
    if not preview:
        return ""
    if len(preview) > limit:
        preview = preview[:limit]
    return " ".join("%02x" % b for b in preview)


def _score_percent(score):
    value = score
    if value <= 1.0:
        value *= 100.0
    return value


def _device_uid_hex():
    return imx500.get_sensor_device_id()


def _refresh_device_uid(state):
    if state.get("device_uid"):
        return True

    try:
        state["device_uid"] = _device_uid_hex()
        state["device_uid_error"] = None
        return True
    except Exception as exc:
        state["device_uid_error"] = _exception_text("get sensor device id failed", exc)
    return False


def _exception_name(exc):
    try:
        return type(exc).__name__
    except Exception:
        return "Exception"


def _exception_text(prefix, exc):
    text = str(exc)
    name = _exception_name(exc)
    if text:
        return "%s: %s: %s" % (prefix, name, text)
    return "%s: %s" % (prefix, name)


def _hex_or_none(value):
    if value is None:
        return "None"
    return hex(value)


def _checksum32(parts):
    checksum = 0
    for data in parts:
        if data is None:
            continue
        for b in data:
            checksum = (checksum + b) & 0xFFFFFFFF
    return checksum


def _uart_write_all(uart, data):
    total = 0
    length = len(data)
    while total < length:
        end = total + UART_WRITE_CHUNK
        if end > length:
            end = length
        chunk = data[total:end]
        written = uart.write(chunk)
        if written is None:
            written = 0
        if written <= 0:
            time.sleep_ms(1)
            continue
        total += written


def _uart_read_exact(uart, length, timeout_ms):
    data = bytearray()
    last_progress = time.ticks_ms()
    while len(data) < length:
        available = uart.any()
        if available:
            need = length - len(data)
            if available < need:
                need = available
            chunk = uart.read(need)
            if chunk:
                data.extend(chunk)
                last_progress = time.ticks_ms()
                continue
        if time.ticks_diff(time.ticks_ms(), last_progress) >= timeout_ms:
            return None
        time.sleep_ms(1)
    return bytes(data)


def _read_uart_packet(uart):
    if uart.any() <= 0:
        return None

    window = b""
    start = time.ticks_ms()
    while True:
        chunk = uart.read(1)
        if chunk:
            window = (window + chunk)[-len(UART_MAGIC):]
            if window == UART_MAGIC:
                break
        elif time.ticks_diff(time.ticks_ms(), start) >= UART_READ_TIMEOUT_MS:
            return None
        else:
            time.sleep_ms(1)

    rest = _uart_read_exact(uart, UART_HEADER_SIZE - len(UART_MAGIC), UART_READ_TIMEOUT_MS)
    if rest is None:
        return {"error": "incomplete header", "sequence": 0}

    header = UART_MAGIC + rest
    magic, version, msg_type, code, flags, sequence, json_len, binary_len, checksum = struct.unpack(
        UART_HEADER_FORMAT,
        header,
    )
    if magic != UART_MAGIC:
        return None
    if version != UART_VERSION:
        return {
            "error": "unsupported protocol version %d" % version,
            "sequence": sequence,
        }
    if json_len > UART_MAX_COMMAND_JSON_BYTES:
        return {"error": "command JSON too large", "sequence": sequence}
    if binary_len > UART_MAX_COMMAND_BINARY_BYTES:
        return {"error": "command binary payload is not supported", "sequence": sequence}

    json_bytes = _uart_read_exact(uart, json_len, UART_READ_TIMEOUT_MS) if json_len else b""
    if json_bytes is None:
        return {"error": "incomplete JSON payload", "sequence": sequence}
    binary_payload = _uart_read_exact(uart, binary_len, UART_READ_TIMEOUT_MS) if binary_len else b""
    if binary_payload is None:
        return {"error": "incomplete binary payload", "sequence": sequence}

    expected = _checksum32((json_bytes, binary_payload))
    if expected != checksum:
        return {"error": "checksum mismatch", "sequence": sequence}

    request = {}
    if json_bytes:
        try:
            request = json.loads(json_bytes.decode())
        except Exception as exc:
            return {"error": _exception_text("invalid command JSON", exc), "sequence": sequence}
        if request is None:
            request = {}

    return {
        "msg_type": msg_type,
        "code": code,
        "flags": flags,
        "sequence": sequence,
        "json": request,
    }


def _send_uart_packet(
    uart,
    msg_type,
    code,
    sequence,
    json_payload=None,
    binary_payload=None,
    binary_len=0,
    flags=0,
):
    json_bytes = b""
    if json_payload is not None:
        json_bytes = json.dumps(json_payload).encode()
    has_binary = binary_payload is not None and binary_len > 0
    checksum = _checksum32((json_bytes, binary_payload if has_binary else None))
    header = struct.pack(
        UART_HEADER_FORMAT,
        UART_MAGIC,
        UART_VERSION,
        msg_type,
        code,
        flags,
        sequence,
        len(json_bytes),
        binary_len if has_binary else 0,
        checksum,
    )
    _uart_write_all(uart, header)
    if json_bytes:
        _uart_write_all(uart, json_bytes)
    if has_binary:
        _uart_write_all(uart, binary_payload)
    return UART_HEADER_SIZE + len(json_bytes) + (binary_len if has_binary else 0)


def _scaled_box(box, scale):
    return [
        int(_clip01(box[0]) * scale + 0.5),
        int(_clip01(box[1]) * scale + 0.5),
        int(_clip01(box[2]) * scale + 0.5),
        int(_clip01(box[3]) * scale + 0.5),
    ]


def _detection_for_uart(detection):
    item = {
        "class_id": detection["class_id"],
        "score_permille": int(_clip01(detection["score"]) * 1000 + 0.5),
        "box_norm_10000": _scaled_box(detection["box"], 10000),
    }
    if "box_input_px" in detection:
        item["box_input_px"] = list(detection["box_input_px"])
    return item


def _jpeg_payload(buf, parsed, metadata_len):
    offset = parsed.get("jpeg_data_offset", 0)
    length = parsed.get("jpeg_data_len", 0)
    if offset < 0 or length <= 0 or offset >= metadata_len:
        return None, 0
    if offset + length > metadata_len:
        length = metadata_len - offset
    return memoryview(buf)[offset:offset + length], length


def _build_ai_result(
    parsed,
    detections,
    err,
    sequence,
    device_uid,
    device_id,
    status_text,
    jpeg_len,
    sample_index,
):
    header = parsed.get("primary_header") or {}
    result = {
        "device_uid": device_uid,
        "device_id": device_id,
        "sequence": sequence,
        "sample_index": sample_index,
        "frame_count": header.get("frame_count"),
        "status": status_text,
        "score_threshold_permille": int(SCORE_THRESHOLD * 1000 + 0.5),
        "detection_count": 0 if detections is None else len(detections),
        "detections": [],
        "jpeg_bytes": jpeg_len,
    }
    if err:
        result["error"] = err
        return result

    for detection in detections[:MAX_UART_DETECTIONS]:
        result["detections"].append(_detection_for_uart(detection))
    if detections and len(detections) > MAX_UART_DETECTIONS:
        result["truncated"] = True
    return result


def _result_code(status_text):
    if status_text == "NG":
        return UART_CODE_NG
    if status_text == "ERROR":
        return UART_CODE_ERROR
    return UART_CODE_OK


def _core1_status_text():
    if core1_status == CORE1_STATUS_READY:
        return "READY"
    if core1_status == CORE1_STATUS_RUNNING:
        return "RUNNING"
    if core1_status == CORE1_STATUS_FAILED:
        return "FAILED"
    return "STOPPED"


def print_tensor(kind, index, tensor):
    print(
        "    %s[%d] id=%s name=%s dims=%s elements=%d bytes=%d off=%d"
        % (
            kind,
            index,
            tensor.get("id"),
            tensor.get("name"),
            _dims_text(tensor),
            tensor.get("element_count", 0),
            tensor.get("data_bytes", 0),
            tensor.get("data_offset", 0),
        )
    )
    preview = tensor.get("preview")
    if preview:
        print("      preview:", _preview_hex(preview))


def print_parsed_metadata(parsed):
    header = parsed.get("primary_header") or {}
    print(
        "  header frame=%s valid=%s ap=%s network=%s"
        % (
            header.get("frame_count"),
            header.get("valid_flag"),
            header.get("size_of_ap_parameter"),
            header.get("network_ordinal"),
        )
    )
    print(
        "  raw=%d ap=[%d,%d) output=[%d,+%d)"
        % (
            parsed.get("raw_bytes", 0),
            parsed.get("ap_param_offset", 0),
            parsed.get("ap_param_end_offset", 0),
            parsed.get("output_payload_offset", 0),
            parsed.get("output_payload_length", 0),
        )
    )
    print(
        "  networks=%d selected=%d"
        % (
            parsed.get("network_count", 0),
            parsed.get("selected_network_index", 0),
        )
    )

    for net_index, network in enumerate(parsed.get("networks", ())):
        print(
            "  network[%d] id=%s name=%s type=%s"
            % (
                net_index,
                network.get("id"),
                network.get("name"),
                network.get("type"),
            )
        )
        for index, tensor in enumerate(network.get("input_tensors", ())):
            print_tensor("input", index, tensor)
        for index, tensor in enumerate(network.get("output_tensors", ())):
            print_tensor("output", index, tensor)


def _signed_value(value, bits):
    sign_bit = 1 << (bits - 1)
    if value & sign_bit:
        value -= 1 << bits
    return value


def _read_le(data, offset, byte_count):
    value = 0
    for i in range(byte_count):
        value |= data[offset + i] << (8 * i)
    return value


def _decode_tensor_values(tensor):
    data = tensor.get("preview")
    data_bytes = tensor.get("data_bytes", 0)
    if data is None:
        return None, "tensor %s has no payload preview" % tensor.get("name")
    if len(data) < data_bytes:
        return None, "tensor %s payload truncated: preview=%d bytes need=%d bytes" % (
            tensor.get("name"),
            len(data),
            data_bytes,
        )

    bits = tensor.get("bits_per_element", 0)
    if bits not in (8, 16, 32):
        return None, "tensor %s uses unsupported %d-bit elements" % (tensor.get("name"), bits)

    byte_count = (bits + 7) // 8
    count = tensor.get("element_count", 0)
    signed = tensor.get("format", 0) == 0
    zero_point = tensor.get("zero_point", 0)
    scale = tensor.get("scale", 1.0)
    values = []

    for i in range(count):
        offset = i * byte_count
        if offset + byte_count > len(data):
            break
        raw = _read_le(data, offset, byte_count)
        if signed:
            raw = _signed_value(raw, bits)
        values.append((raw - zero_point) * scale)

    return values, None


def _input_size(network):
    inputs = network.get("input_tensors", ())
    if not inputs:
        return None, None
    dims = _tensor_dims(inputs[0])
    if len(dims) >= 4 and dims[0] == 1:
        return dims[1], dims[2]
    if len(dims) < 2:
        return None, None
    return dims[0], dims[1]


def _clip01(value):
    if value < 0.0:
        return 0.0
    if value > 1.0:
        return 1.0
    return value


def _insert_detection_sorted(detections, detection):
    index = 0
    while index < len(detections) and detections[index]["score"] >= detection["score"]:
        index += 1
    detections.insert(index, detection)


def parse_ssd_mobilenet_detections(parsed, score_threshold=SCORE_THRESHOLD):
    networks = parsed.get("networks", ())
    if not networks:
        return None, "no network metadata"

    selected = parsed.get("selected_network_index", 0)
    if selected < 0 or selected >= len(networks):
        selected = 0
    network = networks[selected]
    outputs = network.get("output_tensors", ())
    if len(outputs) < 4:
        return None, "ssd mobilenet needs 4 output tensors, got %d" % len(outputs)

    boxes, err = _decode_tensor_values(outputs[0])
    if err:
        return None, err
    scores, err = _decode_tensor_values(outputs[1])
    if err:
        return None, err
    classes, err = _decode_tensor_values(outputs[2])
    if err:
        return None, err
    valid_counts, err = _decode_tensor_values(outputs[3])
    if err:
        return None, err
    if not valid_counts:
        return None, "valid-count tensor is empty"

    valid_count = int(valid_counts[0] + 0.5)
    max_count = min(len(scores), len(classes), len(boxes) // 4)
    if valid_count > max_count:
        valid_count = max_count

    input_h, input_w = _input_size(network)
    detections = []
    for i in range(valid_count):
        score = scores[i]
        if score <= score_threshold:
            continue

        y1 = boxes[i * 4]
        x1 = boxes[i * 4 + 1]
        y2 = boxes[i * 4 + 2]
        x2 = boxes[i * 4 + 3]
        if x2 <= x1 or y2 <= y1:
            continue

        detection = {
            "class_id": int(classes[i] + 0.5),
            "score": score,
            "box": (y1, x1, y2, x2),
        }
        if input_h is not None and input_w is not None:
            detection["box_input_px"] = (
                int(_clip01(y1) * input_h + 0.5),
                int(_clip01(x1) * input_w + 0.5),
                int(_clip01(y2) * input_h + 0.5),
                int(_clip01(x2) * input_w + 0.5),
            )
        _insert_detection_sorted(detections, detection)

    return detections, None


def print_detections(parsed, detections=None, err=None):
    if detections is None and err is None:
        detections, err = parse_ssd_mobilenet_detections(parsed, SCORE_THRESHOLD)
    if err:
        print("  detections: skipped:", err)
        return

    print("  detections > %.1f%%: %d" % (_score_percent(SCORE_THRESHOLD), len(detections)))
    for index, det in enumerate(detections):
        y1, x1, y2, x2 = det["box"]
        text = (
            "    #%d class=%d score=%.1f%% box_norm=(y1=%.3f x1=%.3f y2=%.3f x2=%.3f)"
            % (
                index,
                det["class_id"],
                _score_percent(det["score"]),
                y1,
                x1,
                y2,
                x2,
            )
        )
        if "box_input_px" in det:
            py1, px1, py2, px2 = det["box_input_px"]
            text += " box_input_px=(y1=%d x1=%d y2=%d x2=%d)" % (py1, px1, py2, px2)
        print(text)


def _make_camera_state():
    state = {
        "device_uid": "",
        "fw_version": None,
        "pid": None,
        "probe_ok": False,
        "device_id": 0,
        "boot_status": 0,
        "opened": False,
        "streaming": False,
        "ready": False,
        "startup_state": "BOOTING",
        "startup_message": "camera core starting",
        "startup_attempts": 0,
        "device_uid_error": None,
        "last_error": None,
        "last_result": None,
        "parsed_frames": 0,
        "buf": bytearray(METADATA_BUFFER_SIZE),
    }
    return state


def _probe_camera(state):
    state["startup_state"] = "PROBING"
    state["startup_message"] = "probing imx500 module"
    try:
        state["fw_version"] = imx500.get_fw_ver()
        state["pid"] = imx500.get_pid()
        ok, device_id, boot_status = imx500.probe_imx500_module()
        state["probe_ok"] = bool(ok)
        state["device_id"] = device_id
        state["boot_status"] = boot_status
        if not ok:
            state["ready"] = False
            state["last_error"] = "probe_imx500_module failed"
        else:
            state["startup_state"] = "PROBED"
            state["startup_message"] = "imx500 module probed"
        return bool(ok)
    except Exception as exc:
        state["probe_ok"] = False
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "probe failed"
        state["last_error"] = _exception_text("probe failed", exc)
        return False


def _ensure_camera_ready(state):
    if not _probe_camera(state):
        return False

    if not state["opened"]:
        if not _open_camera(state):
            return False

    if not state["streaming"]:
        if not _start_camera_stream(state):
            return False

    return _mark_camera_ready(state)


def _open_camera(state):
    state["startup_state"] = "OPENING"
    state["startup_message"] = "opening imx500 stream path"
    try:
        state["opened"] = bool(
            imx500.imx500_open(None, None, imx500.MipiDataFormat.IMAGE, SPI_FORMAT, 10)
        )
    except Exception as exc:
        state["opened"] = False
        state["last_error"] = _exception_text("imx500_open failed", exc)
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "imx500_open failed"
        return False
    if not state["opened"]:
        state["last_error"] = "imx500_open() returned false"
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "imx500_open returned false"
        return False
    state["startup_state"] = "OPENED"
    state["startup_message"] = "imx500 stream path opened"
    return True


def _start_camera_stream(state):
    state["startup_state"] = "STREAMING"
    state["startup_message"] = "starting imx500 stream"
    try:
        imx500.stream_on()
        state["streaming"] = True
    except Exception as exc:
        state["streaming"] = False
        state["last_error"] = _exception_text("imx500 stream_on failed", exc)
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "imx500 stream_on failed"
        return False
    state["startup_state"] = "STREAM_ON"
    state["startup_message"] = "imx500 stream started"
    return True


def _mark_camera_ready(state):
    state["ready"] = True
    state["startup_state"] = "READY"
    state["startup_message"] = "camera ready"
    state["last_error"] = None
    _refresh_device_uid(state)
    return True


def _advance_camera_startup(state):
    if state.get("ready"):
        _refresh_device_uid(state)
        return True
    state["startup_attempts"] = state.get("startup_attempts", 0) + 1
    if not state.get("probe_ok"):
        return _probe_camera(state)
    if not state.get("opened"):
        return _open_camera(state)
    if not state.get("streaming"):
        return _start_camera_stream(state)
    return _mark_camera_ready(state)


def _status_payload(state):
    startup_state = state.get("startup_state", "BOOTING")
    job_snapshot = _camera_job_snapshot()
    poll_after_ms = 200
    if startup_state == "PROBED":
        poll_after_ms = 5000
    elif startup_state in ("BOOTING", "OPENED", "STREAM_ON"):
        poll_after_ms = 500
    elif startup_state in ("READY", "ERROR"):
        poll_after_ms = 0

    return {
        "protocol_version": UART_VERSION,
        "device_uid": state.get("device_uid", ""),
        "device_uid_error": state.get("device_uid_error"),
        "fw_version": state.get("fw_version"),
        "pid": state.get("pid"),
        "probe_ok": state.get("probe_ok", False),
        "device_id": state.get("device_id", 0),
        "boot_status": state.get("boot_status", 0),
        "opened": state.get("opened", False),
        "streaming": state.get("streaming", False),
        "ready": state.get("ready", False),
        "startup_state": startup_state,
        "startup_message": state.get("startup_message", ""),
        "startup_attempts": state.get("startup_attempts", 0),
        "camera_core": _core1_status_text(),
        "camera_operation_busy": job_snapshot["state"] != "IDLE",
        "camera_job_id": job_snapshot["id"],
        "camera_job_state": job_snapshot["state"],
        "camera_job_elapsed_ms": job_snapshot["elapsed_ms"],
        "poll_after_ms": poll_after_ms,
        "last_error": state.get("last_error"),
        "last_result": state.get("last_result"),
        "parsed_frames": state.get("parsed_frames", 0),
        "uart_baudrate": UART_BAUDRATE,
        "data_timeout_ms": DATA_READ_TIMEOUT_MS,
        "data_capture_timeout_ms": DATA_CAPTURE_TIMEOUT_MS,
    }


def _request_response_wait_ms(request):
    value = request.get("wait_ms", request.get("timeout_ms", DATA_READ_TIMEOUT_MS))
    try:
        value = int(value)
    except Exception:
        value = DATA_READ_TIMEOUT_MS
    if value < 0:
        value = 0
    if value > 5000:
        value = 5000
    return value


def _request_capture_timeout_ms(request):
    value = request.get("capture_timeout_ms", DATA_CAPTURE_TIMEOUT_MS)
    try:
        value = int(value)
    except Exception:
        value = DATA_CAPTURE_TIMEOUT_MS
    if value < 1000:
        value = 1000
    if value > DATA_CAPTURE_MAX_TIMEOUT_MS:
        value = DATA_CAPTURE_MAX_TIMEOUT_MS
    return value


def _request_jpeg_mode(request):
    value = request.get("include_jpeg", "on_ng")
    if value is True:
        return "always"
    if value is False:
        return "never"
    if value in ("always", "never", "on_ng"):
        return value
    return "on_ng"


def _read_ai_frame(state, sequence, request):
    if not state.get("ready"):
        return {
            "device_uid": state.get("device_uid", ""),
            "device_id": state.get("device_id", 0),
            "sequence": sequence,
            "sample_index": state.get("parsed_frames", 0),
            "status": "ERROR",
            "startup_state": state.get("startup_state", "BOOTING"),
            "startup_message": state.get("startup_message", ""),
            "error": state.get("last_error") or "camera is still starting",
            "jpeg_bytes": 0,
        }, None, 0

    timeout_ms = _request_capture_timeout_ms(request)
    jpeg_mode = _request_jpeg_mode(request)
    start = time.ticks_ms()
    attempts = 0
    last_error = None
    buf = state["buf"]

    while time.ticks_diff(time.ticks_ms(), start) < timeout_ms:
        attempts += 1
        try:
            n = imx500.read_metadata(buf)
        except Exception as exc:
            last_error = _exception_text("read_metadata exception", exc)
            time.sleep_ms(10)
            continue
        if n <= 0:
            time.sleep_ms(10)
            continue

        try:
            parsed = imx500.parse_metadata(
                buf,
                length=n,
                spi_format=SPI_FORMAT,
                preview_len=TENSOR_CAPTURE_BYTES,
            )
        except Exception as exc:
            last_error = _exception_text("parse_metadata exception", exc)
            time.sleep_ms(10)
            continue
        if parsed is None:
            last_error = "parse_metadata returned None for %d bytes" % n
            time.sleep_ms(10)
            continue

        state["parsed_frames"] += 1
        sample_index = state["parsed_frames"]
        print("\n=== requested frame %d seq=%d bytes=%d ===" % (sample_index, sequence, n))
        if PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME and sample_index == 1:
            print_parsed_metadata(parsed)

        jpeg, jpeg_len = None, 0
        detections, err = None, None
        status_text = "ERROR"
        try:
            detections, err = parse_ssd_mobilenet_detections(parsed, SCORE_THRESHOLD)
        except Exception as exc:
            err = _exception_text("detection post-processing exception", exc)
        if err:
            status_text = "ERROR"
        else:
            has_detection = len(detections) > 0
            is_ng = has_detection if NG_ON_ANY_DETECTION else not has_detection
            status_text = "NG" if is_ng else "PASS"
            want_jpeg = jpeg_mode == "always" or (jpeg_mode == "on_ng" and is_ng)
            jpeg, jpeg_len = _jpeg_payload(buf, parsed, n) if want_jpeg else (None, 0)

        ai_result = _build_ai_result(
            parsed,
            detections,
            err,
            sequence,
            state.get("device_uid", ""),
            state.get("device_id", 0),
            status_text,
            jpeg_len,
            sample_index,
        )
        ai_result["attempts"] = attempts
        ai_result["capture_timeout_ms"] = timeout_ms
        ai_result["jpeg_mode"] = jpeg_mode
        state["last_result"] = status_text
        print(
            "  response: status=%s jpeg=%d attempts=%d"
            % (status_text, jpeg_len, attempts)
        )
        print_detections(parsed, detections, err)
        return ai_result, jpeg, jpeg_len

    state["last_result"] = "ERROR"
    state["last_error"] = last_error or "metadata read timeout"
    return {
        "device_uid": state.get("device_uid", ""),
        "device_id": state.get("device_id", 0),
        "sequence": sequence,
        "sample_index": state.get("parsed_frames", 0),
        "status": "ERROR",
        "error": state["last_error"],
        "attempts": attempts,
        "capture_timeout_ms": timeout_ms,
        "jpeg_bytes": 0,
    }, None, 0


def _send_error_response(uart, sequence, message):
    payload = {
        "status": "ERROR",
        "error": message,
        "sequence": sequence,
    }
    sent = _send_uart_packet(
        uart,
        UART_MSG_ERROR_RESPONSE,
        UART_CODE_ERROR,
        sequence,
        payload,
    )
    print("  protocol error seq=%d bytes=%d: %s" % (sequence, sent, message))


def _make_camera_error_result(state, sequence, request, message):
    request = request or {}
    return {
        "device_uid": state.get("device_uid", ""),
        "device_id": state.get("device_id", 0),
        "sequence": sequence,
        "sample_index": state.get("parsed_frames", 0),
        "status": "ERROR",
        "startup_state": state.get("startup_state", "BOOTING"),
        "startup_message": state.get("startup_message", ""),
        "error": message,
        "wait_ms": _request_response_wait_ms(request),
        "capture_timeout_ms": _request_capture_timeout_ms(request),
        "jpeg_bytes": 0,
    }, None, 0


def _clear_camera_job_locked():
    camera_job["id"] = 0
    camera_job["operation"] = CAMERA_OPERATION_NONE
    camera_job["sequence"] = 0
    camera_job["request"] = None
    camera_job["pending"] = False
    camera_job["busy"] = False
    camera_job["done"] = False
    camera_job["cancelled"] = False
    camera_job["ai_result"] = None
    camera_job["jpeg"] = None
    camera_job["jpeg_len"] = 0
    camera_job["created_ms"] = 0


def _camera_job_snapshot():
    camera_job_lock.acquire()
    try:
        if camera_job["done"]:
            state_text = "DONE"
        elif camera_job["busy"]:
            state_text = "BUSY"
        elif camera_job["pending"]:
            state_text = "PENDING"
        else:
            state_text = "IDLE"
        elapsed_ms = 0
        if camera_job["created_ms"]:
            elapsed_ms = time.ticks_diff(time.ticks_ms(), camera_job["created_ms"])
        return {
            "id": camera_job["id"],
            "sequence": camera_job["sequence"],
            "state": state_text,
            "pending": camera_job["pending"],
            "busy": camera_job["busy"],
            "done": camera_job["done"],
            "elapsed_ms": elapsed_ms,
        }
    finally:
        camera_job_lock.release()


def _make_pending_result(state, sequence, request, snapshot):
    return {
        "device_uid": state.get("device_uid", ""),
        "device_id": state.get("device_id", 0),
        "sequence": sequence,
        "job_id": snapshot.get("id", 0),
        "job_sequence": snapshot.get("sequence", 0),
        "sample_index": state.get("parsed_frames", 0),
        "status": "PENDING",
        "pending": True,
        "camera_job_state": snapshot.get("state", "BUSY"),
        "elapsed_ms": snapshot.get("elapsed_ms", 0),
        "poll_after_ms": DATA_PENDING_POLL_AFTER_MS,
        "wait_ms": _request_response_wait_ms(request or {}),
        "capture_timeout_ms": _request_capture_timeout_ms(request or {}),
        "jpeg_bytes": 0,
    }, None, 0


def _submit_camera_data_request(sequence, request):
    global camera_job_next_id
    camera_job_lock.acquire()
    try:
        if camera_job["pending"] or camera_job["busy"] or camera_job["done"]:
            return 0
        camera_job_next_id += 1
        camera_job["id"] = camera_job_next_id
        camera_job["operation"] = CAMERA_OPERATION_READ_AI_FRAME
        camera_job["sequence"] = sequence
        camera_job["request"] = request or {}
        camera_job["pending"] = True
        camera_job["busy"] = False
        camera_job["done"] = False
        camera_job["cancelled"] = False
        camera_job["ai_result"] = None
        camera_job["jpeg"] = None
        camera_job["jpeg_len"] = 0
        camera_job["created_ms"] = time.ticks_ms()
        return camera_job_next_id
    finally:
        camera_job_lock.release()


def _take_next_camera_job():
    camera_job_lock.acquire()
    try:
        if not camera_job["pending"]:
            return None
        job = (
            camera_job["id"],
            camera_job["operation"],
            camera_job["sequence"],
            camera_job["request"],
        )
        camera_job["pending"] = False
        camera_job["busy"] = True
        return job
    finally:
        camera_job_lock.release()


def _finish_camera_job(job_id, ai_result, jpeg, jpeg_len):
    camera_job_lock.acquire()
    try:
        if camera_job["id"] != job_id or not camera_job["busy"]:
            return
        if camera_job["cancelled"]:
            _clear_camera_job_locked()
            return
        camera_job["ai_result"] = ai_result
        camera_job["jpeg"] = jpeg
        camera_job["jpeg_len"] = jpeg_len
        camera_job["busy"] = False
        camera_job["done"] = True
    finally:
        camera_job_lock.release()


def _take_finished_camera_job(job_id, response_sequence=None):
    camera_job_lock.acquire()
    try:
        if camera_job["id"] != job_id or not camera_job["done"]:
            return None
        ai_result = camera_job["ai_result"]
        if ai_result is not None and response_sequence is not None:
            if response_sequence != camera_job["sequence"]:
                ai_result["job_sequence"] = camera_job["sequence"]
            ai_result["sequence"] = response_sequence
            ai_result["job_id"] = job_id
        result = (
            ai_result,
            camera_job["jpeg"],
            camera_job["jpeg_len"],
        )
        _clear_camera_job_locked()
        return result
    finally:
        camera_job_lock.release()


def _fail_camera_jobs(message):
    camera_job_lock.acquire()
    try:
        if not (camera_job["pending"] or camera_job["busy"]):
            return
        ai_result, jpeg, jpeg_len = _make_camera_error_result(
            state,
            camera_job["sequence"],
            camera_job["request"],
            message,
        )
        camera_job["pending"] = False
        camera_job["busy"] = False
        camera_job["done"] = True
        camera_job["cancelled"] = False
        camera_job["ai_result"] = ai_result
        camera_job["jpeg"] = jpeg
        camera_job["jpeg_len"] = jpeg_len
    finally:
        camera_job_lock.release()


def _request_ai_frame_on_core1(state, sequence, request):
    request = request or {}
    if core1_status == CORE1_STATUS_FAILED:
        return _make_camera_error_result(state, sequence, request, "camera core is stopped")

    snapshot = _camera_job_snapshot()
    if snapshot["done"]:
        result = _take_finished_camera_job(snapshot["id"], sequence)
        if result is not None:
            return result

    if snapshot["state"] == "IDLE":
        job_id = _submit_camera_data_request(sequence, request)
        if not job_id:
            snapshot = _camera_job_snapshot()
            if snapshot["done"]:
                result = _take_finished_camera_job(snapshot["id"], sequence)
                if result is not None:
                    return result
            return _make_pending_result(state, sequence, request, snapshot)
    else:
        job_id = snapshot["id"]

    wait_ms = _request_response_wait_ms(request)
    start = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), start) <= wait_ms:
        result = _take_finished_camera_job(job_id, sequence)
        if result is not None:
            return result
        if core1_status == CORE1_STATUS_FAILED:
            return _make_camera_error_result(
                state,
                sequence,
                request,
                state.get("last_error") or "camera core stopped",
            )
        time.sleep_ms(CAMERA_RESPONSE_WAIT_SLICE_MS)

    result = _take_finished_camera_job(job_id, sequence)
    if result is not None:
        return result

    return _make_pending_result(state, sequence, request, _camera_job_snapshot())


def _handle_uart_packet(uart, state, packet):
    sequence = packet.get("sequence", 0)
    msg_type = packet.get("msg_type")
    request = packet.get("json") or {}
    if msg_type == UART_MSG_STATUS_REQUEST:
        payload = _status_payload(state)
        code = UART_CODE_ERROR if payload["startup_state"] == "ERROR" else UART_CODE_OK
        sent = _send_uart_packet(uart, UART_MSG_STATUS_RESPONSE, code, sequence, payload)
        print(
            "  status response seq=%d state=%s ready=%s bytes=%d"
            % (sequence, payload["startup_state"], payload["ready"], sent)
        )
        return

    if msg_type == UART_MSG_DATA_REQUEST:
        ai_result, jpeg, jpeg_len = _request_ai_frame_on_core1(state, sequence, request)
        sent = _send_uart_packet(
            uart,
            UART_MSG_DATA_RESPONSE,
            _result_code(ai_result.get("status")),
            sequence,
            ai_result,
            jpeg,
            jpeg_len,
        )
        print(
            "  data response seq=%d status=%s bytes=%d"
            % (sequence, ai_result.get("status"), sent)
        )
        return

    _send_error_response(uart, sequence, "unsupported message type %s" % msg_type)


state = _make_camera_state()
core1_status = CORE1_STATUS_STOPPED
camera_job_next_id = 0
camera_job_lock = _thread.allocate_lock()
camera_job = {
    "id": 0,
    "operation": CAMERA_OPERATION_NONE,
    "sequence": 0,
    "request": None,
    "pending": False,
    "busy": False,
    "done": False,
    "cancelled": False,
    "ai_result": None,
    "jpeg": None,
    "jpeg_len": 0,
    "created_ms": 0,
}


def core1_task():
    global core1_status
    try:
        core1_status = CORE1_STATUS_RUNNING
        print("camera core1 task started")
        while True:
            if not state.get("ready"):
                _advance_camera_startup(state)
                if state.get("ready"):
                    core1_status = CORE1_STATUS_READY

            job = _take_next_camera_job()
            if job is not None:
                job_id, operation, sequence, request = job
                if operation == CAMERA_OPERATION_READ_AI_FRAME:
                    ai_result, jpeg, jpeg_len = _read_ai_frame(state, sequence, request)
                else:
                    ai_result, jpeg, jpeg_len = _make_camera_error_result(
                        state,
                        sequence,
                        request,
                        "unsupported camera operation %s" % operation,
                    )
                _finish_camera_job(job_id, ai_result, jpeg, jpeg_len)
                continue

            if state.get("ready"):
                time.sleep_ms(CAMERA_CORE_IDLE_SLEEP_MS)
            else:
                time.sleep_ms(CAMERA_CORE_STARTUP_SLEEP_MS)
    except Exception as exc:
        message = _exception_text("camera core exception", exc)
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "camera core stopped"
        state["last_error"] = message
        core1_status = CORE1_STATUS_FAILED
        _fail_camera_jobs(message)
        print("camera core1 stopped:", message)


def main():
    global core1_status
    print("IMX500 MicroPython metadata parse example")

    uart = UART(
        UART_ID,
        baudrate=UART_BAUDRATE,
        tx=Pin(UART_TX_PIN),
        rx=Pin(UART_RX_PIN),
    )
    try:
        _thread.start_new_thread(core1_task, ())
    except Exception as exc:
        state["ready"] = False
        state["startup_state"] = "ERROR"
        state["startup_message"] = "camera core start failed"
        state["last_error"] = _exception_text("start camera core failed", exc)
        core1_status = CORE1_STATUS_FAILED
    print(
        "uart protocol v%d: id=%d tx=GPIO%d rx=GPIO%d baud=%d"
        % (UART_VERSION, UART_ID, UART_TX_PIN, UART_RX_PIN, UART_BAUDRATE)
    )

    print(
        "camera startup_state=%s uid=%s fw=%s pid=%s device_id=%s boot_status=%s"
        % (
            state["startup_state"],
            state["device_uid"],
            _hex_or_none(state["fw_version"]),
            _hex_or_none(state["pid"]),
            _hex_or_none(state["device_id"]),
            _hex_or_none(state["boot_status"]),
        )
    )
    if state["last_error"]:
        print("camera error:", state["last_error"])
    if state["device_uid_error"]:
        print("device uid error:", state["device_uid_error"])
    print("waiting for STATUS or GET_DATA requests from host")

    while True:
        packet = _read_uart_packet(uart)
        if packet is None:
            time.sleep_ms(UART_IDLE_SLEEP_MS)
            continue
        if packet.get("error"):
            sequence = packet.get("sequence", 0)
            _send_error_response(uart, sequence, packet["error"])
            continue
        _handle_uart_packet(uart, state, packet)


main()
