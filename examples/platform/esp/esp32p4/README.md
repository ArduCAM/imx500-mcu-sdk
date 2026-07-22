# Mission: Run A Visible IMX500 AI Demo On ESP32-P4

`examples/platform/esp/esp32p4/ai_camera_multitask` uses the ESP32-P4 Function EV Board with:

- `MIPI CSI` for the camera video stream
- `I2C` for IMX500 camera control
- `SPI` for IMX500 metadata / register data path
- `MIPI DSI` for the LCD

The same wiring assumptions are baked into the current `ai_camera_multitask` code.

## Goal

Get the fastest MCU-hosted visual demo: MIPI camera preview on the LCD, IMX500
control over `I2C`, and AI metadata/tensor access over `SPI`.

The firmware embeds four model and network-info pairs:

| Model | Task |
| --- | --- |
| MobileNet V2 | Image classification |
| SSD MobileNetV2 FPNLite | Object detection |
| HigherHRNet | Pose estimation |
| DeepLabV3Plus | Semantic segmentation |

HigherHRNet is the initial default. The selected model index is stored in NVS,
so later boots restore the last selection.

## Hardware

- ESP32-P4 Function EV Board.
- Arducam IMX500 camera module.
- LCD panel for the board `MIPI DSI` connector.
- Camera FPC cable, LCD FPC cable, and 8-pin IMX500 header wires.

## Run

Wire the board using the checklist below, then build and flash the example:

```bash
cd examples/platform/esp/esp32p4/ai_camera_multitask
idf.py set-target esp32p4
idf.py build flash monitor
```

Press and release the board `BOOT` button (`GPIO35`) to select the next embedded
model. The demo stores the new selection and restarts automatically.

## Build Configuration

The default build uses direct model loading and output-tensor-only SPI metadata:

| CMake option | Default | Supported values |
| --- | --- | --- |
| `AI_CAMERA_MULTITASK_BOOT_MODE` | `DIRECT` | `DIRECT`, `FLASH` |
| `AI_CAMERA_MULTITASK_SPI_METADATA_MODE` | `OUTPUT_TENSOR_ONLY` | `OUTPUT_TENSOR_ONLY`, `JPEG_INPUT_OUTPUT` |

To program the selected embedded model into module flash before opening it and
to include the JPEG/input payload on SPI, reconfigure with:

```bash
idf.py -DAI_CAMERA_MULTITASK_BOOT_MODE=FLASH \
  -DAI_CAMERA_MULTITASK_SPI_METADATA_MODE=JPEG_INPUT_OUTPUT reconfigure
idf.py build flash monitor
```

`FLASH` mode programs the selected embedded model and matching network-info on
each start, then opens the stream from module flash. `DIRECT` loads that pair
directly from ESP32-P4 firmware without first persisting it to module flash.

## Expected Feedback

You should see:

- LCD video preview from the camera.
- IMX500 `I2C` and `SPI` initialization logs.
- The demo task reading IMX500 metadata or tensor payloads.
- AI overlay or parsed AI output matching the selected embedded model.
- The LCD status area showing the current model and task.

## You Passed This Mission When

- The LCD preview is visible.
- The serial monitor shows the stream has started.
- Parsed AI output or overlay output appears for at least 30 seconds.
- Pressing `BOOT` advances to the next model and restarts into that selection.
- No repeated `SPI` metadata timeout or IMX500 detect failure appears.

## If It Fails

- If the camera is not detected, check the `MIPI CSI` cable, `GND`, `3V3`, `SDA`, and `SCL`.
- If the module powers up but IMX500 communication fails, check `CS`, `SCK`, and whether `SPI_TX` / `SPI_RX` were crossed correctly.
- If the LCD stays dark, check the `MIPI DSI` cable and confirm `LCD RST` really goes to `GPIO27`.
- If the build fails, confirm ESP-IDF supports the ESP32-P4 target and that submodules are initialized.

## Next Unlock

- Convert parsed metadata into product events.
- Continue with the [SPI metadata to MCU product path](../../../../docs/paths/spi-mcu-product-path.md).
- Validate a different model with the [model validation mission](../../../../docs/paths/model-validation-to-production.md).
- Move toward production with the [design-in checklist](../../../../docs/production/design-in-checklist.md).

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

### 2. Required Connections for `ai_camera_multitask`

This project needs three physical connections:

1. Connect the camera FPC cable to the board `MIPI CSI` connector.
2. Connect the camera 8-pin header to the ESP32-P4 GPIOs listed below.
3. Connect the LCD FPC cable to the board `MIPI DSI` connector, and add one extra reset wire: `LCD RST -> GPIO27`.

Without the MIPI CSI cable, the video stream will not work.

Without the 8-pin header wiring, IMX500 control and SPI data access will not work.

### 3. Recommended IMX500 Header Wiring

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

### 4. Why `SPI_TX` and `SPI_RX` Can Be Confusing

This is the part that most easily gets wired wrong.

The camera board labels `SPI_TX` and `SPI_RX` from the camera side. The ESP32-P4 SPI controller labels `MOSI/TX` and `MISO/RX` from the MCU side. Because of that:

- ESP32-P4 `TX / MOSI` must connect to camera `RX`
- ESP32-P4 `RX / MISO` must connect to camera `TX`

So the correct SPI cross-connection is:

- `GPIO3 (ESP32-P4 SPI MOSI / TX)` -> `camera pad 4 (SPI_RX_3v3)`
- `GPIO2 (ESP32-P4 SPI MISO / RX)` <- `camera pad 3 (SPI_TX_3v3)`

If you connect `TX -> TX` and `RX -> RX`, SPI communication will fail.

### 5. LCD Wiring

The LCD used by this example is expected to connect through the board `MIPI DSI` interface.

In addition to the LCD FPC cable, add this extra reset line:

| LCD signal | ESP32-P4 connection |
| --- | --- |
| `LCD RST` | `GPIO27` |

Important:

- The current code uses `GPIO27` as the LCD reset pin.
- If your LCD reset line is connected somewhere else, update the LCD pin definition in the project before building.

### 6. Wiring Diagram

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

### 7. Wiring Checklist

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

### 8. Troubleshooting

- If the camera is not detected, first check the `MIPI CSI` cable, `GND`, `3V3`, `SDA`, and `SCL`.
- If the module powers up but IMX500 communication fails, check `CS`, `SCK`, and whether `SPI_TX` / `SPI_RX` were crossed correctly.
- If the LCD stays dark, check the `MIPI DSI` cable and confirm `LCD RST` really goes to `GPIO27`.
- If you changed any pins, update the corresponding source definitions before building.

### 9. Pin Definitions Used by the Current Code

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
