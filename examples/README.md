# IMX500 MCU SDK Examples

Use these examples as missions. Start with the smallest visible result, confirm
the checkpoint, then continue into the product path that matches your design.

## Choose An Example Mission

| Mission | Start with | Success checkpoint | Next unlock |
| --- | --- | --- | --- |
| Run a visible AI demo | [ESP32-P4 example](platform/esp/esp32p4/README.md) | LCD preview is visible and parsed AI output or overlay appears | [SPI metadata to MCU product path](../docs/paths/spi-mcu-product-path.md) |
| Bring up Pico 2 wiring | [Pico 2 guide](platform/rpi/pico2/README.md) | Pico 2 controls IMX500 over `I2C0` and receives metadata over `SPI0` | Pico 2 serial stream or production test |
| Stream metadata to a PC | [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md) | Host receiver parses at least one metadata frame for the selected task | [Model validation mission](../docs/paths/model-validation-to-production.md) |
| Run a repeatable production check | [Pico 2 production test](platform/rpi/pico2/production_test/README.md) | Host script prints `TEST_RESULT: PASS` | [EOL test guide](../docs/production/eol-test.md) |
| Build a product-like ROI demo | [Pico W person-detection ROI MVP](platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md) | Browser UI shows frame, ROI, person count, and GP0/GP1 state | [Production design-in checklist](../docs/production/design-in-checklist.md) |

## Recommended Order

If you are evaluating the SDK for the first time:

1. Run USB validation from the repository root README.
2. Run the [ESP32-P4 visible demo](platform/esp/esp32p4/README.md) if you need LCD preview and AI overlay.
3. Run the [Pico 2 guide](platform/rpi/pico2/README.md) if you need MCU metadata bring-up.
4. Use [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md) to inspect model output on a PC.
5. Use [Pico 2 production test](platform/rpi/pico2/production_test/README.md) when you need repeatable pass/fail behavior.

## Platform Groups

| Path | Purpose |
| --- | --- |
| `platform/esp/esp32p4/` | ESP32-P4 Function EV Board wiring and visible AI demo entry point |
| `platform/rpi/pico2/` | Pico 2 wiring, serial metadata stream, integration, and production-test examples |
| `platform/rpi/pico_w/` | Pico W person-detection ROI MVP and related SPI receive experiments |
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
Confirm IMX500 open(), stream_on(), and metadata output
    |
    v
Choose model validation, SPI/MCU productization, or production test
```

## If You Are Not Sure Where To Start

- Want the fastest visible result? Start with [ESP32-P4](platform/esp/esp32p4/README.md).
- Want low-power MCU event output? Start with [Pico 2](platform/rpi/pico2/README.md).
- Want to inspect model output on a PC? Start with [Pico 2 serial stream multitask](platform/rpi/pico2/camera_serial_stream_multitask/README.md).
- Want a repeatable pass/fail station flow? Start with [Pico 2 production test](platform/rpi/pico2/production_test/README.md).
- Want a product-like web UI and GPIO event output? Start with [Pico W person-detection ROI MVP](platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md).

## Next Product Paths

- [USB Validation To USB3 UVC Deployment](../docs/paths/usb-validation-to-uvc.md)
- [MIPI / Raspberry Pi / CM5 Product](../docs/paths/mipi-rpi-product-path.md)
- [SPI Metadata To MCU Product](../docs/paths/spi-mcu-product-path.md)
- [Model Validation To Production](../docs/paths/model-validation-to-production.md)
- [Production Design-In Checklist](../docs/production/design-in-checklist.md)
