# Pico2 IMX500 Camera Serial Stream (Segmentation)

`camera_serial_stream_segmentation` targets `tools/assets/models/deeplabv3plus`.

This model produces a much larger output tensor than the JPEG demo, so both the Pico-side frame buffer and the host-side default `--max-payload` are increased.

## Device side

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_segmentation
mkdir build
cd build
cmake ..
cmake --build .
```

## Host side

```bash
pip install pyserial numpy opencv-python flatbuffers
python host_receiver.py --port COM7 --save-metadata-json --save-tensors
```

The renderer follows the same overlay idea as `postprocess/parse_deeplabv3plus.py`.
