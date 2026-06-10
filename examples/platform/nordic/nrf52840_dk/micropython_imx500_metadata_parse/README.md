# nRF52840 DK MicroPython IMX500 Metadata Parse

这个示例为 Nordic nRF52840 DK（MicroPython board `PCA10056`）构建带
`imx500_mcu_sdk` user C module 的 MicroPython 固件，并运行一个轻量
metadata 解析脚本。

实现思路参考：

- `examples/platform/rpi/pico/micropython_imx500_metadata_parse`

区别是 nRF52840 DK 只有 256 KB RAM，示例默认使用
`SPI_METADATA_OUTPUT_TENSOR`，不默认抓 JPEG/input tensor，以降低
`bytearray` 和解析对象的内存压力。

## 默认引脚

默认引脚在 `g_config.h` 中配置：

| IMX500 signal | nRF52840 DK pin number | PCA10056 header hint |
| --- | ---: | --- |
| I2C SDA | 26 | P0.26 |
| I2C SCL | 27 | P0.27 |
| SPI SCK | 47 | P1.15 |
| SPI MOSI / TX | 45 | P1.13 |
| SPI MISO / RX | 46 | P1.14 |
| SPI CSN | 44 | P1.12 |
| 3V3 | 3V3 | external power rail |
| GND | GND | common ground |

The adapter uses `TWI1` and `SPIM3`, so it avoids the board UART pins and the
default MicroPython SPI0 pins.

## Prerequisites

Install the ARM embedded toolchain and Nordic flashing tools:

```sh
arm-none-eabi-gcc --version
nrfjprog --version
python3 -m pip install mpremote
```

Initialize MicroPython and nrfx submodules:

```sh
cd examples/platform/nordic/nrf52840_dk/micropython_imx500_metadata_parse
make submodules
```

## Build Firmware

Build the full firmware with the IMX500 SDK module:

```sh
make
```

The local `Makefile` auto-detects `arm-none-eabi-gcc` from `PATH` or
`~/.pico-sdk/toolchain`. It also passes `LTO=0` by default because recent GCC
14 embedded toolchains can fail the nRF port link with LTO type-mismatch
warnings promoted to errors.

Generated files are under:

```text
../../../../../third_party/micropython/ports/nrf/build-PCA10056-imx500-nrf52840-full/
```

The main image is:

```text
firmware.hex
```

If you only want to verify that MicroPython can boot and import the module name,
build the minimal diagnostic image:

```sh
make clean MODULE_MODE=minimal
make MODULE_MODE=minimal
```

Then in the REPL:

```python
import imx500_mcu_sdk
imx500_mcu_sdk.build_mode()
imx500_mcu_sdk.ping()
```

## Flash Firmware

Connect the nRF52840 DK through the debug USB port and run:

```sh
make deploy
```

The default flasher is `jlink`, which calls `nrfjprog`. You can also use another
MicroPython nRF flasher supported by the port:

```sh
make deploy FLASHER=pyocd
make deploy FLASHER=openocd
```

After flashing, reconnect the board if the USB CDC serial device does not appear.

## Deploy and Run `main.py`

Check the MicroPython serial port:

```sh
mpremote devs
```

Run the example once without copying it:

```sh
mpremote run main.py
```

Copy it to the board filesystem so it runs after reset:

```sh
mpremote fs cp main.py :main.py
mpremote reset
```

If multiple boards are connected, pass the port explicitly:

```sh
mpremote connect /dev/cu.usbmodemXXXX run main.py
```

## Model Requirement

This example assumes the IMX500 module flash already contains a model and network
information. Use the model flashing flow in:

- `../../../../../tools/README.md`

## Memory Notes

`main.py` allocates a 48 KB metadata buffer. If your model produces larger
metadata, increase `METADATA_BUFFER_SIZE` carefully and watch `gc.mem_free()` in
the logs. If the board resets or raises `MemoryError`, reduce the SPI format or
avoid JPEG/input tensor payloads on nRF52840.

To enable JPEG/input tensor parsing for experiments, change:

```python
SPI_FORMAT = imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR
```

to a larger format such as:

```python
SPI_FORMAT = imx500.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR
```

Then increase `METADATA_BUFFER_SIZE`.
