# nRF52840 DK MicroPython IMX500 SPI Receive

This example builds MicroPython for the Nordic nRF52840 DK board
(`PCA10056`) with the `imx500_mcu_sdk` user C module, then runs a small SPI
receive smoke test from `main.py`.

It mirrors the Raspberry Pi Pico example:

- `examples/platform/rpi/pico/micropython_imx500_spi_receive`

The example initializes the IMX500 module over `TWI1`, starts streaming, reads
metadata over `SPIM3`, and prints the frame byte count plus a short byte preview.
It does not parse tensors or model metadata.

## Wiring Details

### 1. Read the Camera Header First

![](../../../../pics/B0642_connector.png)

Based on the hardware image, the camera board exposes an 8-pin header. In the
image orientation, the pins appear in this order from left to right:

| Camera pad | Signal on camera board |
| --- | --- |
| `8` | `I2C SDA` |
| `7` | `+3v3` |
| `6` | `DGND` |
| `5` | `SPI_SCK_3v3` |
| `4` | `SPI_RX_3v3` |
| `3` | `SPI_TX_3v3` |
| `2` | `SPI_CS_3v3` |
| `1` | `I2C SCL` |

Important:

- This left-to-right order is only valid for the same viewing direction as the image.
- If you flip the board or view it from the opposite side, the physical order will appear reversed.
- When wiring by hand, always confirm both the pad number and the signal name on the PCB silk.

### 2. Recommended nRF52840 DK Wiring

Use the following mapping between the camera board and nRF52840 DK. The adapter
uses `TWI1` and `SPIM3`; the SPI pins match the PCA10056 default SPI0 header
pins while still avoiding the board UART pins.

| Camera pad | Camera signal | nRF52840 DK connection | nRF52840 DK function |
| --- | --- | --- | --- |
| `8` | `I2C SDA` | `P0.26` | `TWI1 SDA` |
| `1` | `I2C SCL` | `P0.27` | `TWI1 SCL` |
| `5` | `SPI_SCK_3v3` | `P1.15` | `SPIM3 SCK` |
| `4` | `SPI_RX_3v3` | `P1.13` | `SPIM3 MOSI / TX` |
| `3` | `SPI_TX_3v3` | `P1.14` | `SPIM3 MISO / RX` |
| `2` | `SPI_CS_3v3` | `P1.12` | `SPI chip select` |
| `7` | `+3v3` | `VDD` | DK VDD, 3.0 V by default from USB |
| `6` | `DGND` | `GND` | Ground |

Do not connect the camera `+3v3` pad to any `5V` or `VIN 3-5V` pin. If your
camera setup requires exactly `3.3 V`, power the camera from an external
regulated `3.3 V` supply and keep grounds common.

### 3. Why `SPI_TX` and `SPI_RX` Can Be Confusing

The camera board silk uses `SPI_TX` and `SPI_RX` from the camera side. The
nRF52840 DK uses `MOSI/TX` and `MISO/RX` from the MCU side. Because of that:

- nRF52840 `MOSI/TX` must connect to camera `RX`
- nRF52840 `MISO/RX` must connect to camera `TX`

So the correct SPI cross-connection is:

- `P1.13 (SPIM3 MOSI / TX)` -> `camera pad 4 (SPI_RX_3v3)`
- `P1.14 (SPIM3 MISO / RX)` <- `camera pad 3 (SPI_TX_3v3)`

If you instead connect `TX -> TX` and `RX -> RX`, SPI communication will not work.

### 4. Wiring Diagram

```text
nRF52840 DK                    IMX500 camera board
-----------------------------------------------------------
P0.26  (TWI1 SDA)         ->   Pad 8  (I2C SDA)
P0.27  (TWI1 SCL)         ->   Pad 1  (I2C SCL)
P1.15  (SPIM3 SCK)        ->   Pad 5  (SPI_SCK_3v3)
P1.13  (SPIM3 MOSI / TX)  ->   Pad 4  (SPI_RX_3v3)
P1.14  (SPIM3 MISO / RX)  <-   Pad 3  (SPI_TX_3v3)
P1.12  (SPI CS)           ->   Pad 2  (SPI_CS_3v3)
VDD                       ->   Pad 7  (+3v3)
GND                       ->   Pad 6  (DGND)
```

### 5. Pin Definitions Used by the Current Code

The current nRF52840 DK example uses the nrfx encoded pin numbers below. For
Port 1 pins, the code number is `32 + pin`, so `P1.15` is `47`.

```c
#define I2C_SDA_PIN 26
#define I2C_SCL_PIN 27
#define SPI_SCK_PIN 47
#define SPI_TX_PIN 45
#define SPI_RX_PIN 46
#define SPI_CSN_PIN 44
```

These definitions are in:

- `examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/g_config.h`

## Prerequisites

Install the ARM embedded toolchain and Nordic flashing tools:

```sh
arm-none-eabi-gcc --version
nrfjprog --version
python3 -m pip install mpremote
```

Initialize MicroPython and nrfx submodules:

```sh
cd examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive
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
../../../../../third_party/micropython/ports/nrf/build-PCA10056-imx500-nrf52840-spi-receive-full/
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

## Run

Check the MicroPython serial port:

```sh
mpremote devs
```

Run the example once without copying it:

```sh
cd examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive
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
