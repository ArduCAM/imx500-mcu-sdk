#!/usr/bin/env python3
"""Flash IMX500 blobs over the dedicated USB CDC control interface."""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - import failure path
    raise SystemExit(
        "pyserial is required. Install it with: python -m pip install pyserial"
    ) from exc


USB_VID = 0x2ECA
USB_PID = 0x5021
PROTO_MAGIC = 0x4F525055  # "UPRO"
PROTO_VERSION = 1
PROTO_MAX_CHUNK = 4096
DEFAULT_CHUNK = 1024
DEFAULT_RESPONSE_TIMEOUT = 5.0
DEFAULT_FINALIZE_TIMEOUT = 180.0

TARGET_MODEL = 1
TARGET_NN_INFO = 2

CMD_PING = 1
CMD_GET_STATUS = 2
CMD_BEGIN = 3
CMD_DATA = 4
CMD_FINALIZE = 5
CMD_ABORT = 6

STATE_IDLE = 0
STATE_READY = 1
STATE_RECEIVING = 2
STATE_FINALIZING = 3
STATE_SUCCESS = 4
STATE_FAILED = 5

RESULT_NONE = 0
RESULT_OK = 1
RESULT_BAD_MAGIC = 2
RESULT_BAD_COMMAND = 3
RESULT_BAD_TARGET = 4
RESULT_BAD_SIZE = 5
RESULT_BUSY = 6
RESULT_TARGET_MISMATCH = 7
RESULT_WRITE_FAIL = 8
RESULT_BAD_PAYLOAD = 9
RESULT_BAD_CRC = 10

HEADER_STRUCT = struct.Struct("<IHHIIIII")
STATUS_STRUCT = struct.Struct("<IHHIIIIII")

STATE_NAMES = {
    STATE_IDLE: "idle",
    STATE_READY: "ready",
    STATE_RECEIVING: "receiving",
    STATE_FINALIZING: "finalizing",
    STATE_SUCCESS: "success",
    STATE_FAILED: "failed",
}

RESULT_NAMES = {
    RESULT_NONE: "none",
    RESULT_OK: "ok",
    RESULT_BAD_MAGIC: "bad_magic",
    RESULT_BAD_COMMAND: "bad_command",
    RESULT_BAD_TARGET: "bad_target",
    RESULT_BAD_SIZE: "bad_size",
    RESULT_BUSY: "busy",
    RESULT_TARGET_MISMATCH: "target_mismatch",
    RESULT_WRITE_FAIL: "write_fail",
    RESULT_BAD_PAYLOAD: "bad_payload",
    RESULT_BAD_CRC: "bad_crc",
}

TARGET_NAMES = {
    0: "none",
    TARGET_MODEL: "model",
    TARGET_NN_INFO: "network_info",
}


@dataclass
class DeviceStatus:
    command: int
    seq: int
    state: int
    result: int
    target: int
    bytes_done: int
    bytes_total: int

    def summary(self) -> str:
        return (
            f"state={STATE_NAMES.get(self.state, self.state)} "
            f"result={RESULT_NAMES.get(self.result, self.result)} "
            f"target={TARGET_NAMES.get(self.target, self.target)} "
            f"bytes={self.bytes_done}/{self.bytes_total}"
        )


def crc32_u32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def read_exact(port: serial.Serial, size: int, timeout: float = DEFAULT_RESPONSE_TIMEOUT) -> bytes:
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while len(buf) < size:
        chunk = port.read(size - len(buf))
        if chunk:
            buf.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Timed out waiting for {size} response bytes")
    return bytes(buf)


def decode_status(raw: bytes) -> DeviceStatus:
    (
        magic,
        command,
        version,
        seq,
        state,
        result,
        target,
        bytes_done,
        bytes_total,
    ) = STATUS_STRUCT.unpack(raw)
    if magic != PROTO_MAGIC:
        raise ValueError(f"Bad response magic: 0x{magic:08X}")
    if version != PROTO_VERSION:
        raise ValueError(f"Unsupported response version: {version}")
    return DeviceStatus(
        command=command,
        seq=seq,
        state=state,
        result=result,
        target=target,
        bytes_done=bytes_done,
        bytes_total=bytes_total,
    )


def send_frame(
    port: serial.Serial,
    seq: int,
    command: int,
    target: int = 0,
    arg0: int = 0,
    payload: bytes = b"",
    response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
) -> DeviceStatus:
    header = HEADER_STRUCT.pack(
        PROTO_MAGIC,
        command,
        PROTO_VERSION,
        seq,
        target,
        arg0,
        len(payload),
        crc32_u32(payload) if payload else 0,
    )
    port.write(header)
    if payload:
        port.write(payload)
    port.flush()
    status = decode_status(read_exact(port, STATUS_STRUCT.size, timeout=response_timeout))
    if status.seq != seq or status.command != command:
        raise ValueError(
            f"Unexpected response seq/cmd: got seq={status.seq} cmd={status.command}, "
            f"expected seq={seq} cmd={command}"
        )
    return status


def require_ok(status: DeviceStatus, *, expected_state: int | None = None) -> None:
    if status.result != RESULT_OK:
        raise RuntimeError(status.summary())
    if expected_state is not None and status.state != expected_state:
        raise RuntimeError(
            f"Unexpected state: {STATE_NAMES.get(status.state, status.state)} "
            f"(expected {STATE_NAMES.get(expected_state, expected_state)})"
        )


def pick_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port

    candidates = []
    for port in list_ports.comports():
        if port.vid != USB_VID or port.pid != USB_PID:
            continue
        interface = (getattr(port, "interface", "") or "").upper()
        description = (getattr(port, "description", "") or "").upper()
        score = 0
        if "CTRL" in interface:
            score += 4
        if "CTRL" in description:
            score += 3
        if "IMX500" in description:
            score += 1
        candidates.append((score, port.device))

    if not candidates:
        raise SystemExit(
            "No IMX500 control CDC port found. Use --port COMx to specify it explicitly."
        )

    candidates.sort(reverse=True)
    return candidates[0][1]


def open_port(port_name: str) -> serial.Serial:
    port = serial.Serial(port_name, baudrate=115200, timeout=5, write_timeout=5)
    time.sleep(0.2)
    port.reset_input_buffer()
    port.reset_output_buffer()
    return port


def upload_blob(
    port: serial.Serial,
    seq_start: int,
    target: int,
    path: Path,
    chunk_size: int,
    finalize_timeout: float,
) -> int:
    blob = path.read_bytes()
    seq = seq_start

    status = send_frame(port, seq, CMD_BEGIN, target=target, arg0=len(blob))
    require_ok(status, expected_state=STATE_RECEIVING)
    seq += 1

    total = len(blob)
    sent = 0
    while sent < total:
        end = min(sent + chunk_size, total)
        chunk = blob[sent:end]
        status = send_frame(port, seq, CMD_DATA, target=target, payload=chunk)
        require_ok(status, expected_state=STATE_RECEIVING)
        sent = end
        seq += 1
        print(
            f"{TARGET_NAMES.get(target, target)}: {status.bytes_done}/{status.bytes_total}",
            flush=True,
        )

    print(
        f"{TARGET_NAMES.get(target, target)} finalizing, waiting up to {finalize_timeout:.0f}s...",
        flush=True,
    )
    status = send_frame(
        port,
        seq,
        CMD_FINALIZE,
        target=target,
        response_timeout=finalize_timeout,
    )
    require_ok(status, expected_state=STATE_SUCCESS)
    print(f"{TARGET_NAMES.get(target, target)} finalized: {status.summary()}", flush=True)
    return seq + 1


def query_status(port: serial.Serial, seq: int) -> int:
    status = send_frame(port, seq, CMD_GET_STATUS)
    print(status.summary(), flush=True)
    return seq + 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Flash IMX500 .fpk and network_info.txt over the dedicated USB CDC control port."
    )
    parser.add_argument("--port", help="Control CDC port, for example COM7")
    parser.add_argument("--model", type=Path, help="Path to the .fpk model file")
    parser.add_argument(
        "--network-info",
        type=Path,
        help="Path to the matching network_info.txt file",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=DEFAULT_CHUNK,
        help=f"Per-frame payload size in bytes (default {DEFAULT_CHUNK}, max {PROTO_MAX_CHUNK})",
    )
    parser.add_argument(
        "--finalize-timeout",
        type=float,
        default=DEFAULT_FINALIZE_TIMEOUT,
        help=f"Seconds to wait for FINALIZE response (default {DEFAULT_FINALIZE_TIMEOUT:.0f})",
    )
    parser.add_argument(
        "--status",
        action="store_true",
        help="Only query and print the current device status",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.chunk_size <= 0 or args.chunk_size > PROTO_MAX_CHUNK:
        raise SystemExit(f"--chunk-size must be between 1 and {PROTO_MAX_CHUNK}")
    if args.finalize_timeout <= 0:
        raise SystemExit("--finalize-timeout must be > 0")

    if args.model is None and args.network_info is None and not args.status:
        raise SystemExit("Provide --model and/or --network-info, or use --status")

    port_name = pick_port(args.port)
    print(f"Using port: {port_name}", flush=True)

    seq = 1
    with open_port(port_name) as port:
        ping = send_frame(port, seq, CMD_PING)
        seq += 1
        print(f"Ping: {ping.summary()}", flush=True)

        if args.status and args.model is None and args.network_info is None:
            query_status(port, seq)
            return 0

        if args.model is not None:
            seq = upload_blob(
                port,
                seq,
                TARGET_MODEL,
                args.model,
                args.chunk_size,
                args.finalize_timeout,
            )

        if args.network_info is not None:
            seq = upload_blob(
                port,
                seq,
                TARGET_NN_INFO,
                args.network_info,
                args.chunk_size,
                args.finalize_timeout,
            )

        uploaded_count = int(args.model is not None) + int(args.network_info is not None)
        if uploaded_count <= 1:
            query_status(port, seq)

    return 0


if __name__ == "__main__":
    sys.exit(main())
