# Pico2 IMX500 Production Test

This demo provides a simple production-test flow for Pico2 + IMX500.

- The Pico2 firmware uses the `integration_test` direct boot approach.
- The board boots directly into command-wait mode and does not block waiting for a USB console attach.
- On `RUN`, it reads module information, loads the bundled model, starts streaming, reads the first metadata frame, and checks whether the first byte is `0x01`.
- The board prints a fixed result marker so a host Python script can decide pass/fail.
- An optional compile-time macro can enable BOOT-button-triggered repeated testing with builtin LED indication, without power cycling the Pico board.
- After each test run, the firmware reboots the board so the next run can start from a clean state.

## 1. Build and flash the Pico2 firmware

```bash
cd examples/platform/rpi/pico2/production_test
mkdir build
cd build
cmake ..
cmake --build .
```

Flash `imx500_production_test.uf2` to Pico2.

To enable BOOT-button-triggered reusable test mode with builtin LED indication:

```bash
cmake .. -DPRODUCTION_TEST_BOOT_TRIGGER_MODE=ON
cmake --build .
```

Mode summary:

- `OFF` (default): existing behavior is preserved. The board reboots after each `RUN`.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=OFF` (default): existing behavior is preserved. The board reboots after each `RUN`.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: the board does not reboot after a test. Press the Pico `BOOT` button to start each test cycle.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: builtin LED behavior is `solid on = PASS`, `blinking = FAIL`, `off = waiting/running`.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: repeated BOOT presses are ignored while a test is already running.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: after one test finishes, you can keep Pico powered, replace the camera if needed, then press `BOOT` again for the next cycle.
- `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`: module handshake/probe is still used in the background. If the camera is unplugged, the board clears the latched PASS/FAIL state and resets back to `READY`.

## 2. Install host dependencies

```bash
pip install pyserial
```

## 3. Run the host-side production test

```bash
python host_production_test.py
```

For BOOT-button-triggered mode monitoring:

```bash
python host_production_test.py --boot-trigger-monitor
```

If auto-detection is ambiguous:

```bash
python host_production_test.py --list-ports
python host_production_test.py --port COM7
```

## 4. Serial protocol

The board prints:

- `TEST_STATUS: READY` when it is ready for a host command
- `TEST_STATUS: RUNNING` after `RUN`
- `TEST_RESULT: PASS` or `TEST_RESULT: FAIL` when the test completes

Supported host commands:

- `RUN`
- `PING`
- `LED_OFF` when `PRODUCTION_TEST_BOOT_TRIGGER_MODE=ON`

The host script actively sends `PING` while probing the board, so it does not depend on catching the initial boot banner.
In BOOT-trigger mode, the Pico firmware waits for a `BOOT` button press before each test cycle. It also probes the module state in the background, so unplugging the camera resets the board back to a clean `READY` state. Use `--boot-trigger-monitor` if you want the PC to stay connected and print each cycle result while the operator triggers tests from the board.
If you still see `TEST_STATUS: BUSY`, the board is likely running an older firmware image and needs to be reflashed or manually reset once.

## 5. Pass criteria

The production test passes only when:

1. `open(...)` succeeds in direct boot mode
2. `stream_on()` starts successfully
3. the first metadata frame is read successfully
4. the first byte of that frame is `0x01`

The board prints `TEST_REASON: ...` to help diagnose failures.
