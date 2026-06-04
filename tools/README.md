# IMX500 Tools

This directory contains model assets and helper scripts used by the MCU SDK
examples.

## Flash IMX500 Models Over USB

Several MCU examples open the IMX500 module with `model=None` and
`network_info=None`. In that mode, the model must already be programmed into the
IMX500 module Flash before the MCU example starts streaming.

The bundled model assets live under:

```text
tools/assets/models
```

Each model directory contains:

- a `.fpk` model package
- the matching `network_info.txt`

Always flash the `.fpk` and `network_info.txt` from the same directory.

## SSD MobileNet Example

Use this model for SSD MobileNet object-detection examples:

```text
tools/assets/models/ssd_mobilenetv2_fpnlite/imx500_network_ssd_mobilenetv2_fpnlite_320x320_pp.fpk
tools/assets/models/ssd_mobilenetv2_fpnlite/network_info.txt
```

## Prepare The Module

1. Disconnect the MCU example firmware from the module if it is currently
   streaming.
2. Connect the IMX500 module to the host over USB in bridge/control mode.
   Hold the module `MODE` button while connecting USB if your module requires
   that to expose the bridge/control serial interface.
3. Install Python dependencies:

```sh
python3 -m pip install setuptools wheel pybind11 pyserial
```

If auto-detection cannot find the serial port, pass `--port` explicitly. On
macOS this is usually a `/dev/cu.*` device, on Linux usually `/dev/ttyACM*`, and
on Windows usually `COMx`.

## Recommended Flasher

For camera firmware `0x00000010` or newer, use the Python binding USB bridge
flasher. From the SDK root:

```sh
cd python_bindings
python3 -m pip install -e . --no-build-isolation
python3 tools/imx500_usb_flash.py --status
python3 tools/imx500_usb_flash.py \
  --model ../tools/assets/models/ssd_mobilenetv2_fpnlite/imx500_network_ssd_mobilenetv2_fpnlite_320x320_pp.fpk \
  --network-info ../tools/assets/models/ssd_mobilenetv2_fpnlite/network_info.txt
```

Use `--status` first to confirm that the bridge responds. Add `--port` if
multiple serial devices are connected:

```sh
python3 tools/imx500_usb_flash.py --port /dev/cu.usbmodemXXXX --status
```

## Legacy Flasher

For camera firmware older than `0x00000010`, or for firmware that exposes the
legacy dedicated control CDC protocol, use `tools/imx500_usb_flash.py` from the
SDK root:

```sh
python3 tools/imx500_usb_flash.py --status
python3 tools/imx500_usb_flash.py \
  --model tools/assets/models/ssd_mobilenetv2_fpnlite/imx500_network_ssd_mobilenetv2_fpnlite_320x320_pp.fpk \
  --network-info tools/assets/models/ssd_mobilenetv2_fpnlite/network_info.txt
```

Use `--port` if auto-detection does not select the correct control CDC port:

```sh
python3 tools/imx500_usb_flash.py --port /dev/cu.usbmodemXXXX --status
```

## Other Bundled Models

| Task | Model directory |
| --- | --- |
| Classification | `tools/assets/models/mobilenet_v2` |
| Object detection | `tools/assets/models/ssd_mobilenetv2_fpnlite` |
| Pose estimation | `tools/assets/models/higherhrnet` |
| Segmentation | `tools/assets/models/deeplabv3plus` |

To flash a different task model, keep the same command shape and replace both
paths with files from the selected model directory.

After flashing completes successfully, restart the MCU example and make sure its
task/post-processing matches the model that was programmed into module Flash.
