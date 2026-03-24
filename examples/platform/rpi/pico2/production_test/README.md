# Pico2 IMX500 Production Test

This demo provides a simple production-test flow for Pico2 + IMX500.

- The Pico2 firmware uses the `integration_test` direct boot approach.
- The board boots directly into command-wait mode and does not block waiting for a USB console attach.
- On `RUN`, it reads module information, loads the bundled model, starts streaming, reads the first metadata frame, and checks whether the first byte is `0x01`.
- The board prints a fixed result marker so a host Python script can decide pass/fail.
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

## 2. Install host dependencies

```bash
pip install pyserial
```

## 3. Run the host-side production test

```bash
python host_production_test.py
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

The host script actively sends `PING` while probing the board, so it does not depend on catching the initial boot banner.
If you still see `TEST_STATUS: BUSY`, the board is likely running an older firmware image and needs to be reflashed or manually reset once.

## 5. Pass criteria

The production test passes only when:

1. `open(...)` succeeds in direct boot mode
2. `stream_on()` starts successfully
3. the first metadata frame is read successfully
4. the first byte of that frame is `0x01`

The board prints `TEST_REASON: ...` to help diagnose failures.
