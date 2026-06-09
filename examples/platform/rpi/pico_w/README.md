# Mission: Build IMX500 Event Output On Pico W

All Pico W examples in this directory use the same hardware interface to the
IMX500 camera module:

- `I2C0` for control
- `SPI0` for metadata, JPEG, or tensor data

The same pin mapping is used by:

- `imx500_person_detect_roi_mvp`
- `spi_receive_integration_test`

## Goal

Wire Pico W to the IMX500 module once, then choose either a product-like
person-detection ROI demo or a lower-level SPI receive integration test.

## Hardware

- Raspberry Pi Pico W.
- Arducam IMX500 camera module.
- 8-pin IMX500 header wires.
- USB cable for Pico W power, flashing, and serial output.
- Wi-Fi network for the person-detection ROI web UI.
- Optional GP0/GP1 output wiring for warning, relay, LED, or test-fixture input.

## Run

After wiring, choose the mission that matches your goal:

| Mission | Start with |
| --- | --- |
| Build a product-like person-detection ROI web demo | [imx500_person_detect_roi_mvp](imx500_person_detect_roi_mvp/README.md) |
| Validate SPI metadata receive behavior | [spi_receive_integration_test](spi_receive_integration_test) |
| Turn metadata into an MCU product event | [SPI metadata to MCU product path](../../../../docs/paths/spi-mcu-product-path.md) |

## Expected Feedback

You should see:

- Pico W firmware boots and prints serial output over USB.
- IMX500 control succeeds over `I2C0`.
- Metadata can be read over `SPI0` in the selected example.
- For the ROI demo, Wi-Fi connects and the board prints an HTTP URL.
- For the ROI demo, GP0/GP1 switch when `person_count > 0`.

## You Passed This Mission When

- The wiring checklist below is complete.
- The selected Pico W example can start the IMX500 stream.
- At least one metadata frame is read or parsed.
- For the ROI demo, the browser UI loads and `/status.json` reports person
  count, ROI, threshold, and GPIO state.

## If It Fails

- If the camera is not detected, first check `GND`, `3V3`, `SDA`, `SCL`, and `CS`.
- If the module powers up but data transfer fails, check whether `SPI_TX` and
  `SPI_RX` were crossed correctly.
- If the ROI demo cannot join Wi-Fi, check `.env`, `WIFI_COUNTRY`, SSID,
  password, and network reachability.
- If metadata parsing fails, confirm the expected model and `network_info.txt`
  are already deployed in module Flash.
- If you changed the wiring, update the corresponding `g_config.h` file in the
  example you are building.
- The examples expect both `I2C` and `SPI` to be connected. I2C alone is not enough.

## Next Unlock

- Build the browser-based [person-detection ROI MVP](imx500_person_detect_roi_mvp/README.md).
- Turn metadata into product events with the [SPI metadata path](../../../../docs/paths/spi-mcu-product-path.md).
- Validate or replace a model with the [model validation mission](../../../../docs/paths/model-validation-to-production.md).
- Move toward production with the [design-in checklist](../../../../docs/production/design-in-checklist.md).

## Wiring Details

### 1. Read the Camera Header First

![](../../../pics/B0642_connector.png)

Based on the hardware image, the camera board exposes an 8-pin header. In the
image orientation, the pins appear in this order from left to right:

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

### 2. Recommended Pico W Wiring

Use the following mapping between the camera board and Pico W:

| Camera pad | Camera signal | Pico W connection | Pico W function |
| --- | --- | --- | --- |
| `8` | `I2C SDA` | `GPIO20` | `I2C0 SDA` |
| `1` | `I2C SCL` | `GPIO21` | `I2C0 SCL` |
| `5` | `SPI_SCK_3v3` | `GPIO18` | `SPI0 SCK` |
| `4` | `SPI_RX_3v3` | `GPIO19` | `SPI0 TX` |
| `3` | `SPI_TX_3v3` | `GPIO16` | `SPI0 RX` |
| `2` | `SPI_CS_3v3` | `GPIO17` | `SPI chip select` |
| `7` | `+3v3` | `3V3(OUT)` | `3.3 V power` |
| `6` | `DGND` | `GND` | Ground |

The person-detection ROI demo also uses:

| Pico W pin | Purpose |
| --- | --- |
| `GPIO0` | Warning LED / output 0 |
| `GPIO1` | Buzzer / output 1 |

### 3. Why `SPI_TX` and `SPI_RX` Can Be Confusing

The camera board silk uses `SPI_TX` and `SPI_RX` from the camera side. Pico W
uses `SPI0 TX` and `SPI0 RX` from the MCU side. Because of that:

- Pico `TX` must connect to camera `RX`
- Pico `RX` must connect to camera `TX`

So the correct SPI cross-connection is:

- `GPIO19 (Pico SPI0 TX)` -> `camera pad 4 (SPI_RX_3v3)`
- `GPIO16 (Pico SPI0 RX)` -> `camera pad 3 (SPI_TX_3v3)`

If you instead connect `TX -> TX` and `RX -> RX`, SPI communication will not work.

### 4. Wiring Diagram

```text
Pico W                         IMX500 camera board
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
5. For the ROI demo, connect GP0/GP1 only to 3.3 V-compatible downstream inputs.
6. Double-check that `3.3 V` is used. Do not feed `5 V` into the camera signal pins.
7. Power Pico W through USB only after the wiring is complete.

### 6. Pin Definitions Used by the Current Code

The current Pico W examples use:

```c
#define I2C_SDA_PIN 20
#define I2C_SCL_PIN 21
#define SPI_SCK_PIN 18
#define SPI_TX_PIN 19
#define SPI_RX_PIN 16
#define SPI_CSN_PIN 17
```

The person-detection ROI demo additionally uses:

```c
#define WARN_LED_PIN 0
#define BUZZER_PIN 1
```

These definitions are consistent across:

- `examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/g_config.h`
- `examples/platform/rpi/pico_w/spi_receive_integration_test/g_config.h`
