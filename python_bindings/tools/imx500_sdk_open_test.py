#!/usr/bin/env python3
"""Exercise the IMX500 MCU SDK open() path over the USB CDC bridge."""

from __future__ import annotations

import argparse
import pathlib
import sys
from datetime import datetime


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if PYTHON_DIR.exists():
    sys.path.insert(0, str(PYTHON_DIR))

try:
    import imx500_mcu_sdk
except ImportError as exc:
    raise SystemExit(
        "Failed to import imx500_mcu_sdk. Build or install the binding first:\n"
        "  python3 setup.py build_ext --inplace\n"
        "or:\n"
        "  python3 -m pip install -e . --no-build-isolation"
    ) from exc


MIPI_FORMATS = {
    "image": imx500_mcu_sdk.MipiDataFormat.IMAGE,
    "metadata": imx500_mcu_sdk.MipiDataFormat.METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
    "image-metadata": imx500_mcu_sdk.MipiDataFormat.IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
    "none": imx500_mcu_sdk.MipiDataFormat.NONE,
}

SPI_FORMATS = {
    "output": imx500_mcu_sdk.SpiDataFormat.METADATA_OUTPUT_TENSOR,
    "input": imx500_mcu_sdk.SpiDataFormat.METADATA_INPUT_TENSOR,
    "jpeg": imx500_mcu_sdk.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR,
    "input-output": imx500_mcu_sdk.SpiDataFormat.METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
    "jpeg-output": imx500_mcu_sdk.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
    "none": imx500_mcu_sdk.SpiDataFormat.METADATA_NONE,
}


def read_optional_file(path: pathlib.Path | None) -> bytes | None:
    if path is None:
        return None
    data = path.read_bytes()
    print(f"loaded {path}: {len(data)} bytes", flush=True)
    return data


def save_metadata_frame(data: bytes, output_dir: pathlib.Path, index: int) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = output_dir / f"metadata_{timestamp}_{index:03d}.bin"
    path.write_bytes(data)
    return path


def metadata_read_size(max_bytes: int) -> int:
    if max_bytes > 0:
        return max_bytes
    size = imx500_mcu_sdk.get_metadata_size()
    if size <= 0:
        raise RuntimeError(
            "metadata size is 0; start the stream first or pass --metadata-max-bytes"
        )
    return size


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB bridge CDC serial port")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument(
        "--mipi-format",
        choices=sorted(MIPI_FORMATS),
        default="image",
        help="MIPI output format passed to imx500_mcu_sdk.open()",
    )
    parser.add_argument(
        "--spi-format",
        choices=sorted(SPI_FORMATS),
        default="output",
        help="SPI metadata format passed to imx500_mcu_sdk.open()",
    )
    parser.add_argument(
        "--model",
        type=pathlib.Path,
        help="Network weights/model blob for direct boot. Requires --network-info.",
    )
    parser.add_argument(
        "--network-info",
        type=pathlib.Path,
        help="network_info payload for direct boot. Requires --model.",
    )
    parser.add_argument(
        "--stream-on",
        action="store_true",
        help="Call imx500_mcu_sdk.stream_on() after imx500_mcu_sdk.open() succeeds.",
    )
    parser.add_argument(
        "--metadata-frames",
        type=int,
        default=0,
        help="Read this many metadata frames after open. This waits until frames are ready.",
    )
    parser.add_argument(
        "--metadata-max-bytes",
        type=int,
        default=0,
        help="Metadata read buffer capacity. 0 means use SDK metadata size.",
    )
    parser.add_argument(
        "--metadata-output-dir",
        type=pathlib.Path,
        default=pathlib.Path("metadata_frames"),
    )
    parser.add_argument(
        "--no-probe",
        action="store_true",
        help="Skip imx500_mcu_sdk.probe_imx500_module() before open.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if (args.model is None) != (args.network_info is None):
        raise SystemExit("--model and --network-info must be passed together")

    direct_boot = args.model is not None
    model = read_optional_file(args.model)
    network_info = read_optional_file(args.network_info)

    print("opening USB bridge...", flush=True)
    bridge = imx500_mcu_sdk.connect_usb_bridge(
        args.port,
        baudrate=args.baudrate,
        timeout=args.timeout,
    )

    try:
        print(f"bridge port: {bridge.port}", flush=True)
        print(f"ping: {bridge.ping()}", flush=True)
        print(f"status: {bridge.status()}", flush=True)

        if not args.no_probe:
            ok, device_id, boot_status = imx500_mcu_sdk.probe_imx500_module()
            print(
                "probe: "
                f"ok={ok} device_id=0x{device_id:08x} boot_status={boot_status}",
                flush=True,
            )

        boot_name = "direct model/network_info" if direct_boot else "flash model/network_info"
        print(
            "calling imx500_mcu_sdk.open(): "
            f"boot={boot_name}, mipi={args.mipi_format}, spi={args.spi_format}, "
            f"fps={args.fps}",
            flush=True,
        )
        opened = imx500_mcu_sdk.open(
            model,
            network_info,
            MIPI_FORMATS[args.mipi_format],
            SPI_FORMATS[args.spi_format],
            args.fps,
        )
        print(f"imx500_mcu_sdk.open result: {opened}", flush=True)
        if not opened:
            return 2

        print(f"metadata size: {imx500_mcu_sdk.get_metadata_size()}", flush=True)

        if args.stream_on:
            print("calling imx500_mcu_sdk.stream_on()", flush=True)
            imx500_mcu_sdk.stream_on()

        for index in range(args.metadata_frames):
            print(f"reading metadata frame {index + 1}/{args.metadata_frames}", flush=True)
            read_size = metadata_read_size(args.metadata_max_bytes)
            buffer = bytearray(read_size)
            n = imx500_mcu_sdk.read_metadata(buffer)
            print(f"metadata frame size: {n}", flush=True)
            if n <= 0:
                raise RuntimeError(f"read_metadata(buffer[{read_size}]) returned {n}")
            frame = bytes(memoryview(buffer)[:n])
            path = save_metadata_frame(frame, args.metadata_output_dir, index)
            print(f"saved {path}", flush=True)

    finally:
        bridge.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
