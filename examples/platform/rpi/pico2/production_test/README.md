# Pico2 IMX500 Production Test

This demo provides a simple production-test flow for Pico2 + IMX500.

- The Pico2 firmware uses the `integration_test` direct boot approach.
- The board boots directly into command-wait mode and does not block waiting for a USB console attach.
- On `RUN`, it reads module information, loads the bundled model, starts streaming, reads the first metadata frame, and checks whether the first byte is `0x01`.
- The board prints a fixed result marker so a host Python script can decide pass/fail.
- An optional compile-time macro can enable builtin-LED indication and continuous testing without power cycling the Pico board.
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

To enable continuous test mode with builtin LED indication:

```bash
cmake .. -DPRODUCTION_TEST_CONTINUOUS_LED_MODE=ON
cmake --build .
```

Mode summary:

- `OFF` (default): existing behavior is preserved. The board reboots after each `RUN`.
- `ON`: the board does not reboot after a test. It waits for module removal, then waits for the next module insertion and automatically starts the next test cycle.
- `ON`: builtin LED behavior is `solid on = PASS`, `blinking = FAIL`, `off = waiting/running`.
- `ON`: the same camera will not be retested repeatedly while it remains connected. A new test starts only after a disconnect -> reconnect transition is detected.

## 2. Install host dependencies

```bash
pip install pyserial
```

## 3. Run the host-side production test

```bash
python host_production_test.py
```

For continuous auto-test mode monitoring:

```bash
python host_production_test.py --continuous-monitor
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
- `LED_OFF` when `PRODUCTION_TEST_CONTINUOUS_LED_MODE=ON`

The host script actively sends `PING` while probing the board, so it does not depend on catching the initial boot banner.
In continuous mode, the Pico firmware owns the test cycle and automatically waits for module insertion/removal. Use `--continuous-monitor` if you want the PC to stay connected and print each cycle result.
If you still see `TEST_STATUS: BUSY`, the board is likely running an older firmware image and needs to be reflashed or manually reset once.

## 5. Pass criteria

The production test passes only when:

1. `open(...)` succeeds in direct boot mode
2. `stream_on()` starts successfully
3. the first metadata frame is read successfully
4. the first byte of that frame is `0x01`

The board prints `TEST_REASON: ...` to help diagnose failures.
