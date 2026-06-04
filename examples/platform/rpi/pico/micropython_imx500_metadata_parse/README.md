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
- output tensor payload offsets and short byte previews

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
imx500.parse_metadata(buf, length=n, spi_format=..., preview_len=16)
```

`open(...)`, `probe_imx500_module()`, and `read_metadata(buffer)` are aligned
with the pybind Python API. On Pico MicroPython, `open(...)` also initializes the
fixed Pico I2C/SPI pins before calling the SDK.

`read_metadata(buffer)` writes into the supplied `bytearray` and returns the
number of bytes written. `parse_metadata(...)` returns `None` on parse failure.
On success it returns a dictionary with `primary_header`, `networks`,
`input_tensors`, `output_tensors`, offset fields, and payload previews.

## Wiring

Use the shared Pico wiring guide:

- [../README.md](../README.md)
