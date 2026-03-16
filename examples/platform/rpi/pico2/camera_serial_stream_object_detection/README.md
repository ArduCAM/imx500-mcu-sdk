# Pico2 IMX500 Camera Serial Stream (Object Detection)

`camera_serial_stream_object_detection` targets `tools/assets/models/ssd_mobilenetv2_fpnlite` and reuses the serial JPEG metadata transport from `camera_serial_stream_jpeg`.

## Device side

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_object_detection
mkdir build
cd build
cmake ..
cmake --build .
```

Flash the generated `.uf2` to Pico2.

## Host side

```bash
pip install pyserial numpy opencv-python flatbuffers
python host_receiver.py --port COM7 --save-metadata-json --save-tensors
```

The default renderer expects parsed tensors in the same layout as `postprocess/parse_mobilenetssd.py`.

`metadata_bridge.py` is kept separate so you can adjust tensor-to-postprocess wiring without touching the serial/JPEG/metadata pipeline.
