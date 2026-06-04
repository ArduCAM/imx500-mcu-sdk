import time

import imx500_mcu_sdk as imx500


METADATA_BUFFER_SIZE = 64 * 1024
FRAME_COUNT = 10
SCORE_THRESHOLD = 0.3
TENSOR_CAPTURE_BYTES = 4096
DISPLAY_PREVIEW_BYTES = 16
PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME = True
SPI_FORMAT = imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR


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


def print_detections(parsed):
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


def main():
    print("IMX500 MicroPython metadata parse example")

    print("module fw version:", hex(imx500.get_fw_ver()))
    print("module pid:", hex(imx500.get_pid()))

    ok, device_id, boot_status = imx500.probe_imx500_module()
    print("probe:", ok, "device_id:", hex(device_id), "boot_status:", hex(boot_status))

    if not imx500.open(None, None, imx500.MipiDataFormat.IMAGE, SPI_FORMAT, 10):
        raise RuntimeError("imx500 open() failed")

    imx500.stream_on()

    buf = bytearray(METADATA_BUFFER_SIZE)
    parsed_frames = 0
    attempts = 0
    max_attempts = FRAME_COUNT * 20

    while parsed_frames < FRAME_COUNT and attempts < max_attempts:
        attempts += 1
        n = imx500.read_metadata(buf)
        if n <= 0:
            print("attempt", attempts, "no metadata frame")
            time.sleep_ms(10)
            continue

        parsed = imx500.parse_metadata(
            buf,
            length=n,
            spi_format=SPI_FORMAT,
            preview_len=TENSOR_CAPTURE_BYTES,
        )
        if parsed is None:
            print("attempt", attempts, "parse_metadata failed bytes:", n)
            time.sleep_ms(10)
            continue

        parsed_frames += 1
        print("\n=== parsed frame %d/%d bytes=%d ===" % (parsed_frames, FRAME_COUNT, n))
        if PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME and parsed_frames == 1:
            print_parsed_metadata(parsed)
        print_detections(parsed)
        time.sleep_ms(100)

    print("done parsed=%d attempts=%d" % (parsed_frames, attempts))


main()
