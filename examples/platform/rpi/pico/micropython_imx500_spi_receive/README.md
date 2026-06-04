# MicroPython IMX500 SPI Receive Example(experimental)

This example shows how to use the C/C++ IMX500 MCU SDK from MicroPython on
Raspberry Pi Pico.

The SDK is packaged as a MicroPython user C module named `imx500_mcu_sdk`. It is
compiled into the MicroPython RP2 firmware, then imported from `main.py`.

## What This Provides

- Pico `I2C0` and `SPI0` initialization with the same pins as the C++ example.
- `import imx500_mcu_sdk` from MicroPython.
- Basic module bring-up:
  - `get_fw_ver()`
  - `get_pid()`
  - `probe_imx500_module()`
  - `open(...)`
  - `stream_on()`
  - `read_metadata(buffer)`

The example defaults to Flash boot. It assumes the model and network info have
already been programmed into the IMX500 module flash by a native SDK example or
another tool.

## Build MicroPython With The SDK Module

MicroPython is included in this repository as `third_party/micropython`.
Initialize submodules first if you have just cloned the SDK repository:

```sh
git submodule update --init --recursive third_party/micropython
```

Then build from this example directory. The local `Makefile` automatically finds
the Pico toolchain and picotool from `~/.pico-sdk` when they are not already on
your `PATH`:

```sh
cd examples/platform/rpi/pico/micropython_imx500_spi_receive
make
```

Flash the generated RP2 MicroPython UF2 to Pico. Then copy `main.py` to the Pico
filesystem.

The generated UF2 is:

```text
../../../../../third_party/micropython/ports/rp2/build-RPI_PICO-imx500-full/firmware.uf2
```

If you need to inspect the auto-detected paths:

```sh
make print-config
```

### Build A Minimal Diagnostic Firmware

If the stock Raspberry Pi Pico MicroPython UF2 enumerates as USB serial but this
custom firmware does not, build the minimal diagnostic module first:

```sh
make clean MODULE_MODE=minimal
make MODULE_MODE=minimal
```

Flash this UF2:

```text
../../../../../third_party/micropython/ports/rp2/build-RPI_PICO-imx500-minimal/firmware.uf2
```

The minimal firmware only registers `imx500_mcu_sdk` and does not link the IMX500
MCU SDK or initialize I2C/SPI. After it boots, check:

```sh
mpremote devs
mpremote repl
```

Then in the REPL:

```python
import imx500_mcu_sdk
imx500_mcu_sdk.build_mode()
imx500_mcu_sdk.ping()
```

If this minimal firmware exposes USB serial, but the full firmware does not, the
failure is in the full IMX500 SDK link/runtime path. If the minimal firmware also
does not expose USB serial, check the MicroPython submodule/build environment and
flash process before debugging the IMX500 module.

## Run

After flashing the custom MicroPython firmware and copying `main.py`, open the
USB serial REPL. The script opens the IMX500 module from module Flash, starts
streaming, and prints metadata frame lengths. On Pico MicroPython, `open(...)`
initializes the fixed Pico I2C/SPI pins before calling the SDK.

### Run With mpremote

Install `mpremote` on the host:

```sh
python3 -m pip install mpremote
```

Connect Pico over USB, then confirm the board is visible:

```sh
mpremote devs
```

To run the example once without copying it to the Pico filesystem:

```sh
cd examples/platform/rpi/pico/micropython_imx500_spi_receive
mpremote run main.py
```

To copy the script to Pico as `main.py` so it runs after reset:

```sh
cd examples/platform/rpi/pico/micropython_imx500_spi_receive
mpremote fs cp main.py :main.py
mpremote reset
```

To open an interactive REPL after copying:

```sh
mpremote repl
```

If more than one MicroPython board is connected, pass the serial port explicitly:

```sh
mpremote connect /dev/tty.usbmodemXXXX run main.py
```

### If No USB Serial Port Appears

The generated custom firmware should expose a USB CDC REPL. You can verify the
UF2 metadata on the host:

```sh
picotool info ../../../../../third_party/micropython/ports/rp2/build-RPI_PICO-imx500-full/firmware.uf2
```

The output should include `USB REPL`.

If `mpremote devs` does not show a `/dev/cu.usbmodem*` device after flashing:

1. Unplug Pico, then plug it back in without holding `BOOTSEL`.
2. Wait a few seconds; first boot may initialize the filesystem.
3. Check whether macOS sees a serial device:

   ```sh
   ls /dev/cu.usbmodem*
   ```

4. Check whether Pico is still in UF2 boot mode. If an `RPI-RP2` drive is still
   mounted, copy the generated `firmware.uf2` to that drive again and wait for it
   to reboot.
5. Disconnect the IMX500 module and boot Pico by itself. If USB serial appears
   only with the module disconnected, check the `3V3`/`GND` wiring and current
   draw.
6. Try another USB cable or USB port. Charge-only cables will power the board but
   will not expose a serial device.
7. As a sanity check, flash a stock Raspberry Pi Pico MicroPython UF2. If stock
   MicroPython also does not create `/dev/cu.usbmodem*`, the issue is outside this
   example firmware.
8. If stock MicroPython works and the IMX500 module is disconnected, flash the
   minimal diagnostic firmware above. This separates a USB/build issue from a
   full IMX500 SDK link/runtime issue.

## Memory Notes

Pico has limited RAM. `read_metadata(buffer)` follows the C++ SDK API and writes
into a caller-provided `bytearray`, then returns the number of bytes written.
Start with a smaller buffer in `main.py`, then increase it if `read_metadata(...)`
reports that the buffer is too small.

## Wiring

Use the shared Pico wiring guide:

- [../README.md](../README.md)

The shared MicroPython user module and default pin definitions are in:

- `../../../../../python_bindings/micropython/usermod/imx500_mcu_sdk`
