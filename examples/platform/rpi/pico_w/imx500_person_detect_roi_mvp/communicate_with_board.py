#!/usr/bin/env python3

import argparse
import errno
import ipaddress
import re
import socket
import subprocess
import sys
import time
from typing import Optional


DEFAULT_PORT = 42424
DISCOVERY_REQUEST = b"IMX500_ROI_MVP_DISCOVER"
PING_REQUEST = b"IMX500_ROI_MVP_PING"
ROUTE_ERRORS = {errno.EHOSTUNREACH, errno.ENETUNREACH}


def parse_ip(response: str, fallback: str) -> str:
    match = re.search(r"\bip=([0-9]{1,3}(?:\.[0-9]{1,3}){3})\b", response)
    if match:
        return match.group(1)
    return fallback


def parse_netmask(value: str):
    try:
        if value.startswith("0x"):
            mask_int = int(value, 16)
            return ipaddress.IPv4Address(mask_int)
        return ipaddress.IPv4Address(value)
    except ValueError:
        return None


def get_ipv4_interfaces():
    interfaces = []

    try:
        output = subprocess.check_output(["ifconfig"], text=True, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return interfaces

    current_name = None
    for line in output.splitlines():
        if line and not line.startswith((" ", "\t")) and ":" in line:
            current_name = line.split(":", 1)[0]
            continue

        match = re.search(r"\binet\s+([0-9.]+)\s+netmask\s+([0-9a-fx.]+)(?:\s+broadcast\s+([0-9.]+))?", line)
        if not match:
            continue

        address = match.group(1)
        if address.startswith("127."):
            continue

        netmask = parse_netmask(match.group(2))
        if netmask is None:
            continue

        network = ipaddress.IPv4Network(f"{address}/{netmask}", strict=False)
        broadcast = match.group(3) or str(network.broadcast_address)
        interfaces.append(
            {
                "name": current_name or "?",
                "address": address,
                "network": network,
                "broadcast": broadcast,
            }
        )

    return interfaces


def unique_broadcasts(user_broadcast: str):
    broadcasts = []
    candidates = [user_broadcast]
    for item in get_ipv4_interfaces():
        candidates.append(item["broadcast"])
        octets = item["address"].split(".")
        if len(octets) == 4:
            candidates.append(".".join(octets[:3] + ["255"]))

    for candidate in candidates:
        if candidate not in broadcasts:
            broadcasts.append(candidate)
    return broadcasts


def source_ip_candidates(board_ip: Optional[str], requested_source_ip: Optional[str]):
    if requested_source_ip:
        return [requested_source_ip]

    interfaces = get_ipv4_interfaces()
    candidates = []
    if board_ip:
        try:
            board_addr = ipaddress.IPv4Address(board_ip)
            for item in interfaces:
                if board_addr in item["network"]:
                    candidates.append(item["address"])
        except ValueError:
            pass

    for item in interfaces:
        candidates.append(item["address"])

    unique = []
    for candidate in candidates:
        if candidate not in unique:
            unique.append(candidate)
    return unique


def print_interface_hint():
    interfaces = get_ipv4_interfaces()
    if not interfaces:
        return

    print("Local IPv4 interfaces:")
    for item in interfaces:
        print(f"  {item['name']}: {item['address']} network={item['network']} broadcast={item['broadcast']}")


def is_route_error(exc: OSError) -> bool:
    return exc.errno in ROUTE_ERRORS


def try_discover_board(broadcasts, port: int, timeout: float, source_ip: Optional[str]):
    last_error = None
    for broadcast in broadcasts:
        try:
            response, addr, elapsed_ms = discover_board(broadcast, port, timeout, source_ip)
            return response, addr, elapsed_ms
        except socket.timeout as exc:
            print(f"  no response from {broadcast}")
            last_error = exc
        except OSError as exc:
            print(f"  discovery via {broadcast} failed: {exc}")
            last_error = exc

    if last_error is not None:
        raise last_error
    raise socket.timeout()


def send_udp_message(
    target: str,
    port: int,
    payload: bytes,
    timeout: float,
    broadcast: bool = False,
    source_ip: Optional[str] = None,
):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        if broadcast:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if source_ip:
            sock.bind((source_ip, 0))

        start = time.monotonic()
        sock.sendto(payload, (target, port))
        data, addr = sock.recvfrom(512)
        elapsed_ms = (time.monotonic() - start) * 1000.0
        return data.decode("utf-8", errors="replace"), addr, elapsed_ms


def discover_board(broadcast: str, port: int, timeout: float, source_ip: Optional[str]):
    suffix = f" from {source_ip}" if source_ip else ""
    print(f"Broadcast discovery: {broadcast}:{port}{suffix}")
    return send_udp_message(broadcast, port, DISCOVERY_REQUEST, timeout, broadcast=True, source_ip=source_ip)


def listen_for_announce(port: int, timeout: float, source_ip: Optional[str]):
    bind_ip = source_ip or ""
    label = source_ip or "0.0.0.0"
    print(f"Listening for board announce on {label}:{port} for {timeout:.1f}s")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(timeout)
        sock.bind((bind_ip, port))
        start = time.monotonic()
        while True:
            data, addr = sock.recvfrom(512)
            response = data.decode("utf-8", errors="replace")
            if response.startswith("IMX500_ROI_MVP_OK"):
                elapsed_ms = (time.monotonic() - start) * 1000.0
                return response, addr, elapsed_ms


def ping_board(ip: str, port: int, timeout: float, source_ip: Optional[str]):
    suffix = f" from {source_ip}" if source_ip else ""
    print(f"Direct ping: {ip}:{port}{suffix}")
    return send_udp_message(ip, port, PING_REQUEST, timeout, broadcast=False, source_ip=source_ip)


def try_ping_board(ip: str, port: int, timeout: float, source_ip: Optional[str]):
    errors = []
    candidates = [source_ip] if source_ip else [None] + source_ip_candidates(ip, None)
    for candidate in candidates:
        try:
            return ping_board(ip, port, timeout, candidate)
        except OSError as exc:
            errors.append(exc)
            if is_route_error(exc):
                print(f"  route failed{f' from {candidate}' if candidate else ''}: {exc}")
                continue
            raise

    if errors:
        raise errors[-1]
    raise socket.timeout()


def try_broadcast_ping_board(
    board_ip: str,
    broadcasts,
    port: int,
    timeout: float,
    source_ip: Optional[str],
):
    last_error = None
    for broadcast in broadcasts:
        try:
            print(f"Broadcast ping: {broadcast}:{port}{f' from {source_ip}' if source_ip else ''}")
            response, addr, elapsed_ms = send_udp_message(
                broadcast,
                port,
                PING_REQUEST,
                timeout,
                broadcast=True,
                source_ip=source_ip,
            )
            response_ip = parse_ip(response, addr[0])
            if addr[0] == board_ip or response_ip == board_ip:
                return response, addr, elapsed_ms
            print(f"  ignored response from {addr[0]} for board {response_ip}")
        except socket.timeout as exc:
            print(f"  no ping response from {broadcast}")
            last_error = exc
        except OSError as exc:
            print(f"  broadcast ping via {broadcast} failed: {exc}")
            last_error = exc

    if last_error is not None:
        raise last_error
    raise socket.timeout()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Discover and verify UDP communication with imx500_person_detect_roi_mvp firmware."
    )
    parser.add_argument("--ip", help="Board IP address. If omitted, the script broadcasts a discovery packet first.")
    parser.add_argument("--broadcast", default="255.255.255.255", help="Broadcast address to use for discovery.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port used by the firmware.")
    parser.add_argument("--source-ip", help="Local IPv4 address to bind before sending, for example 192.168.1.75.")
    parser.add_argument("--timeout", type=float, default=3.0, help="Response timeout in seconds.")
    args = parser.parse_args()

    try:
        board_ip = None

        if args.ip:
            try:
                response, addr, elapsed_ms = try_ping_board(args.ip, args.port, args.timeout, args.source_ip)
                print(f"Ping response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
                print(f"  {response}")
                print("Communication OK")
                return 0
            except socket.timeout:
                print("Direct ping timed out.")
                print("Trying broadcast discovery to find the board IP...")
                print_interface_hint()
            except OSError as exc:
                if not is_route_error(exc):
                    raise
                print(f"Direct ping failed: {exc}")
                print("Trying broadcast discovery to find the board IP...")
                print_interface_hint()

            try:
                response, addr, elapsed_ms = listen_for_announce(args.port, args.timeout, args.source_ip)
            except socket.timeout:
                response, addr, elapsed_ms = try_discover_board(
                    unique_broadcasts(args.broadcast),
                    args.port,
                    args.timeout,
                    args.source_ip,
                )
            board_ip = parse_ip(response, addr[0])
            print(f"Discovery response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
            print(f"  {response}")
            print(f"Board IP: {board_ip}")
        else:
            print_interface_hint()
            try:
                response, addr, elapsed_ms = try_discover_board(
                    unique_broadcasts(args.broadcast),
                    args.port,
                    args.timeout,
                    args.source_ip,
                )
            except socket.timeout:
                response, addr, elapsed_ms = listen_for_announce(args.port, args.timeout, args.source_ip)
            board_ip = parse_ip(response, addr[0])
            print(f"Discovery response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
            print(f"  {response}")
            print(f"Board IP: {board_ip}")

        try:
            response, addr, elapsed_ms = try_ping_board(board_ip, args.port, args.timeout, args.source_ip)
            ping_mode = "Ping"
        except OSError as exc:
            if not is_route_error(exc):
                raise
            print(f"Direct ping to {board_ip} failed after discovery: {exc}")
            print("Falling back to broadcast ping; discovery already proved the board can reply.")
            response, addr, elapsed_ms = try_broadcast_ping_board(
                board_ip,
                unique_broadcasts(args.broadcast),
                args.port,
                args.timeout,
                args.source_ip,
            )
            ping_mode = "Broadcast ping"
        except socket.timeout:
            print(f"Direct ping to {board_ip} timed out after discovery.")
            print("Falling back to broadcast ping; discovery already proved the board can reply.")
            response, addr, elapsed_ms = try_broadcast_ping_board(
                board_ip,
                unique_broadcasts(args.broadcast),
                args.port,
                args.timeout,
                args.source_ip,
            )
            ping_mode = "Broadcast ping"

        print(f"{ping_mode} response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
        print(f"  {response}")
        print("Communication OK")
        return 0
    except socket.timeout:
        print("Timed out waiting for board response.", file=sys.stderr)
        print("Check that the board is powered, firmware is flashed, Wi-Fi is connected, and the computer is on the same LAN.", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"UDP communication failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
