# Production Design-In Checklist

Use this checklist after the first signal, model validation, and prototype
mission pass. The goal is to freeze the parts of the product that affect
hardware, firmware, model output, test flow, and supply.

## Readiness Checklist

| Area | Confirm before production |
| --- | --- |
| Product path | USB3 UVC, MIPI/Linux, or SPI/MCU path is selected. |
| Host platform | Target MCU, Linux host, memory budget, and boot flow are known. |
| Interfaces | `I2C`, `SPI`, MIPI, USB, power, reset, and mechanical constraints are confirmed. |
| Model | Model package, `network_info`, metadata format, tensor layout, and post-processing are fixed. |
| Optics | Lens, FOV, focus distance, illumination, enclosure window, and mounting are validated. |
| Firmware | `imx500_open()`, `stream_on()`, metadata read, parse, retry, and recovery behavior are defined. |
| Application output | Product events, thresholds, ROI, labels, and fail-safe behavior are specified. |
| Factory test | Test command, pass/fail marker, expected logs, and station flow are documented. |
| Supply | Volume plan, customization needs, SLA, and long-term supply expectations are known. |

## Stage Gate

You are ready for production design-in when:

- The selected mission has a repeatable pass condition.
- The selected product path has a known hardware and software owner.
- The model output is validated in representative scenes.
- The production test can catch camera detect, model load, stream, metadata, and
  parsing failures.
- Open support questions are collected before hardware or enclosure freeze.

## Related Production Docs

- [Optical Selection](optical-selection.md)
- [EOL Test](eol-test.md)
- [Support Options](support-options.md)

## When To Contact Arducam

Contact Arducam before design freeze when you need:

- Interface or bandwidth review.
- Model conversion, model porting, or post-processing review.
- Lens, FOV, illumination, enclosure, or mechanical review.
- Factory test, EOL test, SLA, customization, or long-term supply support.

Design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

