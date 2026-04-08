# Example Video Common Component

This component provides board-level initialization for `esp_video` with a MIPI-CSI RAW sensor. It is designed for ESP32-P4 Function EV Board examples.

## Supported Boards and GPIO Pins

| Hardware | ESP32-P4-Function-EV-Board V1.4 | ESP32-P4-Function-EV-Board V1.5 |
|:-:|:-:|:-:|
| MIPI-CSI I2C SCL Pin        | 8  | 8  |
| MIPI-CSI I2C SDA Pin        | 7  | 7  |
| MIPI-CSI Reset Pin          | NA | NA |
| MIPI-CSI Power-down Pin     | NA | NA |
| MIPI-CSI XCLK Pin           | NA | NA |

Notes:
- This component only keeps the MIPI-CSI RAW sensor path.

## Usage Instructions

### MIPI-CSI RAW Sensor

```
Example Video Initialization Configuration  --->
    Select Target Development Board (ESP32-P4-Function-EV-Board V1.5)
        ( ) ESP32-P4-Function-EV-Board V1.4
        (X) ESP32-P4-Function-EV-Board V1.5

    MIPI RAW Sensor Configuration  --->
        (0) SCCB(I2C) Port Number
        (100000) SCCB(I2C) Frequency (100K-400K Hz)
        [*] Enable Motor Control
        [ ] MIPI-CSI Video Device Don't Initialize LDO
```

Notes:
- The component always initializes the MIPI-CSI RAW sensor path.
- SCCB(I2C) is always initialized by `example_video_common`; the pre-initialized SCCB-by-app path has been removed.
- JPEG encoding is fixed to the hardware JPEG driver.
