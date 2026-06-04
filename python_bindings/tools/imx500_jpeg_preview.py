#!/usr/bin/env python3
"""Preview JPEG frames from the IMX500 SDK USB bridge."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
import time
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

try:
    import cv2
    import numpy as np
except ImportError as exc:
    raise SystemExit(
        "JPEG preview requires OpenCV and NumPy:\n"
        "  python3 -m pip install opencv-python numpy"
    ) from exc


IMX500_HEADER_LEN = 12
JPEG_ALIGNMENT = 1024
JPEG_MIN_SIZE_BYTES = 4096


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
                            f"header frame={frame_count} net={network_ordinal} "
                            f"valid={valid_flag} jpeg_size={jpeg_size}"
                        )
                        return jpeg, info
        except struct.error:
            pass

    jpeg = trim_jpeg_bytes(frame)
    if jpeg:
        return jpeg, "fallback SOI/EOI scan"
    return b"", "no JPEG SOI/EOI marker found"


def read_optional_file(path: pathlib.Path | None) -> bytes | None:
    if path is None:
        return None
    data = path.read_bytes()
    print(f"loaded {path}: {len(data)} bytes", flush=True)
    return data


def save_jpeg(data: bytes, output_dir: pathlib.Path, index: int) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = output_dir / f"jpeg_{timestamp}_{index:04d}.jpg"
    path.write_bytes(data)
    return path


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
        "--max-bytes",
        type=int,
        default=2 * 1024 * 1024,
        help="Read buffer size for JPEG metadata frames.",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=0,
        help="Stop after N decoded frames. 0 means run until q/Esc/Ctrl-C.",
    )
    parser.add_argument(
        "--save-dir",
        type=pathlib.Path,
        help="Optional directory for saving decoded JPEG frames.",
    )
    parser.add_argument(
        "--save-every",
        type=int,
        default=1,
        help="Save every Nth decoded frame when --save-dir is set.",
    )
    parser.add_argument(
        "--no-window",
        action="store_true",
        help="Do not open an OpenCV preview window; useful for save-only checks.",
    )
    parser.add_argument(
        "--no-open",
        action="store_true",
        help=(
            "Attach to an already configured stream instead of calling "
            "imx500_mcu_sdk.open()/imx500_mcu_sdk.stream_on()."
        ),
    )
    parser.add_argument(
        "--no-probe",
        action="store_true",
        help="Skip imx500_mcu_sdk.probe_imx500_module() before opening the stream.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if (args.model is None) != (args.network_info is None):
        raise SystemExit("--model and --network-info must be passed together")
    if args.max_bytes <= 0:
        raise SystemExit("--max-bytes must be > 0")
    if args.save_every <= 0:
        raise SystemExit("--save-every must be > 0")

    model = read_optional_file(args.model)
    network_info = read_optional_file(args.network_info)

    bridge = imx500_mcu_sdk.connect_usb_bridge(
        args.port,
        baudrate=args.baudrate,
        timeout=args.timeout,
    )

    window_name = "IMX500 JPEG Preview"
    decoded_frames = 0
    read_frames = 0
    last_report = time.monotonic()
    last_report_frames = 0
    metadata_buffer = bytearray(args.max_bytes)

    try:
        print(f"bridge port: {bridge.port}", flush=True)
        print(f"ping: {bridge.ping()}", flush=True)
        print(f"status: {bridge.status()}", flush=True)

        if not args.no_probe:
            ok, device_id, boot_status = imx500_mcu_sdk.probe_imx500_module()
            print(
                f"probe: ok={ok} device_id=0x{device_id:08x} boot_status={boot_status}",
                flush=True,
            )

        if not args.no_open:
            print("calling imx500_mcu_sdk.open(..., spi=jpeg-output)", flush=True)
            opened = imx500_mcu_sdk.open(
                model,
                network_info,
                imx500_mcu_sdk.MipiDataFormat.IMAGE,
                imx500_mcu_sdk.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                args.fps,
            )
            print(f"imx500_mcu_sdk.open result: {opened}", flush=True)
            if not opened:
                return 2
            print("calling imx500_mcu_sdk.stream_on()", flush=True)
            imx500_mcu_sdk.stream_on()

        if not args.no_window:
            cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)

        print("reading JPEG metadata frames; press q or Esc to quit.", flush=True)
        while args.max_frames == 0 or decoded_frames < args.max_frames:
            n = imx500_mcu_sdk.read_metadata(metadata_buffer)
            read_frames += 1
            if n <= 0:
                print("empty metadata frame; waiting for next frame...", flush=True)
                continue
            frame = bytes(memoryview(metadata_buffer)[:n])

            jpeg, info = extract_jpeg_from_metadata(frame)
            if not jpeg:
                print(
                    f"frame {read_frames}: metadata={len(frame)} bytes, {info}",
                    flush=True,
                )
                continue

            image = cv2.imdecode(np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
            if image is None:
                print(
                    f"frame {read_frames}: JPEG decode failed, jpeg={len(jpeg)} bytes ({info})",
                    flush=True,
                )
                continue

            decoded_frames += 1
            now = time.monotonic()
            if now - last_report >= 1.0:
                fps = (decoded_frames - last_report_frames) / (now - last_report)
                print(
                    f"decoded={decoded_frames} fps={fps:.1f} "
                    f"image={image.shape[1]}x{image.shape[0]} jpeg={len(jpeg)} "
                    f"metadata={len(frame)} ({info})",
                    flush=True,
                )
                last_report = now
                last_report_frames = decoded_frames

            if args.save_dir is not None and decoded_frames % args.save_every == 0:
                path = save_jpeg(jpeg, args.save_dir, decoded_frames)
                print(f"saved {path}", flush=True)

            if not args.no_window:
                cv2.imshow(window_name, image)
                key = cv2.waitKey(1) & 0xFF
                if key in (27, ord("q")):
                    break

    except KeyboardInterrupt:
        print("stopped", flush=True)
    finally:
        bridge.close()
        if not args.no_window:
            cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
