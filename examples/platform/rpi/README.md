# Raspberry Pi Platform Examples

Use this directory when you want to bring up IMX500 metadata on Raspberry Pi
family boards. The Pico-class examples focus on MCU firmware over `I2C` + `SPI`;
the Raspberry Pi 5 examples add Linux-hosted paths for MIPI preview, metadata
access through `i2c-dev`/`spidev`, and model asset operations over the camera
connector I2C bus.

## Choose A Raspberry Pi Mission

| Mission | Start with | Success checkpoint |
| --- | --- | --- |
| Build and run on Raspberry Pi Pico | [pico](pico/README.md) | Pico controls IMX500 over `I2C0` and receives metadata over `SPI0` |
| Use the SDK from MicroPython | [pico/micropython_imx500_metadata_parse](pico/micropython_imx500_metadata_parse/README.md) | MicroPython imports `imx500_mcu_sdk`, parses metadata, and prints detections or UART product frames |
| Smoke-test MicroPython SPI reads | [pico/micropython_imx500_spi_receive](pico/micropython_imx500_spi_receive/README.md) | MicroPython starts the stream and reads at least one metadata frame |
| Stream metadata from Pico 2 to a PC | [pico2/camera_serial_stream_multitask](pico2/camera_serial_stream_multitask/README.md) | Host receiver parses at least one streamed metadata frame |
| Benchmark Pico 2 inference delivery | [pico2/inference_fps_benchmark](pico2/inference_fps_benchmark/README.md) | Firmware reports inference FPS, payload size, and effective SPI throughput |
| Run a Pico 2 production check | [pico2/production_test](pico2/production_test/README.md) | Host script prints `TEST_RESULT: PASS` |
| Build a Pico W product-like event demo | [pico_w/imx500_person_detect_roi_mvp](pico_w/imx500_person_detect_roi_mvp/README.md) | Web UI reports person count, ROI, threshold, and GP0/GP1 state |
| Validate Raspberry Pi 5 metadata | [rpi5/spi_receive_integration_test](rpi5/README.md#choose-a-raspberry-pi-5-mission) | Raspberry Pi 5 previews over MIPI and reads metadata over Linux SPI |
| Test Raspberry Pi 5 I2C model loading and flash | [rpi5/i2c_payload_flash_test](rpi5/README.md#choose-a-raspberry-pi-5-mission) | The selected direct-load, flash-write, or flash-cycle operation completes |

## Project Index

| Path | Purpose |
| --- | --- |
| `pico/spi_receive_integration_test/` | Native Pico C++ SPI metadata receive bring-up |
| `pico/micropython_imx500_spi_receive/` | Custom RP2 MicroPython firmware with basic SDK metadata reads |
| `pico/micropython_imx500_metadata_parse/` | MicroPython metadata parser, SSD MobileNet result printing, and UART product protocol |
| `pico2/integration_test/` | Pico 2 native integration test for IMX500 control and metadata receive |
| `pico2/spi_receive_integration_test/` | Pico 2 SPI metadata receive validation |
| `pico2/camera_serial_stream_multitask/` | Pico 2 serial metadata streamer with PC-side parsing helpers |
| `pico2/inference_fps_benchmark/` | Pico 2 SPI inference-frame delivery and throughput benchmark |
| `pico2/production_test/` | Pico 2 repeatable production-test firmware and host script |
| `pico_w/spi_receive_integration_test/` | Pico W SPI metadata receive validation |
| `pico_w/imx500_person_detect_roi_mvp/` | Pico W person-detection ROI web UI with GPIO event outputs |
| `rpi5/spi_receive_integration_test/` | Experimental Raspberry Pi 5 Linux `i2c-dev`/`spidev` metadata test |
| `rpi5/i2c_payload_flash_test/` | Raspberry Pi 5 direct-load and flash operations over the camera connector I2C payload path |

## Shared Wiring Pattern

The Pico, Pico 2, and Pico W examples use the same default IMX500 8-pin header
mapping:

| Camera signal | Pico-family pin |
| --- | --- |
| `I2C SDA` | `GPIO20` / `I2C0 SDA` |
| `I2C SCL` | `GPIO21` / `I2C0 SCL` |
| `SPI_SCK_3v3` | `GPIO18` / `SPI0 SCK` |
| `SPI_RX_3v3` | `GPIO19` / `SPI0 TX` |
| `SPI_TX_3v3` | `GPIO16` / `SPI0 RX` |
| `SPI_CS_3v3` | `GPIO17` / chip select |
| `+3v3` | `3V3(OUT)` |
| `DGND` | `GND` |

The camera board labels `SPI_TX` and `SPI_RX` from the camera side, so connect
MCU `TX` to camera `RX` and MCU `RX` to camera `TX`.

For full wiring diagrams, use the board-specific guides:

- [Pico](pico/README.md)
- [Pico 2](pico2/README.md)
- [Pico W](pico_w/README.md)
- [Raspberry Pi 5](rpi5/README.md)
