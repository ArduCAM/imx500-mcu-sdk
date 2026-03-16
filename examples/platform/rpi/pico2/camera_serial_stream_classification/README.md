# Pico2 IMX500 Camera Serial Stream (Classification)

`camera_serial_stream_classification` builds on `camera_serial_stream_jpeg` and targets `tools/assets/models/mobilenet_v2`.

## Device side

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_classification
mkdir build
cd build
cmake ..
cmake --build .
```

Flash the generated `.uf2` to Pico2.

This example still calls `open(nullptr, 0, nullptr, 0, ...)`, so the IMX500 module must already have the matching network loaded.

## Host side

Install dependencies:

```bash
pip install pyserial numpy opencv-python flatbuffers
```

Run:

```bash
python host_receiver.py --port COM7 --save-metadata-json --save-tensors
```

The host script will:

- receive framed serial packets
- decode JPEG bytes from metadata
- parse AP params and output tensors
- save annotated `.jpg`
- optionally save `.json`, `.npz`, and raw `.bin` sidecars

`metadata_bridge.py` is the hook point if you want to change how parsed metadata is handed to the renderer.
