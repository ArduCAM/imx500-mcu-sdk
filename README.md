# IMX500 MCU SDK

<img src="https://www.arducam.com/wp-content/uploads/2023/07/logo-4.png" width="100%" />

Bring sensor-side AI vision to MCU and low-cost SoC products.

`imx500-mcu-sdk` helps MCU applications control Arducam IMX500 camera modules, start
inference streams, and consume AI metadata or tensors over a portable `I2C` + `SPI`
interface. Use it when your product needs vision events from the sensor, not a full
Linux vision pipeline.

## Start Here

| Goal | Go to |
| --- | --- |
| Run the fastest display demo | [ESP32-P4 example](examples/platform/esp/esp32p4/README.md) |
| Wire a Raspberry Pi Pico 2 | [Pico 2 wiring guide](examples/platform/rpi/pico2/README.md) |
| Stream IMX500 metadata over USB serial | [Pico 2 serial stream example](examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md) |
| Integrate the SDK into your own MCU project | Integration guide: [中文](docs/zho/integration-guide.md), [English](docs/eng/integration-guide.md), [日本語](docs/jpn/integration-guide.md) |
| Read the public API reference | [Doxygen API docs](https://arducam.github.io/imx500-mcu-sdk/) |

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
- Optional data-injection and SPI forwarding modes for validation workflows
- A platform adapter pattern for bringing up new MCU boards

## Supported Platforms

| Platform | Status | Entry point |
| --- | --- | --- |
| ESP32-P4 Function EV Board | Reference display and metadata demo | [examples/platform/esp/esp32p4](examples/platform/esp/esp32p4/README.md) |
| Raspberry Pi Pico 2 | Wiring, serial metadata stream, production test examples | [examples/platform/rpi/pico2](examples/platform/rpi/pico2/README.md) |
| Raspberry Pi Pico W | Person-detection ROI MVP and SPI receive test examples | [examples/platform/rpi/pico_w](examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md) |
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

Before publishing a production-facing quick start, verify and document the exact
`ESP-IDF` version, build command, flash command, monitor baud rate, expected boot log,
and expected metadata output for the target hardware bundle.

## Port To A New MCU

Most ports follow the same sequence:

1. Add `imx500_mcu_sdk.cmake` and `imx500_firmware.cmake` to your CMake project.
2. Add `${IMX500_MCU_SDK_SRC_FILES}` and `${IMX500_FIRMWARE_CPP_FILES}` to your target.
3. Apply `imx500_mcu_sdk_apply_config(your_target)`.
4. Implement platform `I2C`, `SPI`, delay, and optional log callbacks.
5. Register callbacks with `register_i2c_driver(...)`, `register_spi_driver(...)`, and optionally `register_printf(...)`.
6. Call `open(...)`, `stream_on()`, `read_metadata(...)`, and `parse_metadata(...)`.
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
| `imx500_firmware.cmake` | Generated firmware and network-info source collection |
| `imx500_firmware_cpp/imx500_firmware/` | Generated firmware and network-info blobs |
| `python_bindings/` | Optional Python host tools for quick USB bridge validation |
| `docs/` | API landing page and integration documentation |
| `examples/` | Platform reference projects |
| `third_party/flatbuffers/` | FlatBuffers dependency used by network-info parsing |

## Quick USB Validation

If you want to quickly validate a camera module from a PC before integrating
the SDK into firmware, the optional Python host tools can exercise the USB
bridge, `imx500_mcu_sdk.open()`, metadata reads, JPEG preview, and model
flashing flows.

See [`python_bindings/README.md`](python_bindings/README.md) for firmware
version requirements, USB connection steps, Python dependencies, and tool usage.

## Troubleshooting

- If FlatBuffers headers are missing, run `git submodule update --init --recursive`.
- If the module is not detected, check power, ground, `I2C` wiring, and the platform adapter callbacks.
- If metadata reads fail, check `SPI` pin direction carefully. Camera-side `SPI_TX` connects to MCU-side `RX`; camera-side `SPI_RX` connects to MCU-side `TX`.
- If the video stream works but AI output does not, confirm the selected metadata format, firmware/network-info loading path, and SPI data-forwarding mode.
- If you changed the MIPI output resolution, rebuild with a supported `IMX500_MCU_SDK_SENSOR_MIPI_RESOLUTION` value.

## Production Design-In Checklist

Before moving beyond evaluation, confirm:

- Target MCU, memory budget, SPI bandwidth, and MIPI video path
- Camera module, lens, illumination, and mechanical mounting
- Model, network-info version, metadata format, and post-processing path
- Boot mode, firmware loading strategy, and flash programming flow
- Factory test requirements and expected serial/log output
- Failure recovery behavior for camera detect, SPI transfer, and metadata parse errors
- Volume purchasing, optical customization, and support expectations

## Roadmap

This repository is designed to grow across more MCU and low-cost SoC platforms. The
current focus is a reliable SDK core, reference platform examples, clear wiring guides,
and repeatable metadata parsing flows.

## License

See the repository license terms before using this SDK in a product.

## Need Help?

For module information and production design-in discussions, start from the Arducam IMX500
product page: [B0642 IMX500 AI camera module](https://www.arducam.com/arducam-imx500-ai-camera-module-for-esp32-p4-and-other-mcu-soc.html).
