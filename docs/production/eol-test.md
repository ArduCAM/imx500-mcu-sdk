# EOL Test

Use this page to turn a working prototype into a repeatable end-of-line test.
The test should be simple enough for a production station and strict enough to
catch hardware, model, stream, metadata, and parser failures.

## Minimum Test Flow

| Step | Pass condition |
| --- | --- |
| Detect module | Module responds over the expected control path. |
| Read version/status | Firmware/status can be read and logged. |
| Load or confirm model | Model and `network_info` are present and match the expected build. |
| Start stream | `imx500_open()` and `stream_on()` complete. |
| Read metadata | At least one complete metadata frame is received. |
| Parse output | Metadata parser returns the expected network/tensor descriptors. |
| Emit marker | Fixture prints a stable `PASS` or `FAIL` result marker. |

## Reference Example

For Pico 2, start from:

- [Pico 2 production test](../../examples/platform/rpi/pico2/production_test/README.md)

That example checks `imx500_open(...)`, `stream_on()`, first metadata frame read, and a
fixed metadata byte condition before printing `TEST_RESULT: PASS` or
`TEST_RESULT: FAIL`.

## What To Log

- Product or fixture serial number.
- Camera/module identifier if available.
- Firmware/status version.
- Model or `network_info` identifier.
- Test result marker and failure reason.
- Metadata frame size and parser result.

## When To Contact Arducam

Contact Arducam when you need a station flow, EOL pass/fail criteria, expected
serial output, recovery behavior, or production support.

Production test support: https://www.arducam.com/blog/your-reliable-oem-odm/

Design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

