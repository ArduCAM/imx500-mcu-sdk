from __future__ import annotations

import argparse
import os
import sys
import time

import serial
from serial.tools import list_ports

READY_MARKER = "TEST_STATUS: READY"
BUSY_MARKER = "TEST_STATUS: BUSY"
PASS_MARKER = "TEST_RESULT: PASS"
FAIL_MARKER = "TEST_RESULT: FAIL"
PONG_MARKER = "PONG"
PICO_USB_VID = 0x2E8A
PORT_HINTS: tuple[tuple[str, int], ...] = (
    ("PICO", 4),
    ("RP2350", 4),
    ("RASPBERRY", 3),
    ("TINYUSB", 1),
)


def _port_sort_key(port: list_ports.ListPortInfo) -> str:
    return str(getattr(port, "device", "") or "")


def _format_port_label(port: list_ports.ListPortInfo) -> str:
    parts: list[str] = []
    for field in ("description", "manufacturer", "product", "interface"):
        value = str(getattr(port, field, "") or "").strip()
        if value and value != port.device and value not in parts:
            parts.append(value)
    vid = getattr(port, "vid", None)
    pid = getattr(port, "pid", None)
    if vid is not None and pid is not None:
        parts.append(f"VID:PID={vid:04X}:{pid:04X}")
    return f"{port.device} ({'; '.join(parts)})" if parts else str(port.device)


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

    preferred = [(port, _score_port(port)) for port in ports if _score_port(port) > 0]
    if len(preferred) == 1:
        port, _ = preferred[0]
        print(f"[INFO] Auto-selected serial port {_format_port_label(port)}")
        return str(port.device)

    if len(ports) == 1:
        port = ports[0]
        print(f"[INFO] Auto-selected only available serial port {_format_port_label(port)}")
        return str(port.device)

    port_list = "\n".join(f"  - {_format_port_label(port)}" for port in ports)
    raise SystemExit(
        "Could not uniquely auto-detect the Pico2 serial port. "
        "Use --port or --list-ports:\n"
        f"{port_list}"
    )


def open_port(port_name: str, baud: int) -> serial.Serial:
    port = serial.Serial(port_name, baudrate=baud, timeout=0.2, write_timeout=0.2)
    try:
        port.dtr = True
    except (AttributeError, OSError, serial.SerialException):
        pass
    try:
        port.rts = False
    except (AttributeError, OSError, serial.SerialException):
        pass
    port.reset_input_buffer()
    port.reset_output_buffer()
    return port


def _read_line(port: serial.Serial, deadline: float) -> str | None:
    line = bytearray()
    while time.time() < deadline:
        chunk = port.read(1)
        if not chunk:
            if line:
                break
            continue
        if chunk in (b"\r", b"\n"):
            if line:
                break
            continue
        line.extend(chunk)
    if not line:
        return None
    return line.decode("utf-8", errors="replace")


def wait_for_ready(port: serial.Serial, timeout_sec: float) -> None:
    deadline = time.time() + timeout_sec
    next_ping_time = 0.0
    while time.time() < deadline:
        now = time.time()
        if now >= next_ping_time:
            port.write(b"PING\n")
            port.flush()
            next_ping_time = now + 0.5
        line = _read_line(port, deadline)
        if line is None:
            continue
        print(f"[BOARD] {line}")
        if READY_MARKER in line or PONG_MARKER in line:
            return
    raise SystemExit(f"Timed out waiting for board response marker: {READY_MARKER} or {PONG_MARKER}")


def run_test(port: serial.Serial, timeout_sec: float) -> bool:
    port.write(b"RUN\n")
    port.flush()

    deadline = time.time() + timeout_sec
    busy_reason: str | None = None
    while time.time() < deadline:
        line = _read_line(port, deadline)
        if line is None:
            continue
        print(f"[BOARD] {line}")
        if BUSY_MARKER in line:
            reason_deadline = min(deadline, time.time() + 1.0)
            while time.time() < reason_deadline:
                reason_line = _read_line(port, reason_deadline)
                if reason_line is None:
                    continue
                print(f"[BOARD] {reason_line}")
                if reason_line.startswith("TEST_REASON:"):
                    busy_reason = reason_line
                    break
            detail = f" ({busy_reason})" if busy_reason else ""
            raise SystemExit(
                "Board reported BUSY: the production test has already run once and the board must be reset before retrying"
                f"{detail}."
            )
        if PASS_MARKER in line:
            return True
        if FAIL_MARKER in line:
            return False
    raise SystemExit("Timed out waiting for TEST_RESULT marker from board.")


def monitor_boot_trigger_mode(port: serial.Serial) -> int:
    cycle_index = 0
    print("[HOST] BOOT-trigger monitor started. Press the Pico BOOT button to trigger each test. Press Ctrl+C to stop.")
    try:
        while True:
            line = _read_line(port, time.time() + 1.0)
            if line is None:
                continue
            print(f"[BOARD] {line}")
            if READY_MARKER in line:
                print("[HOST] Waiting for the next BOOT button trigger...")
                continue
            if line.startswith("MODULE_REMOVED"):
                print("[HOST] Camera removed. Test board state has been reset.")
                continue
            if PASS_MARKER in line:
                cycle_index += 1
                print(f"[HOST] Cycle {cycle_index}: PASS. Replace the camera if needed, then press BOOT for the next test.")
                continue
            if FAIL_MARKER in line:
                cycle_index += 1
                print(f"[HOST] Cycle {cycle_index}: FAIL. Check or replace the camera, then press BOOT to retry.")
                continue
    except KeyboardInterrupt:
        print("\n[HOST] BOOT-trigger monitor stopped by user.")
        return 0


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the Pico2 IMX500 production test over USB serial.")
    parser.add_argument("--port", help="Serial port, e.g. COM7 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate (USB CDC may ignore this)")
    parser.add_argument("--ready-timeout", type=float, default=15.0, help="Seconds to wait for board ready banner")
    parser.add_argument("--test-timeout", type=float, default=60.0, help="Seconds to wait for PASS/FAIL result")
    parser.add_argument("--boot-trigger-monitor", action="store_true", dest="boot_trigger_monitor", help="Monitor BOOT-button-triggered test mode without sending RUN")
    parser.add_argument("--continuous-monitor", action="store_true", dest="boot_trigger_monitor", help=argparse.SUPPRESS)
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_argument_parser().parse_args(argv)
    if args.list_ports:
        return list_available_ports()

    port_name = pick_serial_port(args.port)
    print(f"[INFO] Using serial port {port_name}")
    port = open_port(port_name, args.baud)
    try:
        wait_for_ready(port, args.ready_timeout)
        if args.boot_trigger_monitor:
            return monitor_boot_trigger_mode(port)
        passed = run_test(port, args.test_timeout)
    finally:
        port.close()

    show_summary = os.getenv("HOST_PRODUCTION_TEST_HIDE_SUMMARY") != "1"
    if passed:
        if show_summary:
            print("[HOST] Production test passed.")
        return 0

    if show_summary:
        print("[HOST] Production test failed.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
