# Mission: Stream IMX500 Metadata From Pico 2 To A PC

This example consolidates the four task-oriented `camera_serial_stream_*` demos
into one Pico 2 serial-forwarding firmware project with one host-side Python
receiver script.

The Pico 2 firmware is task-agnostic. It forwards `read_metadata(...)` frames
over USB serial, while the host script uses `--task` to select the matching
parser and renderer for the model already programmed on the IMX500 camera
module.

## Goal

Receive IMX500 metadata frames on Pico 2, forward them to a PC, and parse the
output as classification, object detection, pose estimation, or segmentation.

## Hardware

- Raspberry Pi Pico 2 wired to the IMX500 module.
- Arducam IMX500 camera module with a matching model already programmed.
- USB cable from Pico 2 to the host PC.

Use the shared [Pico 2 wiring guide](../README.md) before building this mission.

## Supported Tasks

| Task | Expected model directory |
| --- | --- |
| `classification` | `tools/assets/models/mobilenet_v2` |
| `object_detection` | `tools/assets/models/ssd_mobilenetv2_fpnlite` |
| `pose_estimation` | `tools/assets/models/higherhrnet` |
| `segmentation` | `tools/assets/models/deeplabv3plus` |

The programmed model must match the host-side `--task` selection.

## Run

Build and flash the Pico 2 firmware:

```bash
cd examples/platform/rpi/pico2/camera_serial_stream_multitask
mkdir build
cd build
cmake ..
cmake --build .
```

Flash `imx500_camera_serial_stream_multitask.uf2` to Pico 2.

Install host dependencies:

```bash
pip install pyserial numpy opencv-python flatbuffers
```

For pose estimation, install one additional dependency:

```bash
pip install munkres
```

List the supported task names:

```bash
python host_receiver.py --list-tasks
```

Run one task:

```bash
python host_receiver.py --task object_detection
```

Useful task commands:

```bash
python host_receiver.py --task classification --save-img --save-metadata-json --save-tensors
python host_receiver.py --task pose_estimation --show-img --show-fps
python host_receiver.py --task segmentation
```

## Expected Feedback

You should see:

- Pico 2 firmware startup messages in the host receiver output.
- Binary frame packets arriving over USB serial.
- Parsed metadata summaries, rendered preview, or saved artifacts depending on
  the host options.
- Optional annotated JPEGs, raw payloads, metadata JSON, or tensor `.npz` files.

## You Passed This Mission When

- `host_receiver.py --list-tasks` prints the task list.
- The selected `--task` matches the model programmed on the module.
- At least one metadata frame is received and parsed.
- No repeated serial framing, payload-size, or metadata parse errors appear.

## If It Fails

- If no serial port is found, run `python host_receiver.py --list-ports` and pass `--port`.
- If frames arrive but parsing fails, confirm the selected `--task` matches the programmed model.
- If the module does not stream, confirm Pico 2 wiring and model programming.
- If annotated images are missing, confirm `--save-img` or `--show-img` was requested.

## Host Receiver Options

- `--port`: explicitly select a serial port instead of auto-detecting one.
- `--list-ports`: print all currently available serial ports and exit.
- `--output`: override the task-specific default output directory.
- `--max-payload`: override the task-specific payload safety limit.
- `--save-img`: save annotated JPEGs.
- `--save-raw`: save the original framed metadata payload as `.bin`.
- `--save-metadata-json`: save parsed metadata summaries as `.json`.
- `--save-tensors`: save parsed tensor arrays as `.npz`.
- `--save-original`: save the decoded JPEG before annotation.
- `--show-img`: show the annotated OpenCV preview window for any supported task.
- `--show-fps`: print host-side render/postprocess FPS for any supported task.

## Model Programming

The Pico 2 firmware intentionally calls `open(nullptr, 0, nullptr, 0, ...)`.
Pico flash is too small to store both the forwarding firmware and an IMX500
`.fpk` package, so the model must be programmed onto the camera module
separately.

Flash the model directory that matches the `--task` value before running the
host receiver. The supported model directories are listed in the table above.

Use the USB flashing guide in
[../../../../../tools/README.md#flash-imx500-models-over-usb](../../../../../tools/README.md#flash-imx500-models-over-usb).
It includes the recommended Python binding flasher, the legacy
`tools/imx500_usb_flash.py` flow, `--status`, `--port`, and concrete SSD
MobileNet command examples.

## Next Unlock

- Validate the selected model with the [model validation mission](../../../../../docs/paths/model-validation-to-production.md).
- Convert parsed tensors into product events with the [SPI metadata path](../../../../../docs/paths/spi-mcu-product-path.md).
- Move from prototype parsing to a repeatable check with [production_test](../production_test/README.md).
