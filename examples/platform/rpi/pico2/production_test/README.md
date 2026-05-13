# Mission: Run A Repeatable Pico 2 Production Test

This demo provides a simple production-test flow for Pico 2 + IMX500.

## Goal

Run one host-triggered `RUN` command that checks module information, loads the
bundled model, starts streaming, reads the first metadata frame, and prints a
fixed pass/fail result marker.

## Hardware

- Raspberry Pi Pico 2 wired to the IMX500 module.
- Arducam IMX500 camera module.
- USB cable from Pico 2 to the host PC.

Use the shared [Pico 2 wiring guide](../README.md) before building this mission.

## Run

Build and flash the Pico 2 firmware:

```bash
cd examples/platform/rpi/pico2/production_test
mkdir build
cd build
cmake ..
cmake --build .
```

Flash `imx500_production_test.uf2` to Pico 2.

Install host dependencies:

```bash
pip install pyserial
```

Run the host-side production test:

```bash
python host_production_test.py
```

If auto-detection is ambiguous:

```bash
python host_production_test.py --list-ports
python host_production_test.py --port COM7
```

## Expected Feedback

The board prints:

- `TEST_STATUS: READY` when it is ready for a host command.
- `TEST_STATUS: RUNNING` after `RUN`.
- `TEST_RESULT: PASS` or `TEST_RESULT: FAIL` when the test completes.
- `TEST_REASON: ...` when it needs to explain a failure.

The host script actively sends `PING` while probing the board, so it does not
depend on catching the initial boot banner.

## You Passed This Mission When

The production test passes only when:

1. `open(...)` succeeds in direct boot mode.
2. `stream_on()` starts successfully.
3. The first metadata frame is read successfully.
4. The first byte of that frame is `0x01`.

## If It Fails

- If you see `TEST_STATUS: BUSY`, the board is likely running an older firmware image and needs to be reflashed or manually reset once.
- If the host cannot find a port, run `--list-ports` and pass `--port`.
- If `open(...)` fails, confirm wiring, power, and model/network-info bundling.
- If metadata fails, check SPI pin direction and metadata buffer assumptions.

## Reusable BOOT-Button Mode

To enable BOOT-button-triggered reusable test mode with builtin LED indication:

```bash
cmake .. -DPRODUCTION_TEST_BOOT_TRIGGER_MODE=ON
cmake --build .
```

Monitor repeated BOOT-button cycles:

```bash
python host_production_test.py --boot-trigger-monitor
```

Mode summary:

- `OFF` or `PRODUCTION_TEST_BOOT_TRIGGER_MODE=OFF`: the board reboots after each `RUN`.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: the board does not reboot after a test.
- Press the Pico `BOOT` button to start each test cycle.
- Builtin LED behavior is `solid on = PASS`, `blinking = FAIL`, `off = waiting/running`.
- Repeated BOOT presses are ignored while a test is already running.
- After one test finishes, you can keep Pico powered, replace the camera if needed, then press `BOOT` again for the next cycle.
- If the camera is unplugged, the board clears the latched PASS/FAIL state and resets back to `READY`.

## Serial Protocol

Supported host commands:

- `RUN`
- `PING`
- `LED_OFF` when `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`

## Next Unlock

- Turn this into a factory station flow with [production support](https://www.arducam.com/blog/your-reliable-oem-odm/).
- Review the [production design-in checklist](../../../../../docs/production/design-in-checklist.md).
- Use the [EOL test guide](../../../../../docs/production/eol-test.md) to define station logs and pass/fail markers.
- Continue productization through the [SPI metadata path](../../../../../docs/paths/spi-mcu-product-path.md).
