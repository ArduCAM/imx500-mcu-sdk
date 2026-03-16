# Pico2 IMX500 Camera Serial Stream (Pose Estimation)

`camera_serial_stream_pose_estimation` targets `tools/assets/models/higherhrnet`.

The serial packet handling, JPEG extraction, metadata parsing, and sidecar saving path are already in place. Host-side drawing reuses `postprocess/parse_higherhrnet.py`, and `PoseRenderer.render()` accepts `networks` directly.

## Device side

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_pose_estimation
mkdir build
cd build
cmake ..
cmake --build .
```

## Host side

```bash
pip install pyserial numpy opencv-python flatbuffers munkres
python host_receiver.py --port COM7 --save-metadata-json --save-tensors --show-img
```

Useful runtime preview args:

- `--show-img`: show the annotated output image in a realtime OpenCV window
- `--show-input-tensor`: show the network input tensor image window
- `--show-fps`: print HigherHRNet postprocess FPS

`metadata_bridge.py` should return the output-parser-style `networks` structure. The renderer will feed those tensors into the HigherHRNet postprocess code and draw the resulting keypoints and boxes on the saved `.jpg`.
