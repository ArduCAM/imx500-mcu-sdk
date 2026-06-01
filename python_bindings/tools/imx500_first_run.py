#!/usr/bin/env python3
"""Run a checkpoint-style first validation over the IMX500 USB bridge."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Callable


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


IMX500_HEADER_LEN = 12
JPEG_ALIGNMENT = 1024
JPEG_MIN_SIZE_BYTES = 4096
METADATA_SIZE_POLL_INTERVAL_SEC = 0.05

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


@dataclass
class FirstRunState:
    bridge: object | None = None
    metadata_frame: bytes = b""
    metadata_path: pathlib.Path | None = None
    jpeg_path: pathlib.Path | None = None


@dataclass(frozen=True)
class Checkpoint:
    label: str
    action: Callable[[], str]


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def calculate_jpeg_aligned_size(jpeg_size: int) -> int:
    return max(JPEG_MIN_SIZE_BYTES, align_up(jpeg_size + 3, JPEG_ALIGNMENT))


def trim_jpeg_bytes(data: bytes) -> bytes:
    soi = data.find(b"\xff\xd8")
    eoi = data.rfind(b"\xff\xd9")
    if soi >= 0 and eoi > soi:
        return data[soi : eoi + 2]
    return b""


def extract_jpeg_from_metadata(frame: bytes) -> tuple[bytes, str]:
    if len(frame) >= IMX500_HEADER_LEN:
        try:
            (
                valid_flag,
                frame_count,
                _max_line_len,
                ap_param_size,
                network_ordinal,
                _indicator,
            ) = struct.unpack_from("<BBHHHB", frame, 0)
            ap_end = IMX500_HEADER_LEN + ap_param_size
            if ap_end + 4 <= len(frame):
                jpeg_size = struct.unpack_from("<I", frame, ap_end)[0]
                jpeg_start = ap_end + 4
                jpeg_end = jpeg_start + calculate_jpeg_aligned_size(jpeg_size) - 4
                if jpeg_start <= len(frame) and jpeg_start < jpeg_end:
                    jpeg_block = frame[jpeg_start : min(jpeg_end, len(frame))]
                    jpeg = trim_jpeg_bytes(jpeg_block)
                    if jpeg:
                        info = (
                            f"frame={frame_count} net={network_ordinal} "
                            f"valid={valid_flag} jpeg_size={jpeg_size}"
                        )
                        return jpeg, info
        except struct.error:
            pass

    jpeg = trim_jpeg_bytes(frame)
    if jpeg:
        return jpeg, "fallback SOI/EOI scan"
    return b"", "no JPEG SOI/EOI marker found"


def read_optional_file(path: pathlib.Path | None, label: str) -> bytes | None:
    if path is None:
        return None
    data = path.read_bytes()
    if not data:
        raise RuntimeError(f"{label} file is empty: {path}")
    return data


def save_bytes(data: bytes, output_dir: pathlib.Path, prefix: str, suffix: str) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = output_dir / f"{prefix}_{timestamp}{suffix}"
    path.write_bytes(data)
    return path


def bridge_status_summary(response: object) -> str:
    return (
        f"boot_state={getattr(response, 'value0', 0)} "
        f"spi_mode={getattr(response, 'value1', 0)} "
        f"metadata_size={getattr(response, 'value2', 0)}"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB bridge CDC serial port")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument(
        "--model",
        type=pathlib.Path,
        help="Model blob for direct boot. Requires --network-info.",
    )
    parser.add_argument(
        "--network-info",
        type=pathlib.Path,
        help="network_info payload for direct boot. Requires --model.",
    )
    parser.add_argument(
        "--mipi-format",
        choices=sorted(MIPI_FORMATS),
        default="image",
        help="MIPI format passed to imx500_mcu_sdk.open().",
    )
    parser.add_argument(
        "--spi-format",
        choices=sorted(SPI_FORMATS),
        default="output",
        help=(
            "SPI format passed to imx500_mcu_sdk.open(). Ignored when "
            "--jpeg-preview is set."
        ),
    )
    parser.add_argument(
        "--metadata-max-bytes",
        type=int,
        default=0,
        help=(
            "Maximum bytes for read_metadata(). 0 means wait for the SDK "
            "metadata size, except --jpeg-preview defaults to 2 MiB."
        ),
    )
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=pathlib.Path("first_run_outputs"),
        help="Directory for saved metadata and optional JPEG output.",
    )
    parser.add_argument(
        "--jpeg-preview",
        action="store_true",
        help="Request JPEG metadata output and save one decoded JPEG frame.",
    )
    parser.add_argument(
        "--jpeg-search-frames",
        type=int,
        default=5,
        help="Maximum metadata frames to search for a JPEG when --jpeg-preview is set.",
    )
    parser.add_argument(
        "--no-probe",
        action="store_true",
        help="Skip imx500_mcu_sdk.probe_imx500_module() before open().",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if (args.model is None) != (args.network_info is None):
        raise SystemExit("--model and --network-info must be passed together")

    if args.model is not None and not args.model.is_file():
        raise SystemExit(f"model file does not exist: {args.model}")
    if args.network_info is not None and not args.network_info.is_file():
        raise SystemExit(f"network_info file does not exist: {args.network_info}")

    if args.timeout <= 0:
        raise SystemExit("--timeout must be > 0")
    if args.fps <= 0:
        raise SystemExit("--fps must be > 0")
    if args.metadata_max_bytes < 0:
        raise SystemExit("--metadata-max-bytes must be >= 0")
    if args.jpeg_search_frames <= 0:
        raise SystemExit("--jpeg-search-frames must be > 0")


def print_checkpoint(index: int, total: int, label: str, status: str, detail: str) -> None:
    dots = "." * max(1, 34 - len(label))
    print(f"[{index}/{total}] {label} {dots} {status}", flush=True)
    if detail:
        print(f"      {detail}", flush=True)


def print_next_unlocks() -> None:
    print("\nNext unlock:", flush=True)
    print("- Want USB3 plug-and-play deployment? Continue with the B0566 USB3 UVC path.", flush=True)
    print("- Want a visible MCU demo? Continue with the ESP32-P4 example.", flush=True)
    print("- Want low-power event output? Continue with the SPI metadata path.", flush=True)
    print("- Want model validation? Continue with the model mission.", flush=True)


def run_checkpoints(checkpoints: list[Checkpoint]) -> int:
    total = len(checkpoints)
    for index, checkpoint in enumerate(checkpoints, start=1):
        try:
            detail = checkpoint.action()
        except Exception as exc:  # noqa: BLE001 - user-facing checkpoint runner.
            print_checkpoint(index, total, checkpoint.label, "FAIL", str(exc))
            print("\nFirst-run validation stopped. Check:", flush=True)
            print("- Hold the module MODE button while connecting USB.", flush=True)
            print("- Confirm the USB CDC bridge port or pass --port explicitly.", flush=True)
            print("- Confirm firmware version 0x00000010 or newer.", flush=True)
            print("- Confirm model/network_info are present in Flash or pass direct-boot files.", flush=True)
            return 1
        print_checkpoint(index, total, checkpoint.label, "PASS", detail)
    print_next_unlocks()
    return 0


def main() -> int:
    args = parse_args()
    validate_args(args)
    state = FirstRunState()

    model = read_optional_file(args.model, "model")
    network_info = read_optional_file(args.network_info, "network_info")
    metadata_max_bytes = args.metadata_max_bytes
    if args.jpeg_preview and metadata_max_bytes == 0:
        metadata_max_bytes = 2 * 1024 * 1024

    def connect_bridge() -> str:
        state.bridge = imx500_mcu_sdk.connect_usb_bridge(
            args.port,
            baudrate=args.baudrate,
            timeout=args.timeout,
        )
        ping = state.bridge.ping()
        if getattr(ping, "result", 0) != 1:
            raise RuntimeError(f"bridge ping failed: result={getattr(ping, 'result', 0)}")
        return f"port={state.bridge.port} {bridge_status_summary(ping)}"

    def read_status() -> str:
        if state.bridge is None:
            raise RuntimeError("USB bridge is not connected")
        status = state.bridge.status()
        detail = bridge_status_summary(status)
        if args.no_probe:
            return detail
        ok, device_id, boot_status = imx500_mcu_sdk.probe_imx500_module()
        if not ok:
            raise RuntimeError(
                f"module probe failed: device_id=0x{device_id:08x} boot_status={boot_status}"
            )
        return f"{detail} device_id=0x{device_id:08x} boot_status={boot_status}"

    def open_stream() -> str:
        spi_format_name = "jpeg-output" if args.jpeg_preview else args.spi_format
        opened = imx500_mcu_sdk.open(
            model,
            network_info,
            MIPI_FORMATS[args.mipi_format],
            SPI_FORMATS[spi_format_name],
            args.fps,
        )
        if not opened:
            raise RuntimeError("imx500_mcu_sdk.open() returned false")
        imx500_mcu_sdk.stream_on()
        boot_name = "flash" if model is None else "direct"
        return f"boot={boot_name} mipi={args.mipi_format} spi={spi_format_name} fps={args.fps}"

    def metadata_read_size() -> int:
        if metadata_max_bytes > 0:
            return metadata_max_bytes

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            size = imx500_mcu_sdk.get_metadata_size()
            if size > 0:
                return size
            time.sleep(METADATA_SIZE_POLL_INTERVAL_SEC)

        raise RuntimeError(
            "METADATA_SIZE_REG stayed 0 after stream_on(); pass a non-zero "
            "--metadata-max-bytes if the frame size is not reported promptly"
        )

    def read_metadata_frame() -> str:
        read_size = metadata_read_size()
        frame = imx500_mcu_sdk.read_metadata(read_size)
        if not frame:
            raise RuntimeError(f"read_metadata({read_size}) returned an empty frame")
        state.metadata_frame = frame
        state.metadata_path = save_bytes(frame, args.output_dir, "metadata", ".bin")
        return f"bytes={len(frame)} read_size={read_size} saved={state.metadata_path}"

    def save_jpeg_preview() -> str:
        frame = state.metadata_frame
        if not frame:
            raise RuntimeError("no metadata frame available for JPEG extraction")

        jpeg = b""
        info = ""
        frames_checked = 0
        for frames_checked in range(1, args.jpeg_search_frames + 1):
            jpeg, info = extract_jpeg_from_metadata(frame)
            if jpeg:
                break
            frame = imx500_mcu_sdk.read_metadata(metadata_max_bytes)
            if not frame:
                raise RuntimeError("read_metadata() returned an empty frame while searching for JPEG")
        else:
            raise RuntimeError(info or "no JPEG found")

        state.jpeg_path = save_bytes(jpeg, args.output_dir, "preview", ".jpg")
        return f"bytes={len(jpeg)} saved={state.jpeg_path} searched={frames_checked} ({info})"

    checkpoints = [
        Checkpoint("USB bridge detected", connect_bridge),
        Checkpoint("IMX500 status read", read_status),
        Checkpoint("SDK open and stream_on", open_stream),
        Checkpoint("Metadata frame received", read_metadata_frame),
    ]
    if args.jpeg_preview:
        checkpoints.append(Checkpoint("JPEG preview saved", save_jpeg_preview))

    try:
        return run_checkpoints(checkpoints)
    finally:
        if state.bridge is not None:
            state.bridge.close()


if __name__ == "__main__":
    raise SystemExit(main())
