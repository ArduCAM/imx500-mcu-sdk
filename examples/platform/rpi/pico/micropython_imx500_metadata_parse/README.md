# MicroPython IMX500 Metadata Parse Example

This example reads IMX500 SPI metadata frames from MicroPython and parses them
with the SDK `parse_metadata(...)` API exposed through the `imx500_mcu_sdk` user
C module.

It uses the shared MicroPython user module in:

- `../../../../../python_bindings/micropython/usermod/imx500_mcu_sdk`

The MicroPython script prints:

- primary metadata header
- ApParams and output payload offsets
- network name and type
- input and output tensor dimensions
- output tensor payload offsets and short byte previews on the first parsed frame
- SSD MobileNet valid detection boxes for every parsed frame
- UART request/response product frames containing camera status, AI results, and
  optional JPEG bytes

The SSD MobileNet post-processing follows
`examples/platform/rpi/pico2/camera_serial_stream_multitask/common/renderers.py`.
It expects four output tensors in this order:

1. boxes: normalized `(y1, x1, y2, x2)`
2. scores
3. class IDs
4. valid detection count

Detections with score greater than `SCORE_THRESHOLD` are printed with normalized
box coordinates and input-tensor pixel coordinates.

## UART Product Protocol

`main.py` runs as a hardware UART request/response service:

```text
UART1 TX = GPIO4
UART1 RX = GPIO5
Baudrate = 115200
```

These pins do not overlap the IMX500 `I2C0` and `SPI0` pins used by this
example. The USB serial `print(...)` logs remain available for debugging.

The Pico does not push AI frames by itself. The host should:

1. Send `STATUS_REQUEST`.
2. Check that the `STATUS_RESPONSE` JSON has `ready: true`.
3. Send `GET_DATA_REQUEST` whenever one parsed AI result is needed.
4. If the response JSON has `status: "PENDING"`, wait `poll_after_ms`, then
   send `GET_DATA_REQUEST` again. This polls the same core1 camera job instead
   of starting another camera read.

The default PASS/NG rule is:

- `NG`: at least one detection is above `SCORE_THRESHOLD`
- `PASS`: no detections are above `SCORE_THRESHOLD`
- `PENDING`: the core1 camera worker is still waiting for one valid metadata frame
- `ERROR`: metadata parsed, but SSD MobileNet detection post-processing failed

For `GET_DATA_REQUEST`, JPEG transfer is controlled by request JSON. The default
is `include_jpeg: "on_ng"`, so `NG` responses contain AI JSON followed by the
JPEG bytes extracted from the metadata frame, while `PASS` responses contain only
AI JSON.

Every request and response has a 24-byte little-endian binary header:

```text
offset  size  field
0       4     magic: "IMX5"
4       1     version: 2
5       1     message type
6       1     code: request=0, response 0=OK/PASS, 1=NG, 2=ERROR
7       1     flags: reserved, send 0
8       4     sequence, echoed by the response
12      4     JSON byte length
16      4     binary byte length
20      4     checksum32 byte sum of JSON + optional binary bytes
```

The payload immediately follows the header:

```text
JSON bytes
optional binary bytes
```

Message types:

```text
0x10  STATUS_REQUEST
0x11  GET_DATA_REQUEST
0x90  STATUS_RESPONSE
0x91  DATA_RESPONSE
0x92  ERROR_RESPONSE
```

`STATUS_REQUEST` normally has no JSON payload. The response JSON includes:

```json
{
  "protocol_version": 2,
  "device_uid": "...",
  "device_uid_error": null,
  "fw_version": 0,
  "pid": 0,
  "probe_ok": true,
  "device_id": 0,
  "boot_status": 0,
  "opened": true,
  "streaming": true,
  "ready": true,
  "startup_state": "READY",
  "startup_message": "camera ready",
  "startup_attempts": 3,
  "camera_core": "READY",
  "camera_operation_busy": false,
  "camera_job_id": 0,
  "camera_job_state": "IDLE",
  "camera_job_elapsed_ms": 0,
  "poll_after_ms": 0,
  "last_error": null,
  "last_result": null,
  "parsed_frames": 0,
  "uart_baudrate": 115200,
  "data_timeout_ms": 1000,
  "data_capture_timeout_ms": 10000
}
```

Immediately after power-on, `STATUS_RESPONSE` can return `ready: false` with a
startup state such as `"BOOTING"`, `"PROBED"`, `"OPENED"`, or `"STREAM_ON"`.
This is not a transport error; the host should wait `poll_after_ms`, then poll
`STATUS_REQUEST` again until `ready: true`. Sensor UID is optional during
startup, and `device_uid_error` does not prevent `ready: true`. `camera_core`
reports the core1 camera worker state; when `ready` is true it should normally be
`"READY"`.

`GET_DATA_REQUEST` accepts optional JSON:

```json
{
  "wait_ms": 200,
  "capture_timeout_ms": 10000,
  "include_jpeg": "on_ng"
}
```

`wait_ms` controls how long core0 waits for an already-running core1 camera job
before returning `PENDING`; it is clamped to `0..5000`. For compatibility,
`timeout_ms` is also accepted as the wait value. `capture_timeout_ms` controls
how long core1 may keep waiting for a valid metadata frame before returning an
`ERROR`; it defaults to 10000 and is clamped to `1000..20000`. `include_jpeg`
can be `"on_ng"`, `"always"`, or `"never"`. Boolean `true` is treated as
`"always"` and `false` as `"never"`.

`DATA_RESPONSE` JSON includes `device_uid` from
`imx500.get_sensor_device_id()`, the probed `device_uid`, echoed request
`sequence`, local `sample_index`, IMX500 `frame_count`, result status,
threshold, read attempts, JPEG mode, and up to eight detections. Detection
scores are sent as `score_permille`, and normalized boxes are sent as
`box_norm_10000` in `(y1, x1, y2, x2)` order. If the response header has a
non-zero binary length, those bytes are the JPEG payload. While a frame read is
still running, `DATA_RESPONSE` returns `status: "PENDING"` with `job_id`,
`camera_job_state`, `elapsed_ms`, and `poll_after_ms`; the host should poll
again after that delay.

Minimal host-side packet flow:

```python
import json
import struct
import time

import serial


MAGIC = b"IMX5"
VERSION = 2
HEADER = "<4sBBBBIIII"
HEADER_SIZE = struct.calcsize(HEADER)
STATUS_REQUEST = 0x10
GET_DATA_REQUEST = 0x11
STATUS_RESPONSE = 0x90
DATA_RESPONSE = 0x91
ERROR_RESPONSE = 0x92

CODE_OK = 0
CODE_NG = 1
CODE_ERROR = 2

PORT = "/dev/ttyCH9344USB0"
BAUDRATE = 115200
STARTUP_POLLS = 60
DATA_POLLS = 300


def checksum32(*parts):
    value = 0
    for data in parts:
        if data:
            value = (value + sum(data)) & 0xFFFFFFFF
    return value


def send_packet(port, msg_type, sequence, payload=None):
    body = json.dumps(payload or {}).encode() if payload else b""
    header = struct.pack(
        HEADER,
        MAGIC,
        VERSION,
        msg_type,
        0,
        0,
        sequence,
        len(body),
        0,
        checksum32(body),
    )
    packet = header + body
    written = port.write(packet)
    if written != len(packet):
        raise RuntimeError("serial write incomplete: wrote %s of %d bytes" % (written, len(packet)))
    port.flush()


def read_exact(port, length):
    data = bytearray()
    while len(data) < length:
        chunk = port.read(length - len(data))
        if not chunk:
            raise TimeoutError("serial read timeout")
        data.extend(chunk)
    return bytes(data)


def recv_packet(port):
    header = read_exact(port, HEADER_SIZE)
    magic, version, msg_type, code, flags, seq, json_len, bin_len, checksum = struct.unpack(
        HEADER,
        header,
    )
    if magic != MAGIC or version != VERSION:
        raise RuntimeError("bad IMX5 packet")
    json_bytes = read_exact(port, json_len) if json_len else b""
    binary = read_exact(port, bin_len) if bin_len else b""
    if checksum32(json_bytes, binary) != checksum:
        raise RuntimeError("bad IMX5 checksum")
    payload = json.loads(json_bytes.decode()) if json_bytes else {}
    return msg_type, code, seq, payload, binary


def require_sequence(expected, actual):
    if actual != expected:
        raise RuntimeError("response sequence mismatch: expected %d got %d" % (expected, actual))


def raise_protocol_error(payload):
    raise RuntimeError(payload.get("error") or "device returned protocol error")


def wait_until_camera_ready(port, sequence):
    for _ in range(STARTUP_POLLS):
        send_packet(port, STATUS_REQUEST, sequence)
        msg_type, code, resp_seq, status, _ = recv_packet(port)
        require_sequence(sequence, resp_seq)
        if msg_type == ERROR_RESPONSE:
            raise_protocol_error(status)
        if msg_type != STATUS_RESPONSE:
            raise RuntimeError("unexpected response type: 0x%02x" % msg_type)

        print(
            "state=%s core=%s busy=%s ready=%s msg=%s"
            % (
                status.get("startup_state"),
                status.get("camera_core"),
                status.get("camera_operation_busy"),
                status.get("ready"),
                status.get("startup_message"),
            )
        )

        if status.get("ready") and status.get("camera_core") == "READY":
            return sequence + 1, status

        if code != CODE_OK and status.get("startup_state") == "ERROR":
            raise RuntimeError(status.get("last_error") or "camera startup failed")

        sequence += 1
        time.sleep(max(status.get("poll_after_ms", 200), 100) / 1000.0)

    raise TimeoutError("camera did not become ready")


def request_ai_frame(port, sequence):
    request = {
        "wait_ms": 200,
        "capture_timeout_ms": 10000,
        "include_jpeg": "on_ng",
    }
    for _ in range(DATA_POLLS):
        send_packet(port, GET_DATA_REQUEST, sequence, request)
        msg_type, code, resp_seq, result, jpeg = recv_packet(port)
        require_sequence(sequence, resp_seq)
        if msg_type == ERROR_RESPONSE:
            raise_protocol_error(result)
        if msg_type != DATA_RESPONSE:
            raise RuntimeError("unexpected response type: 0x%02x" % msg_type)

        status = result.get("status")
        if status == "PENDING":
            print(
                "pending job=%s state=%s elapsed=%sms"
                % (
                    result.get("job_id"),
                    result.get("camera_job_state"),
                    result.get("elapsed_ms"),
                )
            )
            sequence += 1
            time.sleep(max(result.get("poll_after_ms", 200), 50) / 1000.0)
            continue

        return sequence + 1, code, result, jpeg

    raise TimeoutError("camera data request stayed pending")


with serial.Serial(PORT, BAUDRATE, timeout=30) as port:
    sequence = 1
    sequence, status = wait_until_camera_ready(port, sequence)

    sequence, code, result, jpeg = request_ai_frame(port, sequence)
    print("response code:", code)
    print(result)
    if jpeg:
        print("jpeg bytes:", len(jpeg))
```

If `parse_metadata(...)` raises `UnicodeError`, the script treats that metadata
frame as invalid and skips it. This can happen when a partial or noisy SPI
metadata frame passes the header checks but contains non-UTF-8 bytes in
network/tensor name fields.

## Model Requirement

This example is designed for the SSD MobileNet object-detection model. Before
running `main.py`, write the `ssdmobilenet` model and its network information to
the IMX500 module Flash first.

Use the SSD MobileNet flashing commands in:

- [../../../../../tools/README.md#flash-imx500-models-over-usb](../../../../../tools/README.md#flash-imx500-models-over-usb)

The required model files are:

```text
tools/assets/models/ssd_mobilenetv2_fpnlite/imx500_network_ssd_mobilenetv2_fpnlite_320x320_pp.fpk
tools/assets/models/ssd_mobilenetv2_fpnlite/network_info.txt
```

The Pico 2 multitask example uses the same SSD MobileNet post-processing
convention. If another model is loaded, the script may still parse metadata
successfully, but detection box parsing will be skipped or the printed values
will not be meaningful.

## Build Firmware

MicroPython is included in this repository as `third_party/micropython`.
Initialize submodules first if needed:

```sh
git submodule update --init --recursive third_party/micropython
```

Build from this example directory:

```sh
cd examples/platform/rpi/pico/micropython_imx500_metadata_parse
make
```

The generated UF2 is:

```text
../../../../../third_party/micropython/ports/rp2/build-RPI_PICO-imx500-metadata-full/firmware.uf2
```

Flash this UF2 to Pico.

## Run With mpremote

Install `mpremote` on the host if needed:

```sh
python3 -m pip install mpremote
```

Check that Pico is visible:

```sh
mpremote devs
```

Run the script once without copying it to the Pico filesystem:

```sh
cd examples/platform/rpi/pico/micropython_imx500_metadata_parse
mpremote run main.py
```

To copy it as `main.py` so it runs after reset:

```sh
mpremote fs cp main.py :main.py
mpremote reset
```

## Module API Used

The script uses:

```python
imx500.open(
    None,
    None,
    imx500.MipiDataFormat.IMAGE,
    imx500.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
    10,
)
imx500.stream_on()
imx500.read_metadata(buf)
imx500.parse_metadata(buf, length=n, spi_format=..., preview_len=4096)
```

`open(...)`, `probe_imx500_module()`, and `read_metadata(buffer)` are aligned
with the pybind Python API. On Pico MicroPython, `open(...)` also initializes the
fixed Pico I2C/SPI pins before calling the SDK.

`read_metadata(buffer)` writes into the supplied `bytearray` and returns the
number of bytes written. `parse_metadata(...)` returns `None` on parse failure.
On success it returns a dictionary with `primary_header`, `networks`,
`input_tensors`, `output_tensors`, offset fields, and payload previews.

The example uses a larger `preview_len` because MicroPython receives tensor
payload bytes through each tensor's `preview` field. SSD MobileNet's
post-processed output tensors are small enough for this. If a tensor payload is
larger than `preview_len`, the script skips detection parsing for that frame and
prints the truncation reason.

## Wiring

Use the shared Pico wiring guide:

- [../README.md](../README.md)
