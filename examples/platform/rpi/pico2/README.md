# Mission: Bring Up IMX500 Metadata On Pico 2

All Pico 2 examples in this directory use the same hardware interface to the IMX500 camera module:

- `I2C0` for control
- `SPI0` for data

The same pin mapping is used by:

- `integration_test`
- `spi_receive_integration_test`
- `camera_serial_stream_multitask`
- `inference_fps_benchmark`
- `production_test`

## Goal

Wire Pico 2 to the IMX500 module once, then reuse the same `I2C0` + `SPI0`
interface across serial streaming, integration, and production-test examples.

## Hardware

- Raspberry Pi Pico 2.
- Arducam IMX500 camera module.
- 8-pin IMX500 header wires.
- USB cable for Pico 2 power, flashing, and serial output.

## Run

After wiring, choose the mission that matches your goal:

| Mission | Start with |
| --- | --- |
| Run the low-level SDK integration test | [integration_test](integration_test) |
| Validate native C++ SPI metadata reads | [spi_receive_integration_test](spi_receive_integration_test) |
| Stream metadata to a PC for parsing | [camera_serial_stream_multitask](camera_serial_stream_multitask/README.md) |
| Benchmark inference FPS without retaining payloads | [inference_fps_benchmark](inference_fps_benchmark/README.md) |
| Run a repeatable production check | [production_test](production_test/README.md) |
| Port the same interface to your own MCU product | [SPI metadata to MCU product path](../../../../docs/paths/spi-mcu-product-path.md) |

## Expected Feedback

You should see:

- Pico 2 firmware boots and prints serial output.
- IMX500 control succeeds over `I2C0`.
- Metadata can be read over `SPI0` in the selected example.

## You Passed This Mission When

- The wiring checklist below is complete.
- The selected Pico 2 example can start the IMX500 stream.
- At least one metadata frame is read or forwarded.

## If It Fails

- If the camera is not detected, first check `GND`, `3V3`, `SDA`, `SCL`, and `CS`.
- If the module powers up but data transfer fails, check whether `SPI_TX` and `SPI_RX` were crossed correctly.
- If you changed the wiring, update the corresponding `g_config.h` file in the example you are building.
- The examples expect both `I2C` and `SPI` to be connected. I2C alone is not enough.

## Next Unlock

- Parse metadata into model output with [camera_serial_stream_multitask](camera_serial_stream_multitask/README.md).
- Turn metadata into product events with the [SPI metadata path](../../../../docs/paths/spi-mcu-product-path.md).
- Validate a model with the [model validation mission](../../../../docs/paths/model-validation-to-production.md).

## Wiring Details

### 1. Read the Camera Header First

![](../../../pics/B0642_connector.png)
Based on the hardware image, the camera board exposes an 8-pin header. In the image orientation, the pins appear in this order from left to right:

| Camera pad | Signal on camera board |
| --- | --- |
| `8` | `I2C SDA` |
| `7` | `+3v3` |
| `6` | `DGND` |
| `5` | `SPI_SCK_3v3` |
| `4` | `SPI_RX_3v3` |
| `3` | `SPI_TX_3v3` |
| `2` | `SPI_CS_3v3` |
| `1` | `I2C SCL` |

Important:

- This left-to-right order is only valid for the same viewing direction as the image.
- If you flip the board or view it from the opposite side, the physical order will appear reversed.
- When wiring by hand, always confirm both the pad number and the signal name on the PCB silk.

### 2. Recommended Pico 2 Wiring

Use the following mapping between the camera board and Pico 2:

| Camera pad | Camera signal | Pico 2 connection | Pico 2 function |
| --- | --- | --- | --- |
| `8` | `I2C SDA` | `GPIO20` | `I2C0 SDA` |
| `1` | `I2C SCL` | `GPIO21` | `I2C0 SCL` |
| `5` | `SPI_SCK_3v3` | `GPIO18` | `SPI0 SCK` |
| `4` | `SPI_RX_3v3` | `GPIO19` | `SPI0 TX` |
| `3` | `SPI_TX_3v3` | `GPIO16` | `SPI0 RX` |
| `2` | `SPI_CS_3v3` | `GPIO17` | `SPI chip select` |
| `7` | `+3v3` | `3V3(OUT)` | `3.3 V power` |
| `6` | `DGND` | `GND` | `Ground` |

### 3. Why `SPI_TX` and `SPI_RX` Can Be Confusing

This is the part that usually causes mistakes.

The camera board silk uses `SPI_TX` and `SPI_RX` from the camera side. Pico 2 uses `SPI0 TX` and `SPI0 RX` from the MCU side. Because of that:

- Pico `TX` must connect to camera `RX`
- Pico `RX` must connect to camera `TX`

So the correct SPI cross-connection is:

- `GPIO19 (Pico SPI0 TX)` -> `camera pad 4 (SPI_RX_3v3)`
- `GPIO16 (Pico SPI0 RX)` -> `camera pad 3 (SPI_TX_3v3)`

If you instead connect `TX -> TX` and `RX -> RX`, SPI communication will not work.

### 4. Wiring Diagram

```text
Pico 2                         IMX500 camera board
-----------------------------------------------------------
GPIO20  (I2C0 SDA)        ->   Pad 8  (I2C SDA)
GPIO21  (I2C0 SCL)        ->   Pad 1  (I2C SCL)
GPIO18  (SPI0 SCK)        ->   Pad 5  (SPI_SCK_3v3)
GPIO19  (SPI0 TX / MOSI)  ->   Pad 4  (SPI_RX_3v3)
GPIO16  (SPI0 RX / MISO)  <-   Pad 3  (SPI_TX_3v3)
GPIO17  (SPI CS)          ->   Pad 2  (SPI_CS_3v3)
3V3(OUT)                  ->   Pad 7  (+3v3)
GND                       ->   Pad 6  (DGND)
```

### 5. Wiring Checklist

Before powering the board:

1. Connect `GND` first.
2. Connect `3V3(OUT)` to the camera `+3v3` pin.
3. Connect the two I2C lines: `GPIO20 -> SDA` and `GPIO21 -> SCL`.
4. Connect the four SPI lines: `SCK`, `TX -> RX`, `RX -> TX`, and `CS`.
5. Double-check that `3.3 V` is used. Do not feed `5 V` into the camera signal pins.
6. Power Pico 2 through USB only after the wiring is complete.

### 6. Troubleshooting

- If the camera is not detected, first check `GND`, `3V3`, `SDA`, `SCL`, and `CS`.
- If the module powers up but data transfer fails, check whether `SPI_TX` and `SPI_RX` were crossed correctly.
- If you changed the wiring, update the corresponding `g_config.h` file in the example you are building.
- The examples expect both `I2C` and `SPI` to be connected. I2C alone is not enough.

### 7. Pin Definitions Used by the Current Code

The current Pico 2 examples all use:

```c
#define I2C_SDA_PIN 20
#define I2C_SCL_PIN 21
#define SPI_SCK_PIN 18
#define SPI_TX_PIN 19
#define SPI_RX_PIN 16
#define SPI_CSN_PIN 17
```

These definitions are consistent across:

- `examples/platform/rpi/pico2/integration_test/g_config.h`
- `examples/platform/rpi/pico2/spi_receive_integration_test/g_config.h`
- `examples/platform/rpi/pico2/camera_serial_stream_multitask/g_config.h`
- `examples/platform/rpi/pico2/inference_fps_benchmark/g_config.h`
- `examples/platform/rpi/pico2/production_test/g_config.h`
