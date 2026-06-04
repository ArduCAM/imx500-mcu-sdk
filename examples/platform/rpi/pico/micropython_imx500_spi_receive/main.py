import time

import imx500_mcu_sdk as imx500


I2C_BAUDRATE_HZ = 100_000
SPI_BAUDRATE_HZ = 5_000_000
METADATA_BUFFER_SIZE = 64 * 1024


def main():
    print("IMX500 MicroPython SPI receive example")

    imx500.init(i2c_baudrate=I2C_BAUDRATE_HZ, spi_baudrate=SPI_BAUDRATE_HZ)

    fw_ver = imx500.get_fw_ver()
    pid = imx500.get_pid()
    print("module fw version:", hex(fw_ver))
    print("module pid:", hex(pid))

    ok, device_id, boot_status = imx500.probe()
    print("probe:", ok, "device_id:", hex(device_id), "boot_status:", hex(boot_status))

    if not imx500.open_flash(fps=10):
        raise RuntimeError("imx500 open_flash() failed")

    imx500.stream_on()

    buf = bytearray(METADATA_BUFFER_SIZE)
    frame_count = 0

    while True:
        n = imx500.read_metadata_into(buf)
        if n > 0:
            frame_count += 1
            print("frame", frame_count, "metadata bytes:", n, "head:", bytes(buf[: min(n, 12)]))
        else:
            print("no metadata frame")
        time.sleep_ms(100)


main()

