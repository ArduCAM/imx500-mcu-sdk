# IMX500 MCU SDK Python Bindings

This directory contains the pybind11 Python bindings for the IMX500 MCU SDK,
plus PC-side debug tools that communicate with the camera module over USB.

## Requirements

- The camera module firmware version must be `0x00000010` or newer.
- Before using these tools, hold the camera module `MODE` button while connecting
  the module to the PC over USB.
- The PC must have Python 3.9 or newer.
- USB serial access requires `pyserial`; building the Python bindings requires
  `pybind11`.

## Python Dependencies

The base bindings and USB tools require:

```bash
python3 -m pip install setuptools wheel pybind11 pyserial
```

`tools/imx500_jpeg_preview.py` also requires OpenCV and NumPy:

```bash
python3 -m pip install opencv-python numpy
```

## Install

Run this from this directory:

```bash
python3 -m pip install -e . --no-build-isolation
```

For local development without installing into the current Python environment,
build the extension in place from this directory and run tools with
`PYTHONPATH=python`:

```bash
python3 setup.py build_ext --inplace
PYTHONPATH=python python3 -c "import imx500_mcu_sdk; print(imx500_mcu_sdk)"
```

## Metadata Reads

`imx500_mcu_sdk.read_metadata(buffer)` follows the C++ SDK API: pass a writable
buffer, and the function returns the number of bytes written. The buffer must be
large enough for the next metadata frame.

```python
size = imx500_mcu_sdk.get_metadata_size()
buf = bytearray(size)
n = imx500_mcu_sdk.read_metadata(buf)
frame = bytes(memoryview(buf)[:n])
```

`imx500_mcu_sdk.parse_metadata(frame, spi_format=..., preview_len=...)` parses
one raw metadata frame and returns a dictionary with network, input tensor, and
output tensor descriptors. It returns `None` if the frame does not match the
selected SPI metadata layout.

## MicroPython User Module

The MicroPython user C module lives in:

```text
micropython/usermod/imx500_mcu_sdk
```

It exposes the same core SDK function names as the pybind module, including
`imx500_open(...)`, `probe_imx500_module()`, `stream_on()`, `get_metadata_size()`, and
`read_metadata(buffer)`. On Pico MicroPython, `imx500_open(...)` also initializes the
fixed I2C/SPI pins from the example-local `g_config.h` before calling the SDK.

## Tools

Run the following tools from this directory. If you installed the package in
editable mode, you can run them directly. If you built the extension in place,
prefix the command with `PYTHONPATH=python`.

### `tools/imx500_first_run.py`

Runs the recommended first validation flow with checkpoint-style output. It
connects to the USB bridge, probes the IMX500 module, calls
`imx500_mcu_sdk.imx500_open()`, starts the stream, and saves one metadata frame. Add
`--jpeg-preview` to request JPEG metadata output and save one extracted JPEG
preview frame without opening a preview window.

```bash
PYTHONPATH=python python3 tools/imx500_first_run.py
PYTHONPATH=python python3 tools/imx500_first_run.py --jpeg-preview
```

### `tools/imx500_usb_flash.py`

Writes a `.fpk` model and `network_info.txt` to the camera module Flash through
the SDK USB bridge. Use this tool to update the model files persisted on the
module.

```bash
PYTHONPATH=python python3 tools/imx500_usb_flash.py --status
PYTHONPATH=python python3 tools/imx500_usb_flash.py \
  --model path/to/model.fpk \
  --network-info path/to/network_info.txt
```

### `tools/imx500_sdk_open_test.py`

Validates the basic SDK path, including `imx500_mcu_sdk.imx500_open()`,
`imx500_mcu_sdk.stream_on()`, and metadata reads. It can use the model already
stored in module Flash, or temporarily load a model and `network_info` over USB
for direct-boot testing.

```bash
PYTHONPATH=python python3 tools/imx500_sdk_open_test.py --help
PYTHONPATH=python python3 tools/imx500_sdk_open_test.py \
  --stream-on \
  --metadata-frames 1
```

### `tools/imx500_output_tensor_tasks.py`

Loads the four bundled task models with direct boot and prints output tensor
metadata plus raw and dequantized preview values for each parsed frame.

```bash
PYTHONPATH=python python3 tools/imx500_output_tensor_tasks.py --help
PYTHONPATH=python python3 tools/imx500_output_tensor_tasks.py
PYTHONPATH=python python3 tools/imx500_output_tensor_tasks.py --task object_detection
```

### `tools/imx500_yolo_output_tensor_tasks.py`

Loads the four bundled YOLO-series models with direct boot and prints output
tensor metadata plus raw and dequantized preview values for each parsed frame.
With `--preview` and a single task, it opens a continuous OpenCV preview window;
press `q` or `Esc` in the window to stop. Pass `--frames-per-task N` to capture a
fixed number of preview frames instead.

```bash
PYTHONPATH=python python3 tools/imx500_yolo_output_tensor_tasks.py --help
PYTHONPATH=python python3 tools/imx500_yolo_output_tensor_tasks.py
PYTHONPATH=python python3 tools/imx500_yolo_output_tensor_tasks.py --task segmentation
PYTHONPATH=python python3 tools/imx500_yolo_output_tensor_tasks.py --task object_detection --preview
PYTHONPATH=python python3 tools/imx500_yolo_output_tensor_tasks.py --task pose_estimation --preview --preview-save-dir yolo_pose_preview
```

### `tools/imx500_jpeg_preview.py`

Enables JPEG metadata output mode, extracts JPEG images from metadata frames,
and previews them on the PC with OpenCV. This is useful for quickly checking the
ISP/JPEG output path and realtime image pipeline.

```bash
PYTHONPATH=python python3 tools/imx500_jpeg_preview.py --help
PYTHONPATH=python python3 tools/imx500_jpeg_preview.py
```
