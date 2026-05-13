# Path: SPI Metadata To MCU Product

Use this path when your product only needs AI results from the IMX500 module,
not a full Linux vision pipeline. This is the main productization path for
`imx500-mcu-sdk`.

## Best For

- MCU products that consume AI events instead of full video.
- Low-power, low-bandwidth, fixed-function devices.
- Products that need deterministic firmware behavior and small host resources.
- Applications where IMX500 sensor-side inference becomes product logic.

## Start Here

Start from a platform example or integration guide:

| Goal | Entry point |
| --- | --- |
| Visible MCU demo with display and metadata | [ESP32-P4 example](../../examples/platform/esp/esp32p4/README.md) |
| Pico 2 wiring and serial metadata stream | [Pico 2 examples](../../examples/platform/rpi/pico2/README.md) |
| Custom MCU board port | Integration guide: [English](../eng/integration-guide.md), [中文](../zho/integration-guide.md), [日本語](../jpn/integration-guide.md) |

## Success Checkpoints

Move through these checkpoints in order:

| Checkpoint | What should work |
| --- | --- |
| I2C control | The MCU can probe or open the IMX500 module. |
| Stream control | The MCU can call `open()` and `stream_on()` through the SDK. |
| SPI metadata | The MCU receives one complete metadata frame. |
| Metadata parsing | The SDK parses metadata into network and tensor descriptors. |
| Product event | Your firmware converts tensor output into application behavior. |

Example product events:

- `person_count = 3`
- `zone_occupied = true`
- `package_detected = true`
- `gesture = wake`

## Next Unlock

| If you need... | Continue with... |
| --- | --- |
| A SPI-specific camera product | [SPI AI camera specialized support](https://www.arducam.com/arducam-imx500-mcu-ai-camera-module.html) |
| Model or post-processing validation | [Model validation mission](model-validation-to-production.md)
| Lens, FOV, power, or enclosure review | [Optical selection](../production/optical-selection.md) |
| Factory test or production flow | [EOL test](../production/eol-test.md) 

## When To Contact Arducam

Contact Arducam when:

- I2C works but SPI metadata times out.
- Metadata arrives but parsing does not match the expected model.
- SPI bandwidth, MCU memory, or frame rate requirements are unclear.
- Your application needs custom optics, illumination, enclosure, or mechanical
  review.
- You need factory test, EOL test, customization, SLA, or long-term supply.

For production readiness, see the
[Production Design-In Checklist](../production/design-in-checklist.md).

For contact preparation and support routing, see
[Support Options](../production/support-options.md).

Production and design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

## Back To README

Return to the [IMX500 MCU SDK mission map](../../README.md).
