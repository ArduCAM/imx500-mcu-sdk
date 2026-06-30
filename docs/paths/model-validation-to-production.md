# Path: Model Validation To Production

Use this path when you need to prove that an IMX500 model fits your application
before committing to USB, MIPI/Linux, or SPI/MCU productization.

The goal is not only to run a model. The goal is to turn model output into a
validated product decision: continue with an existing model, adapt the
post-processing, convert a custom model, or ask for model porting support.

## Best For

- Teams choosing between known IMX500 models and custom models.
- Application validation before hardware integration.
- Comparing model output across USB validation, Linux-hosted products, and MCU
  metadata consumption.
- Projects that need accuracy, latency, lighting, FOV, or post-processing review
  before production design-in.

## Mission Chain

| Step | Goal | Feedback | Next |
| --- | --- | --- | --- |
| Try a known model | Understand what IMX500 can do | Demo output appears or metadata can be parsed | Choose an application |
| Flash model to device | Run on real hardware | Metadata frames match the expected model output | Prototype validation |
| Convert your own model | Bring a custom model to IMX500 | Converted package is accepted | Model porting / optimization |
| Validate in real scene | Check accuracy, latency, lighting, and FOV | Pass/fail report for the target scene | Production design-in |

## Start Here

Choose the mission that matches your current model stage:

| Current stage | Start with |
| --- | --- |
| You want to try a known model | [Mission 04: Model Zoo Validation](../missions/04-model-zoo-validation.md) |
| You already have a model package for the device | [USB validation path](usb-validation-to-uvc.md) and `python_bindings/tools/imx500_usb_flash.py` |
| You need to bring your own model | [Mission 05: Custom Model Conversion](../missions/05-custom-model-conversion.md) |
| You need to train, package, and deploy your own model | [Mission 06: Train And Deploy A Custom Model](../missions/06-train-and-deploy-custom-model.md) |

Model zoo entry: [https://github.com/ArduCAM/arducam_imx500_model_zoo](https://github.com/ArduCAM/arducam_imx500_model_zoo)

Model conversion docs: [https://www.arducam.com/arducam-ai-model-converter-tutorial](https://www.arducam.com/arducam-ai-model-converter-tutorial)

## Success Checkpoints

You are ready to move from model validation to product validation when:

- The selected model task matches the application requirement.
- A known sample or real hardware run produces expected parsed output.
- The model package can run on real IMX500 hardware.
- Metadata frames match the expected tensor layout and post-processing path.
- Real-scene testing produces an acceptable pass/fail result for accuracy,
  latency, lighting, and FOV.

## Next Unlock

| If model validation shows... | Continue with... |
| --- | --- |
| PC/Linux deployment is enough | [USB3 UVC deployment path](usb-validation-to-uvc.md) |
| You need Linux UI, preview, networking, or packaging | [MIPI / Raspberry Pi / CM5 product path](mipi-rpi-product-path.md) |
| You only need AI events on an MCU | [SPI metadata to MCU product path](spi-mcu-product-path.md) |
| You need packaged application assets | [Application packs](../../examples/README.md)|
| Conversion or accuracy is blocked | [Custom model support](https://ai.arducam.com/) |

## When To Contact Arducam

Contact Arducam when:

- Your model cannot be converted or packaged for IMX500.
- Accuracy drops after moving from sample data to real scenes.
- Metadata is readable but post-processing does not match your application.
- Your scene needs lens, FOV, illumination, enclosure, or mounting review.
- You need a recommendation for USB, MIPI/Linux, or SPI/MCU productization.

For optical and scene validation, see
[Optical Selection](../production/optical-selection.md).

For production readiness, see the
[Production Design-In Checklist](../production/design-in-checklist.md).

For contact preparation and support routing, see
[Support Options](../production/support-options.md).

Production and design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

## Back To README

Return to the [IMX500 MCU SDK mission map](../../README.md).
