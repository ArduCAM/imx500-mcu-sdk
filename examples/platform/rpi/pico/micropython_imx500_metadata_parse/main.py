import time

import imx500_mcu_sdk as imx500


I2C_BAUDRATE_HZ = 100_000
SPI_BAUDRATE_HZ = 5_000_000
METADATA_BUFFER_SIZE = 64 * 1024
FRAME_COUNT = 10
SPI_FORMAT = imx500.SPI_METADATA_OUTPUT_TENSOR


def _dims_text(tensor):
    dims = tensor.get("dimensions", ())
    if not dims:
        return "[]"
    return "[" + " x ".join(str(dim["size"]) for dim in dims) + "]"


def _preview_hex(preview):
    if not preview:
        return ""
    return " ".join("%02x" % b for b in preview)


def print_tensor(kind, index, tensor):
    print(
        "    %s[%d] id=%s name=%s dims=%s elements=%d bytes=%d off=%d"
        % (
            kind,
            index,
            tensor.get("id"),
            tensor.get("name"),
            _dims_text(tensor),
            tensor.get("element_count", 0),
            tensor.get("data_bytes", 0),
            tensor.get("data_offset", 0),
        )
    )
    preview = tensor.get("preview")
    if preview:
        print("      preview:", _preview_hex(preview))


def print_parsed_metadata(parsed):
    header = parsed.get("primary_header") or {}
    print(
        "  header frame=%s valid=%s ap=%s network=%s"
        % (
            header.get("frame_count"),
            header.get("valid_flag"),
            header.get("size_of_ap_parameter"),
            header.get("network_ordinal"),
        )
    )
    print(
        "  raw=%d ap=[%d,%d) output=[%d,+%d)"
        % (
            parsed.get("raw_bytes", 0),
            parsed.get("ap_param_offset", 0),
            parsed.get("ap_param_end_offset", 0),
            parsed.get("output_payload_offset", 0),
            parsed.get("output_payload_length", 0),
        )
    )
    print(
        "  networks=%d selected=%d"
        % (
            parsed.get("network_count", 0),
            parsed.get("selected_network_index", 0),
        )
    )

    for net_index, network in enumerate(parsed.get("networks", ())):
        print(
            "  network[%d] id=%s name=%s type=%s"
            % (
                net_index,
                network.get("id"),
                network.get("name"),
                network.get("type"),
            )
        )
        for index, tensor in enumerate(network.get("input_tensors", ())):
            print_tensor("input", index, tensor)
        for index, tensor in enumerate(network.get("output_tensors", ())):
            print_tensor("output", index, tensor)


def main():
    print("IMX500 MicroPython metadata parse example")

    imx500.init(i2c_baudrate=I2C_BAUDRATE_HZ, spi_baudrate=SPI_BAUDRATE_HZ)

    print("module fw version:", hex(imx500.get_fw_ver()))
    print("module pid:", hex(imx500.get_pid()))

    ok, device_id, boot_status = imx500.probe()
    print("probe:", ok, "device_id:", hex(device_id), "boot_status:", hex(boot_status))

    if not imx500.open_flash(spi_format=SPI_FORMAT, fps=10):
        raise RuntimeError("imx500 open_flash() failed")

    imx500.stream_on()

    buf = bytearray(METADATA_BUFFER_SIZE)
    parsed_frames = 0
    attempts = 0
    max_attempts = FRAME_COUNT * 20

    while parsed_frames < FRAME_COUNT and attempts < max_attempts:
        attempts += 1
        n = imx500.read_metadata_into(buf)
        if n <= 0:
            print("attempt", attempts, "no metadata frame")
            time.sleep_ms(10)
            continue

        parsed = imx500.parse_metadata(buf, length=n, spi_format=SPI_FORMAT, preview_len=16)
        if parsed is None:
            print("attempt", attempts, "parse_metadata failed bytes:", n)
            time.sleep_ms(10)
            continue

        parsed_frames += 1
        print("\n=== parsed frame %d/%d bytes=%d ===" % (parsed_frames, FRAME_COUNT, n))
        print_parsed_metadata(parsed)
        time.sleep_ms(100)

    print("done parsed=%d attempts=%d" % (parsed_frames, attempts))


main()
