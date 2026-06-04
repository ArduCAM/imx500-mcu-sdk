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

The SSD MobileNet post-processing follows
`examples/platform/rpi/pico2/camera_serial_stream_multitask/common/renderers.py`.
It expects four output tensors in this order:

1. boxes: normalized `(y1, x1, y2, x2)`
2. scores
3. class IDs
4. valid detection count

Detections with score greater than `SCORE_THRESHOLD` are printed with normalized
box coordinates and input-tensor pixel coordinates.

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
    imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR,
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
