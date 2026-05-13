# Support Options

Use this page to decide when to involve Arducam during evaluation,
prototyping, and production design-in.

## Support By Blocker

| Blocker | Start with | Contact route |
| --- | --- | --- |
| Camera is not detected | Check power, ground, `I2C`, reset, and wiring guides | [Design-in support](https://www.arducam.com/blog/contact-arducam/) |
| USB validation fails | Run `python_bindings/tools/imx500_first_run.py` and capture checkpoint output | [USB/B0566 support](https://www.arducam.com/arducam-uvc-ai-camera-module-powered-by-imx500.html) |
| SPI-specific camera product is needed | Review target host, SPI interface, power budget, lens/FOV, firmware flow, and production-test needs | [SPI AI camera specialized support](https://www.arducam.com/arducam-imx500-mcu-ai-camera-module.html) |
| Model cannot be converted | Review model task, operators, quantization, input shape, and post-processing | [Custom model support](https://ai.arducam.com/) |
| Accuracy drops in real scene | Review data, lens, FOV, illumination, enclosure, and mounting | [Optical support](https://www.arducam.com/) |
| Product needs Linux packaging | Review MIPI/RPi/CM5 path, networking, enclosure, and UI needs | [MIPI/RPi/CM5 support](https://www.arducam.com/embedded-camera-module/cameras-for-raspberrypi/raspberry-pi-ai-camera.html) |

## What To Prepare Before Contact

- Product path: USB, MIPI/Linux, or SPI/MCU.
- Hardware: module, host board, wiring, lens, enclosure, and power details.
- Firmware/software version and exact command used.
- Model package, `network_info`, task type, and post-processing path.
- Logs showing the last passing checkpoint and the first failing checkpoint.
- Real-scene images or videos if the issue is accuracy or optics.
- Production volume, schedule, customization, and test-flow needs.

## Back To Production Checklist

Return to the [Production Design-In Checklist](design-in-checklist.md).
