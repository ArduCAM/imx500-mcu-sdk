from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import cv2
import numpy as np
import serial
from serial.tools import list_ports

from .metadata_parser import ParsedFrame, checksum32, parse_metadata
from .model_info import ModelInfo

MAGIC = b"IMX5"
HEADER_FMT = "<4sBBHIiI"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
PACKET_TYPE_FRAME = 1
PICO_USB_VID = 0x2E8A
PORT_HINTS: tuple[tuple[str, int], ...] = (
    ("IMX500", 5),
    ("PICO", 4),
    ("RP2350", 4),
    ("RASPBERRY", 3),
    ("TINYUSB", 1),
)


@dataclass(frozen=True)
class ExampleConfig:
    task_name: str
    model_info: ModelInfo
    default_output_dir: str
    default_max_payload: int
    annotate_frame: Callable[[ParsedFrame, argparse.Namespace], np.ndarray]
    save_original: bool = False


def add_common_arguments(
    parser: argparse.ArgumentParser,
    *,
    default_output_dir: str | None,
    default_max_payload: int | None,
    save_original: bool = False,
) -> argparse.ArgumentParser:
    parser.add_argument(
        "--port",
        help="Serial port, e.g. COM7 or /dev/ttyACM0. If omitted, try to auto-detect the Pico2 USB CDC port.",
    )
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit")
    parser.add_argument("--baud", type=int, default=921600, help="Baudrate (USB CDC may ignore this)")
    parser.add_argument("--output", default=default_output_dir, help="Output directory for decoded JPEG files")
    parser.add_argument("--save-img", action="store_true", help="Save annotated JPEG files")
    parser.add_argument("--save-raw", action="store_true", help="Also save raw payload as .bin")
    parser.add_argument("--save-metadata-json", action="store_true", help="Save parsed metadata summary as .json")
    parser.add_argument("--save-tensors", action="store_true", help="Save parsed tensor arrays as .npz")
    parser.add_argument("--save-original", action="store_true", default=save_original, help="Save unannotated JPEG too")
    parser.add_argument("--max-frames", type=int, default=0, help="Stop after N decoded JPEG frames (0 = no limit)")
    parser.add_argument("--max-payload", type=int, default=default_max_payload, help="Reject packet if payload exceeds this")
    parser.add_argument("--show-img", action="store_true", help="Show annotated frames in an OpenCV window while receiving")
    parser.add_argument("--show-fps", action="store_true", help="Print postprocess FPS in examples that support it")
    return parser


def build_argument_parser(config: ExampleConfig) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=f"Receive IMX500 {config.task_name} packets and optionally save annotated JPEGs")
    return add_common_arguments(
        parser,
        default_output_dir=config.default_output_dir,
        default_max_payload=config.default_max_payload,
        save_original=config.save_original,
    )


def run_serial_receiver(config: ExampleConfig, argv: list[str] | None = None) -> int:
    args = build_argument_parser(config).parse_args(argv)
    return run_serial_receiver_with_args(config, args)


def _port_sort_key(port: list_ports.ListPortInfo) -> str:
    return str(getattr(port, "device", "") or "")


def _format_port_label(port: list_ports.ListPortInfo) -> str:
    details: list[str] = []
    for field in ("description", "manufacturer", "product", "interface"):
        value = str(getattr(port, field, "") or "").strip()
        if value and value != port.device and value not in details:
            details.append(value)

    vid = getattr(port, "vid", None)
    pid = getattr(port, "pid", None)
    if vid is not None and pid is not None:
        details.append(f"VID:PID={vid:04X}:{pid:04X}")

    if details:
        return f"{port.device} ({'; '.join(details)})"
    return str(port.device)


def list_available_ports() -> int:
    ports = sorted(list_ports.comports(), key=_port_sort_key)
    if not ports:
        print("[INFO] No serial ports found.")
        return 0

    print("[INFO] Available serial ports:")
    for port in ports:
        print(f"  - {_format_port_label(port)}")
    return 0


def _score_port(port: list_ports.ListPortInfo) -> int:
    score = 0
    if getattr(port, "vid", None) == PICO_USB_VID:
        score += 10

    fields = " ".join(
        str(getattr(port, field, "") or "")
        for field in ("description", "manufacturer", "product", "interface", "hwid")
    ).upper()
    for token, weight in PORT_HINTS:
        if token in fields:
            score += weight
    return score


def pick_serial_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port

    ports = sorted(list_ports.comports(), key=_port_sort_key)
    if not ports:
        raise SystemExit("No serial ports found. Connect the Pico2 board or pass --port explicitly.")

    preferred: list[tuple[list_ports.ListPortInfo, int]] = []
    for port in ports:
        score = _score_port(port)
        if score > 0:
            preferred.append((port, score))
    if len(preferred) == 1:
        port, _ = preferred[0]
        print(f"[INFO] Auto-selected serial port {_format_port_label(port)}")
        return str(port.device)

    if len(ports) == 1:
        port = ports[0]
        print(f"[INFO] Auto-selected only available serial port {_format_port_label(port)}")
        return str(port.device)

    port_list = "\n".join(f"  - {_format_port_label(port)}" for port in ports)
    if preferred:
        raise SystemExit(
            "Multiple possible Pico2 serial ports found. Use --port to choose one explicitly or --list-ports to inspect them:\n"
            f"{port_list}"
        )

    raise SystemExit(
        "Could not auto-detect the Pico2 serial port. Use --port to choose one explicitly or --list-ports to inspect available ports:\n"
        f"{port_list}"
    )


def run_serial_receiver_with_args(config: ExampleConfig, args: argparse.Namespace) -> int:
    if getattr(args, "list_ports", False):
        return list_available_ports()

    port_name = pick_serial_port(args.port)
    args.port = port_name
    os.makedirs(args.output, exist_ok=True)

    ser = serial.Serial(port_name, args.baud, timeout=0.2)
    print(f"[INFO] Opened {port_name} at {args.baud}")
    print(
        f"[INFO] task={config.task_name} model={config.model_info.name} "
        f"input={config.model_info.input_width}x{config.model_info.input_height} "
        f"payload_limit={args.max_payload}"
    )

    buf = bytearray()
    decoded = 0
    last_log = time.time()

    try:
        while True:
            chunk = ser.read(4096)
            if chunk:
                buf.extend(chunk)
            elif time.time() - last_log > 3.0:
                print(f"[INFO] waiting... buffered={len(buf)} bytes")
                last_log = time.time()

            while True:
                packet = extract_packet(buf, args.max_payload)
                if packet is None:
                    break

                seq, payload = packet
                frame_prefix = Path(args.output) / f"frame_{seq:06d}"

                if args.save_raw:
                    frame_prefix.with_suffix(".bin").write_bytes(payload)

                try:
                    parsed_frame = parse_metadata(payload)
                    annotated = config.annotate_frame(parsed_frame, args)
                except NotImplementedError as exc:
                    print(f"[WARN] seq={seq} annotation skipped: {exc}")
                    parsed_frame = parse_metadata(payload)
                    annotated = parsed_frame.image_bgr
                except Exception as exc:
                    print(f"[WARN] seq={seq} parse failed: {exc}")
                    continue

                if args.save_original:
                    cv2.imwrite(str(frame_prefix.with_name(frame_prefix.name + "_orig").with_suffix(".jpg")), parsed_frame.image_bgr)
                if args.save_metadata_json:
                    frame_prefix.with_suffix(".json").write_text(
                        json.dumps(parsed_frame.to_summary_dict(), indent=2),
                        encoding="utf-8",
                    )
                if args.save_tensors:
                    np.savez_compressed(str(frame_prefix.with_suffix(".npz")), **collect_tensor_arrays(parsed_frame))

                decoded += 1
                if args.save_img:
                    out_path = frame_prefix.with_suffix(".jpg")
                    if not cv2.imwrite(str(out_path), annotated):
                        print(f"[WARN] seq={seq} failed to save image")
                        continue
                    print(f"[OK] seq={seq} -> {out_path} shape={annotated.shape}")
                else:
                    print(f"[OK] seq={seq} decoded shape={annotated.shape}")
                if args.max_frames > 0 and decoded >= args.max_frames:
                    print(f"[INFO] reached max-frames={args.max_frames}, exit")
                    return 0
    except KeyboardInterrupt:
        print("\n[INFO] stopped by user")
        return 0
    finally:
        ser.close()
        if args.show_img:
            cv2.destroyAllWindows()


def extract_packet(buf: bytearray, max_payload: int) -> tuple[int, bytes] | None:
    start = buf.find(MAGIC)
    if start < 0:
        if len(buf) > 3:
            del buf[:-3]
        return None

    if start > 0:
        del buf[:start]
    if len(buf) < HEADER_SIZE:
        return None

    magic, version, packet_type, header_len, seq, payload_len, checksum = struct.unpack(HEADER_FMT, buf[:HEADER_SIZE])
    if magic != MAGIC or version != 1 or header_len != HEADER_SIZE:
        del buf[0]
        return extract_packet(buf, max_payload)

    if payload_len < 0:
        print(f"[WARN] seq={seq} frame read failed on device")
        del buf[:HEADER_SIZE]
        return None

    if payload_len > max_payload:
        print(f"[WARN] seq={seq} oversized payload={payload_len}, drop")
        del buf[0]
        return extract_packet(buf, max_payload)

    full_len = HEADER_SIZE + payload_len
    if len(buf) < full_len:
        return None

    payload = bytes(buf[HEADER_SIZE:full_len])
    del buf[:full_len]
    calc = checksum32(payload)
    if calc != checksum:
        print(f"[WARN] checksum mismatch seq={seq} recv=0x{checksum:08X} calc=0x{calc:08X}")
        return None
    if packet_type != PACKET_TYPE_FRAME:
        print(f"[INFO] ignore packet type={packet_type} seq={seq}")
        return None
    return seq, payload


def collect_tensor_arrays(parsed_frame: ParsedFrame) -> dict[str, np.ndarray]:
    arrays: dict[str, np.ndarray] = {"jpeg_bytes": np.frombuffer(parsed_frame.jpeg_data, dtype=np.uint8)}
    for network_index, network in enumerate(parsed_frame.networks):
        for tensor_index, tensor in enumerate(network.input_tensors):
            if tensor.data is not None:
                arrays[f"network{network_index}_input{tensor_index}"] = tensor.data
        for tensor_index, tensor in enumerate(network.output_tensors):
            if tensor.data is not None:
                arrays[f"network{network_index}_output{tensor_index}"] = tensor.data
    return arrays


def main_from_config(config: ExampleConfig) -> int:
    return run_serial_receiver(config, sys.argv[1:])
