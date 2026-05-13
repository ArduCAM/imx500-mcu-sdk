# Path: MIPI / Raspberry Pi / CM5 Product

Use this path when your product needs a Linux host, video preview, application
UI, network services, or a faster route toward a packaged edge vision device.

This path is an exit from the MCU-first SDK journey. The `imx500-mcu-sdk`
repository remains useful for understanding IMX500 control, metadata, and model
output behavior, while the product path moves toward host-side Linux systems.

## Best For

- Linux application development.
- Video preview, UI, streaming, or network service integration.
- Raspberry Pi or CM5 product packaging.
- PoE, Wi-Fi, GMSL, enclosure, or all-in-one deployment options.
- Teams that want a productized host platform instead of a bare MCU integration.

## Start Here

Start with a validated IMX500 signal:

```bash
cd python_bindings
PYTHONPATH=python python3 tools/imx500_first_run.py --jpeg-preview
```

Then move to the appropriate Linux-hosted product family or quick start:

- [Raspberry Pi / CM5 IMX500 product family](https://www.arducam.com/embedded-camera-module/cameras-for-raspberrypi/raspberry-pi-ai-camera.html)
- [PoE](https://www.arducam.com/all-in-one-raspberry-pi-ai-camera-kit-with-cm5-inside-poe-usb-power-compact-versatile-arducam-qb.html) / [Wi-Fi](https://www.arducam.com/arducam-all-in-one-rpi-ai-camera-with-compute-module-inside.html) / [GMSL](https://www.arducam.com/all-in-one-gmsl-rpi-ai-camera-kit.html) / [all-in-one options](https://blog.arducam.com/arducam-imx500-series-ai-vision-solution/)
- [General IMX500 product family](https://blog.arducam.com/arducam-imx500-series-ai-vision-solution/)

## Success Checkpoints

You are ready for this path when:

- The camera is detected and can stream.
- A preview or host-side image path works.
- Metadata or parsed AI output can be observed from the selected host stack.
- The product requirements need Linux services, preview, packaging, or networking.

## Next Unlock

| If you need... | Continue with... |
| --- | --- |
| Raspberry Pi or CM5 integration | [Raspberry Pi / CM5 product family](https://www.arducam.com/embedded-camera-module/cameras-for-raspberrypi/raspberry-pi-ai-camera.html) |
| Field deployment with networking or enclosure | [PoE](https://www.arducam.com/all-in-one-raspberry-pi-ai-camera-kit-with-cm5-inside-poe-usb-power-compact-versatile-arducam-qb.html) / [Wi-Fi](https://www.arducam.com/arducam-all-in-one-rpi-ai-camera-with-compute-module-inside.html) / [GMSL](https://www.arducam.com/all-in-one-gmsl-rpi-ai-camera-kit.html) / [all-in-one options](https://blog.arducam.com/arducam-imx500-series-ai-vision-solution/) |
| Model accuracy or application validation | [Model validation mission](model-validation-to-production.md)
| A product that only needs AI events | [SPI metadata to MCU product path](spi-mcu-product-path.md) |

## When To Contact Arducam

Contact Arducam when:

- You need help choosing between USB, MIPI/Linux, and SPI/MCU paths.
- Your application needs a specific lens, FOV, enclosure, cable length, or network
  interface.
- You need help packaging a validated prototype into a deployable product.
- You need long-term supply, customization, SLA, or production test support.

For lens, FOV, illumination, enclosure, and mounting review, see
[Optical Selection](../production/optical-selection.md).

For contact preparation and support routing, see
[Support Options](../production/support-options.md).

Production and design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

## Back To README

Return to the [IMX500 MCU SDK mission map](../../README.md).
