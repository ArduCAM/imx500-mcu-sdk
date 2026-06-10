import gc
import time

import imx500_mcu_sdk as imx500


METADATA_BUFFER_SIZE = 48 * 1024
DATA_READ_TIMEOUT_MS = 1000
SPI_FORMAT = imx500.SpiDataFormat.METADATA_OUTPUT_TENSOR
I2C_BAUDRATE = 100000
SPI_BAUDRATE = 4000000
FPS = 10
PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME = True


def _dims_text(tensor):
    dims = tensor.get("dimensions", ())
    if not dims:
        return "[]"
    return "[" + " x ".join(str(dim.get("size")) for dim in dims) + "]"


def _preview_hex(preview):
    if not preview:
        return ""
    return " ".join("%02x" % value for value in preview)


def _print_tensor_summary(parsed):
    print("raw bytes:", parsed.get("raw_bytes"))
    print("ap params:", parsed.get("ap_param_offset"), parsed.get("ap_param_size"))
    print("output payload:", parsed.get("output_payload_offset"), parsed.get("output_payload_length"))
    for index, network in enumerate(parsed.get("networks", ())):
        print("network[%d]:" % index, network.get("name"), network.get("type"))
        for tensor in network.get("input_tensors", ()):
            print("  input:", tensor.get("name"), _dims_text(tensor))
        for tensor in network.get("output_tensors", ()):
            print(
                "  output:",
                tensor.get("name"),
                _dims_text(tensor),
                "offset=",
                tensor.get("data_offset"),
                "bytes=",
                tensor.get("data_bytes"),
                "preview=",
                _preview_hex(tensor.get("preview")),
            )


def _wait_for_metadata(buffer, timeout_ms):
    start = time.ticks_ms()
    attempts = 0
    while time.ticks_diff(time.ticks_ms(), start) < timeout_ms:
        attempts += 1
        size = imx500.read_metadata(buffer)
        if size > 0:
            return size, attempts
        time.sleep_ms(10)
    return 0, attempts


def main():
    print("IMX500 nRF52840 DK MicroPython metadata parser")
    print("free heap before init:", gc.mem_free())
    imx500.init(i2c_baudrate=I2C_BAUDRATE, spi_baudrate=SPI_BAUDRATE, settle_ms=100)

    fw_version = imx500.get_fw_ver()
    pid = imx500.get_pid()
    probe_ok, device_id, boot_status = imx500.probe_imx500_module()
    print("fw=0x%08x pid=0x%08x probe=%s device=0x%08x boot=0x%08x" % (
        fw_version,
        pid,
        probe_ok,
        device_id,
        boot_status,
    ))
    if not probe_ok:
        raise RuntimeError("IMX500 probe failed")

    if not imx500.open(spi_format=SPI_FORMAT, fps=FPS):
        raise RuntimeError("IMX500 open failed")
    if not imx500.stream_on():
        raise RuntimeError("IMX500 stream_on failed")

    buffer = bytearray(METADATA_BUFFER_SIZE)
    print("metadata buffer:", len(buffer), "free heap:", gc.mem_free())

    frame_index = 0
    summary_done = False
    while True:
        size, attempts = _wait_for_metadata(buffer, DATA_READ_TIMEOUT_MS)
        if size <= 0:
            print("metadata timeout attempts=", attempts, "free=", gc.mem_free())
            continue

        try:
            parsed = imx500.parse_metadata(buffer, length=size, spi_format=SPI_FORMAT, preview_len=8)
        except UnicodeError:
            print("skip noisy metadata frame bytes=", size)
            continue

        if parsed is None:
            print("parse failed bytes=", size)
            continue

        header = parsed.get("primary_header") or parsed.get("output_header") or {}
        print(
            "frame",
            frame_index,
            "bytes=",
            size,
            "imx_frame=",
            header.get("frame_count"),
            "attempts=",
            attempts,
            "free=",
            gc.mem_free(),
        )
        if PRINT_TENSOR_SUMMARY_ON_FIRST_FRAME and not summary_done:
            _print_tensor_summary(parsed)
            summary_done = True
        frame_index += 1


main()
