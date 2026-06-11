# IMX500 MCU SDK MicroPython User Module

This directory contains the MicroPython user C module for `imx500_mcu_sdk`.
It is kept next to the CPython pybind package so both bindings can expose the
same SDK-facing API.

The user module entry point is:

```text
usermod/imx500_mcu_sdk/micropython.cmake
```

Pico examples reference this CMake file through `USER_C_MODULES`. On Pico
MicroPython, `open(...)` initializes the fixed I2C/SPI pins from the
example-local `g_config.h` before calling the C++ SDK.

Core aligned APIs include:

```python
imx500_mcu_sdk.open(...)
imx500_mcu_sdk.probe_imx500_module()
imx500_mcu_sdk.stream_on()
imx500_mcu_sdk.get_metadata_size()
imx500_mcu_sdk.read_metadata(buffer)
```

`read_metadata(buffer)` writes into a caller-provided writable buffer and
returns the number of bytes written, matching the C++ SDK and pybind API.
