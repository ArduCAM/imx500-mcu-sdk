# IMX500 MCU SDK Integration Guide

Languages: [中文](../zho/integration-guide.md) | English | [日本語](../jpn/integration-guide.md)

This document moves the engineering integration content out of the repository root
`README.md`. The root README is the quick entry point for demos, platform paths, and
product positioning. This guide focuses on CMake integration, platform adapters,
runtime flow, and troubleshooting.

## SDK Positioning

`imx500-mcu-sdk` is a lightweight SDK for controlling Arducam IMX500 modules from MCU
platforms. It provides a portable driver interface, a C/C++ SDK API, and reference
integration code.

The repository is designed to support more MCU platforms over time. Platform-specific
notes are maintained under `examples/`.

## Core Capabilities

- `I2C` / `SPI` communication with IMX500 modules
- Unified C/C++ SDK API, with the main entry point in `ArducamIMX500SDK.h`
- Network firmware loading and stream control
- Metadata readout and parsing
- SPI data forwarding mode switching
- Platform adapter pattern for faster MCU bring-up

## Product Positioning

[B0642](https://www.arducam.com/arducam-imx500-ai-camera-module-for-esp32-p4-and-other-mcu-soc.html)
is positioned as a bridge solution for bringing `IMX500` capabilities to MCU platforms.

It is intended for:

- New MCU-based designs that need `IMX500` inference capability
- Existing SoC-based projects migrating toward MCU-based systems
- Compatibility-first integrations where `SPI` is preferred over direct metadata-over-`MIPI`

Path differences compared with Raspberry Pi AI Camera:

| Feature | Raspberry Pi AI Camera | B0642 |
| --- | --- | --- |
| MIPI output | Image + metadata (`RGB888` input tensor + output tensor) | Image only |
| Metadata path | Over `MIPI` | Over `SPI` (`JPEG` input tensor + output tensor, or output tensor only) |

## Repository Layout

| Path | Purpose |
| --- | --- |
| `ArducamIMX500SDK.h/.cc` | Public SDK API and core implementation |
| `ai_driver.h/.c` | Low-level driver abstraction and registration |
| `imx500_mcu_sdk.cmake` | Collects SDK source files and compile-time configuration |
| `examples/` | Platform reference projects |
| `third_party/flatbuffers/` | FlatBuffers dependency used by network-info parsing |

## Requirements

- CMake 3.13 or later
- A C/C++ toolchain for the target MCU
- Git, used to initialize submodules

Each platform also requires its own SDK or toolchain. For example, the ESP32-P4 example
requires ESP-IDF, while the Pico 2 examples require Raspberry Pi Pico SDK. Use the
corresponding example README as the source of truth for toolchain versions.

## Initialize Submodules

```bash
git submodule update --init --recursive
```

If the build reports missing FlatBuffers headers, the submodules are usually not fully
initialized.

## CMake Integration

Include the SDK source collection in your platform CMake project:

```cmake
include(path/to/imx500_mcu_sdk.cmake)
```

Add these source groups to your target:

- `${IMX500_MCU_SDK_SRC_FILES}`

Apply the SDK compile-time configuration to your target:

```cmake
imx500_mcu_sdk_apply_config(your_target)
```

Typical include directories:

- SDK root
- `third_party/flatbuffers/include`

## Select MIPI Output Resolution

The SDK defaults to `1024x600` MIPI output. You can also select one of the supported
resolutions with CMake:

- `1024x600`
- `1600x1200`
- `1280x720`
- `640x480`

Example:

```bash
cmake -S . -B build -DIMX500_MCU_SDK_SENSOR_MIPI_RESOLUTION=1280x720
```

`imx500_mcu_sdk.cmake` converts this setting into the corresponding sensor MIPI command,
width, and height compile definitions.

## Implement Platform Adapters

A new platform needs platform-specific `I2C`, `SPI`, delay, and optional log functions,
then registers them with the SDK.

Registration APIs:

- `register_i2c_driver(...)`
- `register_spi_driver(...)`
- `register_printf(...)`

Reference implementations:

- `examples/platform/esp/esp32p4/ai_camera_multitask/main/peripherals_adapter.c`
- `examples/platform/rpi/pico2/peripherals_adapter.c`
- `examples/platform/rpi/pico_w/peripherals_adapter.c`

During adapter bring-up, verify:

- `I2C` address, read/write timing, and error return behavior
- `SPI` TX/RX direction and chip-select behavior
- Transfer buffer lifetime for DMA or interrupt-driven implementations
- Delay behavior under your RTOS or bare-metal scheduler
- SDK log output does not block critical realtime paths

## Typical Runtime Flow

Most platforms follow this sequence:

1. Register platform callbacks with `register_i2c_driver(...)` and `register_spi_driver(...)`.
2. Optionally bind SDK logs to the platform logger with `register_printf(...)`.
3. Optionally call `probe_imx500_module(...)` for an early hardware presence check.
4. Call `open(...)` to load firmware/network info and configure data formats.
5. Call `stream_on()` to start runtime output.
6. Call `read_metadata(...)` to read an SPI metadata frame.
7. Call `parse_metadata(...)` to parse metadata and bind output tensor payloads.
8. Run application-level post-processing to convert tensors into product events.

Flow summary:

```text
register_i2c_driver / register_spi_driver
    -> probe_imx500_module
    -> open
    -> stream_on
    -> read_metadata
    -> parse_metadata
    -> postprocess
    -> application event
```

## Metadata To Application Events

The SDK boundary usually ends at raw metadata and parsed tensor descriptors. The product
application still needs model-specific post-processing.

```text
raw SPI metadata
    -> IMX500ParsedMetadata
    -> IMX500ParsedNetwork
    -> IMX500ParsedTensor
    -> post-processing
    -> event or UI overlay
```

Common application events include:

- `person_count`
- `object_detected`
- `zone_occupied`
- `classification_label`
- `pose_keypoints`
- `segmentation_mask`

## API Reference

Public API declarations are in `ArducamIMX500SDK.h`.

Online API documentation:

[https://arducam.github.io/imx500-mcu-sdk/](https://arducam.github.io/imx500-mcu-sdk/)

The API landing page is `docs/mainpage.md`, where interfaces are grouped into:

- Dequant
- ROI
- ISP
- Data Injection
- IMX500 Control

## Platform Example Entry Points

| Platform | Entry |
| --- | --- |
| ESP32-P4 | `examples/platform/esp/esp32p4/README.md` |
| Pico 2 wiring | `examples/platform/rpi/pico2/README.md` |
| Pico 2 serial stream | `examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md` |
| Pico 2 production test | `examples/platform/rpi/pico2/production_test/README.md` |
| Pico W person detect ROI MVP | `examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md` |

## Troubleshooting

- If FlatBuffers headers are missing, run `git submodule update --init --recursive`.
- If the camera is not detected, first check `GND`, `3V3`, `I2C SDA`, `I2C SCL`, and adapter callbacks.
- If video works but metadata reads fail, check `SPI_CS`, `SPI_SCK`, `SPI_TX`, and `SPI_RX`.
- Remember that `SPI_TX` / `SPI_RX` are named from the camera side. MCU `TX` connects to camera `RX`; MCU `RX` connects to camera `TX`.
- If `parse_metadata(...)` fails, verify metadata format, network-info, payload size, and buffer size.
- If you changed the MIPI resolution, make sure CMake uses a value supported by `imx500_mcu_sdk.cmake`.

## Pre-Production Checklist

Before moving into product design, confirm:

- Target MCU RAM, flash, SPI bandwidth, and MIPI video path
- Lens, field of view, lighting conditions, and mechanical mounting
- Model version, network-info version, and metadata format
- Firmware/network-info loading strategy
- Factory test flow and success/failure logs
- Recovery behavior for camera detect failures, SPI read failures, and metadata parse failures
- Commercial support, optical customization, model adaptation, and volume purchasing requirements
