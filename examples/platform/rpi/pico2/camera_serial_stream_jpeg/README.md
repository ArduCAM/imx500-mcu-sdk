# Pico2 IMX500 Camera Serial Stream (JPEG)

This example runs on Raspberry Pi Pico2, opens IMX500 stream in JPEG input-tensor mode, sends each SPI metadata frame over USB serial, and decodes/saves JPEG images on host.

## 1. Build (device side)

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_jpeg
mkdir build
cd build
cmake ..
cmake --build .
```

Flash generated `imx500_camera_serial_stream_jpeg.uf2` to Pico2.

## 2. Packet format

All fields are little-endian.

- `magic[4]`: `IMX5`
- `version` (`uint8`): `1`
- `packet_type` (`uint8`): `1` (frame packet)
- `header_len` (`uint16`): `20`
- `sequence` (`uint32`): frame counter
- `payload_len` (`int32`): frame payload length, `-1` means read failure
- `checksum` (`uint32`): sum(payload bytes) mod 2^32
- `payload`: raw `read_metadata(...)` bytes

## 3. Host receiver and JPEG save

Install dependencies:

```bash
pip install pyserial numpy opencv-python
```

Run:

```bash
python host_receiver.py --port COM7 --baud 921600 --output jpeg_frames --max-frames 100
```

Options:

- `--save-raw`: save original payload as `.bin` together with `.jpg`
- `--max-payload`: upper bound protection for packet payload size

## 4. JPEG parsing logic

Host script follows this logic (same idea as your reference):

1. Use `IMX500OutputParser._unpack_header(data)` to parse input-tensor header.
2. Compute `image_data_offset = 12 + size_of_ap_parameter`.
3. Read 4-byte JPEG length at `image_data_offset`.
4. Align JPEG block size with padding factor `1024` and minimum `4KB`.
5. Extract JPEG block, trim by SOI/EOI markers if available, then `cv2.imdecode` + `cv2.imwrite`.