# IMX500 MCU SDK

<img src="https://www.arducam.com/wp-content/uploads/2023/07/logo-4.png" width="100%" />

Bring sensor-side AI vision to MCU and low-cost SoC products.

`imx500-mcu-sdk` helps MCU applications control Arducam IMX500 camera modules, start
inference streams, and consume AI metadata or tensors over a portable `I2C` + `SPI`
interface. Use it when your product needs vision events from the sensor, not a full
Linux vision pipeline.

## Choose Your Mission

Start with one small win. Each mission gives you a visible checkpoint and unlocks
the next IMX500 product path.

| Mission | Start with | Success checkpoint | Next unlock |
| --- | --- | --- | --- |
| Validate the camera over USB | [Python USB tools](python_bindings/README.md) | USB bridge detected, SDK `imx500_open()` completes, one metadata frame is received, optional JPEG preview is saved | [USB3 UVC deployment path / B0566](docs/paths/usb-validation-to-uvc.md) |
| Run a visible AI demo | [ESP32-P4 example](examples/platform/esp/esp32p4/README.md) | LCD preview is visible and the serial log prints parsed AI output | [MCU integration path](docs/paths/spi-mcu-product-path.md) |
| Read AI metadata on an MCU | [Pico 2 serial stream example](examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md) | MCU receives an IMX500 metadata frame and forwards or parses it into an event | [SPI metadata productization path](docs/paths/spi-mcu-product-path.md) |
| Prototype with MicroPython | [Pico MicroPython metadata parse example](examples/platform/rpi/pico/micropython_imx500_metadata_parse/README.md) | Pico imports `imx500_mcu_sdk`, parses metadata, and prints SSD MobileNet detections or UART product frames | [MicroPython user module](python_bindings/micropython/README.md) |
| Bring up Nordic MicroPython | [nRF52840 DK MicroPython SPI receive example](examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/README.md) | nRF52840 DK imports `imx500_mcu_sdk`, opens IMX500 over `TWI1`, and reads one metadata frame over `SPIM3` | [MicroPython user module](python_bindings/micropython/README.md) |
| Test a model | [Model validation mission](docs/paths/model-validation-to-production.md) | Known model or real hardware output produces parsed metadata | [Application pack](examples/README.md) / [model conversion](https://www.arducam.com/arducam-ai-model-converter-tutorial) |
| Train your own model | [Custom model training guide](docs/missions/06-train-and-deploy-custom-model.md) | Trained model is converted, packaged into `.fpk` + `network_info.txt`, flashed to B0642, and parsed with custom post-processing | [Model validation to production](docs/paths/model-validation-to-production.md) |
| Port the SDK to your board | Integration guide: [中文](docs/zho/integration-guide.md), [English](docs/eng/integration-guide.md), [日本語](docs/jpn/integration-guide.md) | Your platform callbacks can probe the module, start the stream, and read metadata | [Custom MCU product path](docs/paths/spi-mcu-product-path.md) |
| Move toward production | [Production checklist](docs/production/design-in-checklist.md) | Interface, model, optics, enclosure, and factory test flow are confirmed | [Design-in, customization, and volume supply](docs/production/support-options.md) |

Need the API surface while building? Use the
[Doxygen API docs](https://arducam.github.io/imx500-mcu-sdk/).

## Quick USB Validation

[Demo Video](https://cdn.arducam.com/wp-content/uploads/2026/05/B0642_usb_get_start.mp4)

If you just received a B0642 module, validate the camera from a PC before wiring
an MCU. The optional Python host tools can exercise the USB bridge,
`imx500_mcu_sdk.imx500_open()`, metadata reads, JPEG preview, and model flashing flows.

Before connecting the module to the host PC, hold down the module `MODE` button,
then plug in the host-side USB cable. Keep `MODE` button pressed until the USB bridge is
detected by the host.

```bash
git clone --recursive https://github.com/ArduCAM/imx500-mcu-sdk.git
cd imx500-mcu-sdk
git submodule update --init --recursive
cd python_bindings
python3 -m pip install pybind11
python3 -m pip install -e . --no-build-isolation
```

If this is the first time using the module, flash a model and its matching
`network_info.txt` before running validation. For example, write the bundled
HigherHRNet model to module Flash:

```bash
PYTHONPATH=python python3 tools/imx500_usb_flash.py \
  --model ../tools/assets/models/higherhrnet/network.fpk \
  --network-info ../tools/assets/models/higherhrnet/network_info.txt
PYTHONPATH=python python3 tools/imx500_first_run.py
```

You passed this checkpoint when the tool connects to the USB bridge, `imx500_open()`
returns success, and at least one metadata frame is saved.

Next unlocks:

- Want PC/Linux plug-and-play deployment? Continue toward the [USB3 UVC path](docs/paths/usb-validation-to-uvc.md).
- Want a visual MCU demo? Continue with the [ESP32-P4 example](examples/platform/esp/esp32p4/README.md).
- Want to test a model first? Continue with the [model validation mission](docs/paths/model-validation-to-production.md).
- Want to train your own model? Continue with the [custom model training guide](docs/missions/06-train-and-deploy-custom-model.md).
- Want Linux-hosted product packaging? Continue with the [MIPI / Raspberry Pi / CM5 path](docs/paths/mipi-rpi-product-path.md).
- Want low-power event output? Continue with the [SPI metadata path](docs/paths/spi-mcu-product-path.md).

## Train Your Own Model

If the bundled models do not match your scene, start with the
[custom model training guide](docs/missions/06-train-and-deploy-custom-model.md). It walks through the
`YOLOv8n` example path from training, IMX500 conversion and quantization,
Raspberry Pi or online packaging, `.rpk` to `.fpk` extraction, USB flashing to
B0642, and the custom post-processing work required to turn output tensors into
product events.

After the custom model produces readable metadata, use the
[model validation to production path](docs/paths/model-validation-to-production.md)
to decide whether the result is ready for USB, MIPI/Linux, or SPI/MCU
productization.

## What You Can Build

- People counting and occupancy sensing
- Object, package, or shelf-counting devices
- Safety-zone and region-of-interest monitoring
- Smart HMI devices that react to nearby people or objects
- Low-bandwidth IoT vision endpoints that report events instead of video
- MCU-based products that need IMX500 inference without a Linux host

## How It Works

```text
MCU application
    |
    |  C/C++ SDK API
    v
IMX500 MCU SDK
    |
    |  I2C control, SPI metadata / tensor payload
    v
Arducam IMX500 camera module
    |
    |  MIPI image stream, sensor-side AI output
    v
Display, host processor, event logic, or product firmware
```

The SDK provides:

- `I2C` and `SPI` callback registration for platform portability
- Firmware and network-info loading support
- Stream control and runtime state queries
- Metadata readout over `SPI`
- Metadata parsing into network and tensor descriptors
- Optional SPI forwarding modes for validation workflows
- A platform adapter pattern for bringing up new MCU boards

## Supported Platforms

| Platform | Status | Entry point |
| --- | --- | --- |
| ESP32-P4 Function EV Board | Reference display and metadata demo | [examples/platform/esp/esp32p4](examples/platform/esp/esp32p4/README.md) |
| Raspberry Pi Pico | C++ SPI receive and MicroPython SDK module examples | [examples/platform/rpi/pico](examples/platform/rpi/pico/README.md) |
| Raspberry Pi Pico 2 | Wiring, serial metadata stream, production test examples | [examples/platform/rpi/pico2](examples/platform/rpi/pico2/README.md) |
| Raspberry Pi Pico W | Person-detection ROI MVP and SPI receive test examples | [examples/platform/rpi/pico_w](examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md) |
| Raspberry Pi 5 | Experimental MIPI preview plus Linux I2C/SPI metadata test | [examples/platform/rpi/rpi5](examples/platform/rpi/rpi5/README.md) |
| Nordic nRF52840 DK | MicroPython SDK module SPI metadata smoke test | [examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive](examples/platform/nordic/nrf52840_dk/micropython_imx500_spi_receive/README.md) |
| Other MCU platforms | Port by implementing the SDK adapter callbacks | Integration guide: [中文](docs/zho/integration-guide.md), [English](docs/eng/integration-guide.md), [日本語](docs/jpn/integration-guide.md) |

## Quick Start: ESP32-P4

Use the ESP32-P4 example if you want the shortest path to a visible demo. It combines:

- `MIPI CSI` camera video input
- `I2C` control for the IMX500 module
- `SPI` metadata and tensor payload access
- `MIPI DSI` LCD output

```bash
git clone --recursive https://github.com/ArduCAM/imx500-mcu-sdk.git
cd imx500-mcu-sdk
git submodule update --init --recursive
```

Then follow the board wiring and project-specific notes in
[examples/platform/esp/esp32p4/README.md](examples/platform/esp/esp32p4/README.md).

## Port To A New MCU

Most ports follow the same sequence:

1. Add `imx500_mcu_sdk.cmake` to your CMake project.
2. Add `${IMX500_MCU_SDK_SRC_FILES}` to your target.
3. Apply `imx500_mcu_sdk_apply_config(your_target)`.
4. Implement platform `I2C`, `SPI`, delay, and optional log callbacks.
5. Register callbacks with `register_i2c_driver(...)`, `register_spi_driver(...)`, and optionally `register_printf(...)`.
6. Call `imx500_open(...)`, `stream_on()`, `read_metadata(...)`, and `parse_metadata(...)`.
7. Convert parsed tensors into product events with your post-processing logic.

For the detailed integration manual, see [中文](docs/zho/integration-guide.md),
[English](docs/eng/integration-guide.md), or [日本語](docs/jpn/integration-guide.md).

## Metadata To Product Events

The IMX500 module produces sensor-side AI output. Your MCU firmware typically turns that
raw output into application logic:

```text
SPI metadata frame
    -> parsed IMX500 metadata
    -> output tensor descriptors
    -> host-side post-processing
    -> product event
```

Example events include `person_count = 3`, `zone_occupied = true`,
`package_detected = true`, or `gesture = wake`.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `ArducamIMX500SDK.h/.cc` | Public SDK API and core implementation |
| `ai_driver.h/.c` | Platform callback registration interfaces |
| `imx500_mcu_sdk.cmake` | SDK source collection and compile-time configuration |
| `python_bindings/` | Optional Python host tools and MicroPython user module support |
| `docs/` | API landing page and integration documentation |
| `examples/` | Platform reference projects |
| `third_party/flatbuffers/` | FlatBuffers dependency used by network-info parsing |

## Troubleshooting

- If FlatBuffers headers are missing, run `git submodule update --init --recursive`.
- If the module is not detected, check power, ground, `I2C` wiring, and the platform adapter callbacks.
- If metadata reads fail, check `SPI` pin direction carefully. Camera-side `SPI_TX` connects to MCU-side `RX`; camera-side `SPI_RX` connects to MCU-side `TX`.
- If the video stream works but AI output does not, confirm the selected metadata format, firmware/network-info loading path, and SPI data-forwarding mode.
- If you changed the MIPI output resolution, rebuild with a supported `IMX500_MCU_SDK_SENSOR_MIPI_RESOLUTION` value.

## Production Design-In Checklist

Before moving beyond evaluation, confirm the product path, model output,
optics, enclosure, firmware flow, factory test, and support expectations.

| Need | Go to |
| --- | --- |
| Freeze production readiness | [Production Design-In Checklist](docs/production/design-in-checklist.md) |
| Review lens, FOV, illumination, enclosure, or mounting | [Optical Selection](docs/production/optical-selection.md) |
| Build a repeatable station or EOL test | [EOL Test](docs/production/eol-test.md) |
| Decide when to contact Arducam | [Support Options](docs/production/support-options.md) |

## Journey Map: From First Signal To Production

```text
First signal
    |
    v
USB validation
    |
    v
Choose a product path
    |-- USB3 UVC deployment
    |-- MIPI / Raspberry Pi / CM5 product
    `-- SPI / MCU low-power product
    |
    v
Model validation
    |
    v
Prototype validation
    |
    v
Production design-in
```

Path details:

- [USB Validation To USB3 UVC Deployment](docs/paths/usb-validation-to-uvc.md)
- [MIPI / Raspberry Pi / CM5 Product](docs/paths/mipi-rpi-product-path.md)
- [SPI Metadata To MCU Product](docs/paths/spi-mcu-product-path.md)
- [Model Validation To Production](docs/paths/model-validation-to-production.md)
- [Train And Deploy A Custom Model](docs/missions/06-train-and-deploy-custom-model.md)

| Stage | What you get | Arducam can help with |
| --- | --- | --- |
| First signal | Camera detected, SDK opens the module, metadata frame received | Bring-up support and debug workflow |
| Model validation | Known model or custom model result on IMX500 | Model conversion, porting, and post-processing |
| Prototype | Product event output in your application | Optics, firmware, metadata parsing, and interface review |
| Production | Stable hardware/software package and test flow | [Design-in, factory test, customization, SLA, and long-term supply](docs/production/support-options.md) |

## License

See the repository license terms before using this SDK in a product.

## Need Help?

For module information and production design-in discussions, start from the
[B0642 IMX500 AI camera module](https://www.arducam.com/arducam-imx500-ai-camera-module-for-esp32-p4-and-other-mcu-soc.html)
product page or the [support options](docs/production/support-options.md).
