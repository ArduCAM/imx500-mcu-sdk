# Pico2 IMX500 Camera Serial Stream (Multitask)

This example consolidates the four task-oriented `camera_serial_stream_*` demos into a single Pico2 serial-forwarding firmware project with one host-side Python receiver script.

The Pico2 firmware is task-agnostic. It forwards `read_metadata(...)` frames over USB serial, while the host script uses `--task` to select the matching parser and renderer for the model already programmed on the IMX500 camera module.

## Supported tasks

- `classification` -> `tools/assets/models/mobilenet_v2`
- `object_detection` -> `tools/assets/models/ssd_mobilenetv2_fpnlite`
- `pose_estimation` -> `tools/assets/models/higherhrnet`
- `segmentation` -> `tools/assets/models/deeplabv3plus`

## 1. Build and flash the Pico2 firmware

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_multitask
mkdir build
cd build
cmake ..
cmake --build .
```

Flash `imx500_camera_serial_stream_multitask.uf2` to Pico2.

## 2. Install host dependencies

```bash
pip install pyserial numpy opencv-python flatbuffers
```

For pose estimation, install one additional dependency:

```bash
pip install munkres
```

## 3. Run the host receiver

List the supported task names:

```bash
python host_receiver.py --list-tasks
```

Run classification and save annotated JPEGs:

```bash
python host_receiver.py --task classification --save-img --save-metadata-json --save-tensors
```

Run object detection:

```bash
python host_receiver.py --task object_detection
```

Run pose estimation with realtime preview:

```bash
python host_receiver.py --task pose_estimation --show-img --show-fps
```

Run segmentation:

```bash
python host_receiver.py --task segmentation
```

Useful options:

- `--port`: explicitly select a serial port instead of auto-detecting one
- `--list-ports`: print all currently available serial ports and exit
- `--output`: override the task-specific default output directory
- `--max-payload`: override the task-specific payload safety limit
- `--save-img`: save annotated JPEGs (disabled by default)
- `--save-raw`: save the original framed metadata payload as `.bin`
- `--save-metadata-json`: save parsed metadata summaries as `.json`
- `--save-tensors`: save parsed tensor arrays as `.npz`
- `--save-original`: save the decoded JPEG before annotation
- `--show-img`: show the annotated OpenCV preview window for any supported task
- `--show-fps`: print host-side render/postprocess FPS for any supported task

## 4. Notes

- `--task` must match the network currently loaded on the IMX500 camera module.
- If `--port` is omitted, the host script tries to auto-detect the Pico2 USB CDC port.
- If multiple likely serial ports are present, use `--list-ports` to inspect them and then pass `--port`.
- The host receiver now prints device-side text logs, including firmware startup messages and runtime status lines, while still parsing binary frame packets.
- Annotated JPEGs are not written unless `--save-img` is provided.
- The serial packet format is unchanged from the existing `camera_serial_stream_jpeg` and task-specific demos.
- The multitask host script keeps its reusable Python helpers under `camera_serial_stream_multitask/common` for packet extraction, metadata parsing, and rendering.
- `open(nullptr, 0, nullptr, 0, ...)` intentionally does not pass model data from Pico2. Pico flash is too small to store both the forwarding firmware and an IMX500 `.fpk` package, so the model must be programmed onto the camera module separately.

To program a model onto the IMX500 camera module:

1. Hold the `Mode` button on the camera module, then connect it to the host PC over USB Type-C. A removable drive named `IMX500` should appear.
2. Drag the target `.fpk` file onto the `IMX500` drive and wait until the green LED on the back of the camera module stays on.
3. Drag the matching `network_info.txt` for that `.fpk` onto the same `IMX500` drive and wait until the red LED on the back of the camera module stays on.
4. Wait about 3 seconds. If the red and green LEDs blink twice and then turn off, the model update completed successfully.
5. After a successful update, the camera module reboots automatically.

The `network_info.txt` file must match the `.fpk` being programmed, and the programmed model must also match the host-side `--task` selection.
