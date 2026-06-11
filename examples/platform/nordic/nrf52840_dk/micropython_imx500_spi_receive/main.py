import gc
import time

import imx500_mcu_sdk as imx500


METADATA_BUFFER_SIZE = 48 * 1024
I2C_BAUDRATE = 100000
SPI_BAUDRATE = 4000000
SPI_FORMAT = imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR
FPS = 10


def main():
    print("IMX500 nRF52840 DK MicroPython SPI receive example")
    print("free heap before init:", gc.mem_free())

    imx500.init(i2c_baudrate=I2C_BAUDRATE, spi_baudrate=SPI_BAUDRATE, settle_ms=100)

    fw_ver = imx500.get_fw_ver()
    pid = imx500.get_pid()
    print("module fw version:", hex(fw_ver))
    print("module pid:", hex(pid))

    ok, device_id, boot_status = imx500.probe_imx500_module()
    print("probe:", ok, "device_id:", hex(device_id), "boot_status:", hex(boot_status))
    if not ok:
        raise RuntimeError("IMX500 probe failed")

    if not imx500.open(spi_format=SPI_FORMAT, fps=FPS):
        raise RuntimeError("IMX500 open failed")

    imx500.stream_on()

    buf = bytearray(METADATA_BUFFER_SIZE)
    frame_count = 0
    print("metadata buffer:", len(buf), "free heap:", gc.mem_free())

    while True:
        n = imx500.read_metadata(buf)
        if n > 0:
            frame_count += 1
            print("frame", frame_count, "metadata bytes:", n, "head:", bytes(buf[: min(n, 12)]))
        else:
            print("no metadata frame", "free heap:", gc.mem_free())
        time.sleep_ms(100)


main()
