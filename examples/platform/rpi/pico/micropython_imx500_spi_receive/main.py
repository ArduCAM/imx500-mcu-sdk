import time

import imx500_mcu_sdk as imx500


METADATA_BUFFER_SIZE = 64 * 1024


def main():
    print("IMX500 MicroPython SPI receive example")

    fw_ver = imx500.get_fw_ver()
    pid = imx500.get_pid()
    print("module fw version:", hex(fw_ver))
    print("module pid:", hex(pid))

    ok, device_id, boot_status = imx500.probe_imx500_module()
    print("probe:", ok, "device_id:", hex(device_id), "boot_status:", hex(boot_status))

    if not imx500.imx500_open(
        None,
        None,
        imx500.MipiDataFormat.IMAGE,
        imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR,
        10,
    ):
        raise RuntimeError("imx500_open() failed")

    imx500.stream_on()

    buf = bytearray(METADATA_BUFFER_SIZE)
    frame_count = 0

    while True:
        n = imx500.read_metadata(buf)
        if n > 0:
            frame_count += 1
            print("frame", frame_count, "metadata bytes:", n, "head:", bytes(buf[: min(n, 12)]))
        else:
            print("no metadata frame")
        time.sleep_ms(100)


main()
