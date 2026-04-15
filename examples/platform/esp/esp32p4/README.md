# ESP32-P4 to IMX500 Camera and LCD Wiring

`examples/platform/esp/esp32p4/ai_camera_multitask` uses the ESP32-P4 Function EV Board with:

- `MIPI CSI` for the camera video stream
- `I2C` for IMX500 camera control
- `SPI` for IMX500 metadata / register data path
- `MIPI DSI` for the LCD

The same wiring assumptions are baked into the current `ai_camera_multitask` code.

## 1. Read the Camera Header First

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

## 2. Required Connections for `ai_camera_multitask`

This project needs three physical connections:

1. Connect the camera FPC cable to the board `MIPI CSI` connector.
2. Connect the camera 8-pin header to the ESP32-P4 GPIOs listed below.
3. Connect the LCD FPC cable to the board `MIPI DSI` connector, and add one extra reset wire: `LCD RST -> GPIO27`.

Without the MIPI CSI cable, the video stream will not work.

Without the 8-pin header wiring, IMX500 control and SPI data access will not work.

## 3. Recommended IMX500 Header Wiring

Use the following mapping between the camera board and ESP32-P4:

| Camera pad | Camera signal | ESP32-P4 connection | ESP32-P4 function |
| --- | --- | --- | --- |
| `8` | `I2C SDA` | `GPIO7` | `I2C SDA` |
| `1` | `I2C SCL` | `GPIO8` | `I2C SCL` |
| `5` | `SPI_SCK_3v3` | `GPIO5` | `SPI SCK` |
| `4` | `SPI_RX_3v3` | `GPIO3` | `SPI MOSI / TX` |
| `3` | `SPI_TX_3v3` | `GPIO2` | `SPI MISO / RX` |
| `2` | `SPI_CS_3v3` | `GPIO4` | `SPI CS` |
| `7` | `+3v3` | `3V3` | `3.3 V power` |
| `6` | `DGND` | `GND` | `Ground` |

## 4. Why `SPI_TX` and `SPI_RX` Can Be Confusing

This is the part that most easily gets wired wrong.

The camera board labels `SPI_TX` and `SPI_RX` from the camera side. The ESP32-P4 SPI controller labels `MOSI/TX` and `MISO/RX` from the MCU side. Because of that:

- ESP32-P4 `TX / MOSI` must connect to camera `RX`
- ESP32-P4 `RX / MISO` must connect to camera `TX`

So the correct SPI cross-connection is:

- `GPIO3 (ESP32-P4 SPI MOSI / TX)` -> `camera pad 4 (SPI_RX_3v3)`
- `GPIO2 (ESP32-P4 SPI MISO / RX)` <- `camera pad 3 (SPI_TX_3v3)`

If you connect `TX -> TX` and `RX -> RX`, SPI communication will fail.

## 5. LCD Wiring

The LCD used by this example is expected to connect through the board `MIPI DSI` interface.

In addition to the LCD FPC cable, add this extra reset line:

| LCD signal | ESP32-P4 connection |
| --- | --- |
| `LCD RST` | `GPIO27` |

Important:

- The current code uses `GPIO27` as the LCD reset pin.
- If your LCD reset line is connected somewhere else, update the LCD pin definition in the project before building.

## 6. Wiring Diagram

```text
ESP32-P4 Function EV Board         IMX500 camera board
---------------------------------------------------------------
GPIO7   (I2C SDA)              ->  Pad 8  (I2C SDA)
GPIO8   (I2C SCL)              ->  Pad 1  (I2C SCL)
GPIO5   (SPI SCK)              ->  Pad 5  (SPI_SCK_3v3)
GPIO3   (SPI MOSI / TX)        ->  Pad 4  (SPI_RX_3v3)
GPIO2   (SPI MISO / RX)        <-  Pad 3  (SPI_TX_3v3)
GPIO4   (SPI CS)               ->  Pad 2  (SPI_CS_3v3)
3V3                            ->  Pad 7  (+3v3)
GND                            ->  Pad 6  (DGND)

ESP32-P4 Function EV Board         LCD panel
---------------------------------------------------------------
MIPI DSI connector             <-> LCD FPC
GPIO27                         ->  LCD RST

ESP32-P4 Function EV Board         IMX500 camera module
---------------------------------------------------------------
MIPI CSI connector             <-> Camera FPC
```

## 7. Wiring Checklist

Before powering the board:

1. Connect `GND` first.
2. Connect `3V3` to the camera `+3v3` pin.
3. Connect the camera FPC to the board `MIPI CSI` connector.
4. Connect the LCD FPC to the board `MIPI DSI` connector.
5. Connect the two I2C lines: `GPIO7 -> SDA` and `GPIO8 -> SCL`.
6. Connect the four SPI lines: `SCK`, `TX -> RX`, `RX -> TX`, and `CS`.
7. Add the extra LCD reset wire: `LCD RST -> GPIO27`.
8. Double-check that the camera header uses `3.3 V`. Do not feed `5 V` into the camera signal pins.
9. Power the board only after the wiring is complete.

## 8. Troubleshooting

- If the camera is not detected, first check the `MIPI CSI` cable, `GND`, `3V3`, `SDA`, and `SCL`.
- If the module powers up but IMX500 communication fails, check `CS`, `SCK`, and whether `SPI_TX` / `SPI_RX` were crossed correctly.
- If the LCD stays dark, check the `MIPI DSI` cable and confirm `LCD RST` really goes to `GPIO27`.
- If you changed any pins, update the corresponding source definitions before building.

## 9. Pin Definitions Used by the Current Code

The current `ai_camera_multitask` code uses:

```c
// Camera SCCB / I2C
#define EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN 8
#define EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN 7

// IMX500 SPI
static const int s_imx500_spi_sck_pin  = 5;
static const int s_imx500_spi_mosi_pin = 3;
static const int s_imx500_spi_miso_pin = 2;
static const int s_imx500_spi_cs_pin   = 4;

// LCD reset
#define EXAMPLE_PIN_NUM_LCD_RST 27
```

These definitions are consistent with:

- `examples/platform/esp/esp32p4/ai_camera_multitask/components/example_video_common/include/boards/esp32-p4-function-ev-board-v1.4/example_video_common_board.h`
- `examples/platform/esp/esp32p4/ai_camera_multitask/main/peripherals_adapter.c`
- `examples/platform/esp/esp32p4/ai_camera_multitask/components/lcd/app_lcd.c`
