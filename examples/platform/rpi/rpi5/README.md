# Mission: Use B0642 camera module On RPi5 (experimental)

![](pics/hardware_connection.png)

## Hardware Connection

This RPi5 example uses two physical paths to the IMX500 camera module:

- `MIPI CSI`: camera image stream from the module to Raspberry Pi 5.
- `I2C` + `SPI`: IMX500 control and metadata access through the 8-pin camera
  header.

Both paths are required if you want to preview the image and run the
`spi_receive_integration_test` metadata example. The
`i2c_payload_flash_test` example uses the I2C lines inside the MIPI CSI/FPC
camera connector, so it does not require the extra 8-pin header I2C/SPI wiring.

### MIPI CSI Connection

Power off the Raspberry Pi 5 before connecting or removing the camera cable.

Connect the IMX500 camera module FPC cable to one of the Raspberry Pi 5 MIPI
camera connectors. The photo above shows the camera connected to the connector
near the board edge.

Recommended checks:

1. Use a Raspberry Pi 5 compatible camera FPC cable. Raspberry Pi 5 uses the
   small 22-pin MIPI connector on the board side.
2. Fully insert the FPC cable into the Raspberry Pi 5 MIPI connector, then lock
   the latch.
3. Fully insert the other end into the camera module connector, then lock the
   latch.
4. Keep the metal contacts aligned with the connector contacts. If `rpicam-still`
   cannot find the camera, power off and check the cable orientation on both
   ends.
5. Do not hot-plug the MIPI cable while the Raspberry Pi 5 is powered.

The MIPI cable carries the image stream only. The 8-pin header wiring in
[GPIO Wiring](#gpio-wiring) is still needed for the SDK metadata path.

### 8-Pin Header Connection

Connect the camera 8-pin header to the Raspberry Pi 5 40-pin header as listed
in [GPIO Wiring](#gpio-wiring). This provides:

- `I2C1` control on GPIO2/GPIO3.
- `SPI0 CE0` metadata transfer on GPIO8/GPIO9/GPIO10/GPIO11.
- `3.3 V` power and common `GND`.

Do not connect any camera signal pin to `5 V`.

The I2C-only payload direct-load test does not use this 8-pin header wiring;
it talks through the MIPI CSI/FPC connector I2C bus instead.

## Get Image

Download the Installation Script
```
sudo apt update
sudo apt install wget -y
wget -O install_pivariety_pkgs.sh https://github.com/ArduCAM/Arducam-Pivariety-V4L2-Driver/releases/download/install_script/install_pivariety_pkgs.sh
chmod +x install_pivariety_pkgs.sh
```
Install Arducam libcamera Software
```
./install_pivariety_pkgs.sh -p libcamera_dev
./install_pivariety_pkgs.sh -p libcamera_apps
```

Open the configuration file:
```
sudo nano /boot/firmware/config.txt
```

Disable camera auto-detection:
```
camera_auto_detect=0
```
Add arducam-pivariety overlay under the [all] section:
```
dtoverlay=arducam-pivariety
```
Save and reboot:
```
sudo reboot
```
Get frame
```
rpicam-still -t 0 --tuning-file /usr/share/libcamera/ipa/rpi/pisp/imx500.json
```

If the camera is not detected, power off the Raspberry Pi 5 and re-check the
MIPI FPC cable orientation, insertion depth, and connector latch before changing
software settings.

![](pics/image_preview.png)

## Get Metadata

See the RPi5 pinout at [https://pinout.xyz/](https://pinout.xyz/).

The `spi_receive_integration_test` project uses the Raspberry Pi 5 40-pin header
through Linux `i2c-dev` and `spidev`:

- I2C control: `/dev/i2c-1`, GPIO2/GPIO3, physical pins `3`/`5`
- SPI metadata: `/dev/spidev0.0`, SPI0 CE0
- SPI mode: `3`, 8-bit words, 5 MHz

### Enable I2C and SPI

Open the configuration file:

```sh
sudo nano /boot/firmware/config.txt
```

Make sure these lines are present:

```text
dtparam=i2c_arm=on
dtparam=spi=on
```

Save and reboot:

```sh
sudo reboot
```

After reboot, check that the device nodes exist:

```sh
ls /dev/i2c-1 /dev/spidev0.0
```

### GPIO Wiring

Based on the Raspberry Pi 5 pinout from pinout.xyz, use SPI0 CE0 on the 40-pin
header.

| Camera pad | Camera signal | RPi5 BCM GPIO | RPi5 physical pin | RPi5 function |
| --- | --- | --- | --- | --- |
| `8` | `I2C SDA` | `GPIO2` | `3` | `I2C1 SDA` |
| `1` | `I2C SCL` | `GPIO3` | `5` | `I2C1 SCL` |
| `5` | `SPI_SCK_3v3` | `GPIO11` | `23` | `SPI0 SCLK` |
| `4` | `SPI_RX_3v3` | `GPIO10` | `19` | `SPI0 MOSI` |
| `3` | `SPI_TX_3v3` | `GPIO9` | `21` | `SPI0 MISO` |
| `2` | `SPI_CS_3v3` | `GPIO8` | `24` | `SPI0 CE0` |
| `7` | `+3v3` | `3V3` | `1` or `17` | `3.3 V power` |
| `6` | `DGND` | `GND` | `6`, `9`, `14`, `20`, `25`, `30`, `34`, or `39` | `Ground` |

The camera board labels `SPI_TX` and `SPI_RX` from the camera side, so the SPI
data lines must be crossed:

- RPi5 `GPIO10 / MOSI` -> camera pad `4 / SPI_RX_3v3`
- RPi5 `GPIO9 / MISO` <- camera pad `3 / SPI_TX_3v3`

Do not connect any camera signal pin to `5 V`. Use `3.3 V` power and common
ground only.

```text
Raspberry Pi 5 40-pin header      IMX500 camera board
--------------------------------------------------------------
Pin 3   GPIO2  (I2C1 SDA)    ->   Pad 8  (I2C SDA)
Pin 5   GPIO3  (I2C1 SCL)    ->   Pad 1  (I2C SCL)
Pin 23  GPIO11 (SPI0 SCLK)   ->   Pad 5  (SPI_SCK_3v3)
Pin 19  GPIO10 (SPI0 MOSI)   ->   Pad 4  (SPI_RX_3v3)
Pin 21  GPIO9  (SPI0 MISO)   <-   Pad 3  (SPI_TX_3v3)
Pin 24  GPIO8  (SPI0 CE0)    ->   Pad 2  (SPI_CS_3v3)
Pin 1   3V3                  ->   Pad 7  (+3v3)
Pin 6   GND                  ->   Pad 6  (DGND)
```

### Build and Run

Install the build tools:

```sh
sudo apt update
sudo apt install cmake g++ -y
```

Build the RPi5 metadata test:

```sh
cd examples/platform/rpi/rpi5/spi_receive_integration_test
cmake -S . -B build
cmake --build build -j
```

To test with a slower SPI clock, configure it at build time:

```sh
cmake -S . -B build -DINTEGRATION_TEST_SPI_BAUDRATE_HZ=1000000
cmake --build build -j
```

Run it on the Raspberry Pi 5:

```sh
sudo ./build/spi_receive_integration_test
```

Build the I2C-only payload direct-load test:

```sh
cd examples/platform/rpi/rpi5/i2c_payload_flash_test
cmake -S . -B build
cmake --build build -j
```

This example embeds the bundled HigherHRNet model by default:

- `tools/assets/models/higherhrnet/network.fpk`
- `tools/assets/models/higherhrnet/network_info.txt`

By default this test auto-scans common I2C device candidates such as
`/dev/i2c-10`, `/dev/i2c-11`, and `/dev/i2c-1`.
If your Raspberry Pi OS exposes the camera connector on a different bus, set it
explicitly:

```sh
cmake -S . -B build -DI2C_PAYLOAD_I2C_DEVICE=/dev/i2c-X
cmake --build build -j
```

Run individual I2C payload direct-load operations:

```sh
sudo ./build/i2c_payload_flash_test reset
sudo ./build/i2c_payload_flash_test model-direct
sudo ./build/i2c_payload_flash_test nninfo-direct
sudo ./build/i2c_payload_flash_test all-direct
```

The default action is `all-direct`. Other useful actions are `status` and
`reset`. I2C payload no longer writes model or network-info blobs to module
flash; use the SPI or USB flashing path for persistent model/network-info
updates. The `model-direct` and `all-direct` actions run the SDK reset-only
path before sending the model over I2C.

The default settings live in
`examples/platform/rpi/rpi5/i2c_payload_flash_test/g_config.h`.
Change `I2C_PAYLOAD_I2C_DEVICE` or `I2C_PAYLOAD_I2C_CANDIDATES` there if your
Pi exposes the camera I2C bus with different device names.
