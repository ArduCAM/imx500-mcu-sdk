# IMX500 MCU SDK Examples

Use these examples as missions. Start with the smallest visible result, confirm
the checkpoint, then continue into the product path that matches your design.

## Choose An Example Mission

| Mission | Start with | Success checkpoint | Next unlock |
| --- | --- | --- | --- |
| Run a visible AI demo | [ESP32-P4 example](platform/esp/esp32p4/README.md) | LCD preview is visible and parsed AI output or overlay appears | [SPI metadata to MCU product path](../docs/paths/spi-mcu-product-path.md) |
| Bring up Pico or MicroPython | [Pico guide](platform/rpi/pico/README.md) | Pico controls IMX500 over `I2C0` and receives metadata over `SPI0` from C++ or MicroPython | [MicroPython metadata parse](platform/rpi/pico/micropython_imx500_metadata_parse/README.md) |
| Parse metadata from MicroPython | [Pico MicroPython metadata parse](platform/rpi/pico/micropython_imx500_metadata_parse/README.md) | `main.py` prints parsed network/tensor data, detections, or UART product frames | [SPI metadata to MCU product path](../docs/paths/spi-mcu-product-path.md) |
| Bring up Pico 2 wiring | [Pico 2 guide](platform/rpi/pico2/README.md) | Pico 2 controls IMX500 over `I2C0` and receives metadata over `SPI0` | Pico 2 serial stream or production test |
| Stream metadata to a PC | [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md) | Host receiver parses at least one metadata frame for the selected task | [Model validation mission](../docs/paths/model-validation-to-production.md) |
| Measure SPI inference delivery | [Pico 2 inference FPS benchmark](platform/rpi/pico2/inference_fps_benchmark/README.md) | Firmware prints first-frame latency, inference FPS, payload size, and effective SPI throughput | [Benchmark results](../docs/benchmark.md) |
| Run a repeatable production check | [Pico 2 production test](platform/rpi/pico2/production_test/README.md) | Host script prints `TEST_RESULT: PASS` | [EOL test guide](../docs/production/eol-test.md) |
| Build a product-like ROI demo | [Pico W person-detection ROI MVP](platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md) | Browser UI shows frame, ROI, person count, and GP0/GP1 state | [Production design-in checklist](../docs/production/design-in-checklist.md) |
| Validate Raspberry Pi 5 metadata | [RPi5 SPI receive integration test](platform/rpi/rpi5/README.md) | Raspberry Pi 5 previews over MIPI and reads IMX500 metadata through Linux `i2c-dev`/`spidev` | [MIPI / Raspberry Pi / CM5 Product](../docs/paths/mipi-rpi-product-path.md) |
| Exercise Raspberry Pi 5 model asset operations | [RPi5 I2C payload test](platform/rpi/rpi5/README.md#choose-a-raspberry-pi-5-mission) | A direct-load, flash-write, or flash-cycle command completes over the camera connector I2C bus | [Model validation mission](../docs/paths/model-validation-to-production.md) |
| Bring up Nordic MicroPython | [nRF52840 DK SPI receive](platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/README.md) | The board imports `imx500_mcu_sdk`, starts the stream, and prints a metadata frame length | [MicroPython user module](../python_bindings/micropython/README.md) |

## Recommended Order

If you are evaluating the SDK for the first time:

1. Run USB validation from the repository root README.
2. Run the [ESP32-P4 visible demo](platform/esp/esp32p4/README.md) if you need LCD preview and AI overlay.
3. Run the [Pico guide](platform/rpi/pico/README.md) if you want C++ or MicroPython bring-up on RP2040.
4. Run the [Pico 2 guide](platform/rpi/pico2/README.md) if you need MCU metadata bring-up on RP2350.
5. Use [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md) to inspect model output on a PC.
6. Use the [Pico 2 inference FPS benchmark](platform/rpi/pico2/inference_fps_benchmark/README.md) to compare SPI payload modes.
7. Use [Pico 2 production test](platform/rpi/pico2/production_test/README.md) when you need repeatable pass/fail behavior.

For Nordic MicroPython or Linux-hosted model asset operations, branch directly
to the [nRF52840 DK example](platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/README.md)
or the [Raspberry Pi 5 guide](platform/rpi/rpi5/README.md).

## Platform Groups

| Path | Purpose |
| --- | --- |
| `platform/esp/esp32p4/` | ESP32-P4 Function EV Board wiring and visible AI demo entry point |
| `platform/nordic/nrf52840_dk/` | nRF52840 DK MicroPython firmware and SPI metadata smoke test |
| `platform/rpi/` | Raspberry Pi family index for Pico, Pico 2, Pico W, and Raspberry Pi 5 examples |
| `platform/rpi/pico/` | Pico C++ SPI receive and MicroPython SDK module examples |
| `platform/rpi/pico2/` | Pico 2 wiring, serial metadata stream, FPS benchmark, integration, and production-test examples |
| `platform/rpi/pico_w/` | Pico W person-detection ROI MVP and related SPI receive experiments |
| `platform/rpi/rpi5/` | Raspberry Pi 5 MIPI preview, Linux SPI metadata test, and I2C payload model operations |
| `pics/` | Shared images used by example documentation |

## Success Pattern

Most examples follow the same sequence:

```text
Wire the camera
    |
    v
Build and flash firmware
    |
    v
Open serial monitor or host receiver
    |
    v
Confirm IMX500 imx500_open(), stream_on(), and metadata output
    |
    v
Choose model validation, SPI/MCU productization, or production test
```

## If You Are Not Sure Where To Start

- Want the fastest visible result? Start with [ESP32-P4](platform/esp/esp32p4/README.md).
- Want to script the SDK from MicroPython? Start with [Pico MicroPython metadata parse](platform/rpi/pico/micropython_imx500_metadata_parse/README.md).
- Want low-power MCU event output? Start with [Pico 2](platform/rpi/pico2/README.md).
- Want to inspect model output on a PC? Start with [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md).
- Want to measure metadata delivery FPS? Start with the [Pico 2 inference FPS benchmark](platform/rpi/pico2/inference_fps_benchmark/README.md).
- Want a repeatable pass/fail station flow? Start with [Pico 2 production test](platform/rpi/pico2/production_test/README.md).
- Want a product-like web UI and GPIO event output? Start with [Pico W person-detection ROI MVP](platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md).
- Want to validate the Linux/Raspberry Pi path? Start with [RPi5](platform/rpi/rpi5/README.md).
- Want MicroPython on Nordic hardware? Start with the [nRF52840 DK example](platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/README.md).

## Next Product Paths

- [USB Validation To USB3 UVC Deployment](../docs/paths/usb-validation-to-uvc.md)
- [MIPI / Raspberry Pi / CM5 Product](../docs/paths/mipi-rpi-product-path.md)
- [SPI Metadata To MCU Product](../docs/paths/spi-mcu-product-path.md)
- [Model Validation To Production](../docs/paths/model-validation-to-production.md)
- [Production Design-In Checklist](../docs/production/design-in-checklist.md)
