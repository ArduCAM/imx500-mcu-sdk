#!/usr/bin/env python3

import argparse
import errno
import io
import ipaddress
import json
import re
import socket
import subprocess
import sys
import time
from typing import Optional


DEFAULT_PORT = 42424
DISCOVERY_REQUEST = b"IMX500_ROI_MVP_DISCOVER"
PING_REQUEST = b"IMX500_ROI_MVP_PING"
FRAME_REQUEST = b"IMX500_ROI_MVP_GET_FRAME"
SET_ROI_REQUEST = b"IMX500_ROI_MVP_SET_ROI"
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
    candidates = []
    for item in get_ipv4_interfaces():
        candidates.append(item["broadcast"])
    candidates.append(user_broadcast)

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
    first_timeout = None
    for broadcast in broadcasts:
        try:
            response, addr, elapsed_ms = discover_board(broadcast, port, timeout, source_ip)
            return response, addr, elapsed_ms
        except socket.timeout as exc:
            print(f"  no response from {broadcast}")
            if first_timeout is None:
                first_timeout = exc
            last_error = exc
        except OSError as exc:
            print(f"  discovery via {broadcast} failed: {exc}")
            last_error = exc

    if first_timeout is not None:
        raise first_timeout
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


def resolve_board(broadcasts, port: int, timeout: float, source_ip: Optional[str], retries: int):
    retries = max(1, retries)
    listen_timeout = min(max(timeout * 0.5, 0.75), 2.0)
    last_error = None

    for attempt in range(1, retries + 1):
        if retries > 1:
            print(f"Discovery attempt {attempt}/{retries}")

        try:
            return listen_for_announce(port, listen_timeout, source_ip)
        except socket.timeout as exc:
            last_error = exc
        except OSError as exc:
            print(f"  announce listen failed: {exc}")
            last_error = exc

        try:
            return try_discover_board(broadcasts, port, timeout, source_ip)
        except socket.timeout as exc:
            last_error = exc
        except OSError as exc:
            last_error = exc

        try:
            return listen_for_announce(port, listen_timeout, source_ip)
        except socket.timeout as exc:
            last_error = exc
        except OSError as exc:
            print(f"  announce listen failed: {exc}")
            last_error = exc

        if attempt < retries:
            time.sleep(0.25)

    if last_error is not None:
        raise last_error
    raise socket.timeout()


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
    first_timeout = None
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
            if first_timeout is None:
                first_timeout = exc
            last_error = exc
        except OSError as exc:
            print(f"  broadcast ping via {broadcast} failed: {exc}")
            last_error = exc

    if first_timeout is not None:
        raise first_timeout
    if last_error is not None:
        raise last_error
    raise socket.timeout()


def parse_header_fields(header: str):
    fields = {}
    for item in header.split()[1:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        fields[key] = value
    return fields


def request_frame_to_target(
    target: str,
    board_ip: str,
    port: int,
    timeout: float,
    source_ip: Optional[str],
    broadcast: bool,
    request_id: int,
    verbose: bool = True,
):
    suffix = f" via broadcast {target}" if broadcast else f" via direct {target}"
    if verbose:
        print(f"Requesting AI frame: {board_ip}:{port} id={request_id}{suffix}")

    payload = FRAME_REQUEST + f" id={request_id}".encode("ascii")
    jpeg_chunks = {}
    expected_chunks = None
    frame_json = None
    jpeg_len = 0
    saw_pending = False

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
        if broadcast:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if source_ip:
            sock.bind((source_ip, 0))

        sock.sendto(payload, (target, port))
        deadline = time.monotonic() + timeout
        next_progress = time.monotonic() + 2.0

        while time.monotonic() < deadline:
            now = time.monotonic()
            sock.settimeout(min(0.25, max(0.05, deadline - now)))
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                if verbose and saw_pending and time.monotonic() >= next_progress:
                    remaining = max(0.0, deadline - time.monotonic())
                    print(f"  waiting for frame data after pending... {remaining:.1f}s left")
                    next_progress = time.monotonic() + 2.0
                continue
            if addr[0] != board_ip:
                continue

            if data.startswith(b"IMX500_ROI_MVP_FRAME_PENDING"):
                saw_pending = True
                if verbose:
                    print(f"  board accepted frame request id={request_id}")
                continue

            header_bytes, sep, body = data.partition(b"\n")
            header = header_bytes.decode("utf-8", errors="replace")
            fields = parse_header_fields(header)
            packet_id = int(fields.get("id", "0") or "0")
            if packet_id != request_id:
                continue

            if header.startswith("IMX500_ROI_MVP_FRAME_JSON"):
                jpeg_len = int(fields.get("jpeg_len", "0") or "0")
                expected_chunks = int(fields.get("chunks", "0") or "0")
                json_len = int(fields.get("json_len", str(len(body))) or len(body))
                frame_json = json.loads(body[:json_len].decode("utf-8"))
                if expected_chunks == 0:
                    return frame_json, b""
                continue

            if header.startswith("IMX500_ROI_MVP_FRAME_JPEG"):
                index = int(fields.get("index", "-1"))
                expected_chunks = int(fields.get("chunks", expected_chunks or 0))
                chunk_len = int(fields.get("len", str(len(body))) or len(body))
                if index >= 0:
                    jpeg_chunks[index] = body[:chunk_len]
                if expected_chunks is not None and len(jpeg_chunks) >= expected_chunks and frame_json is not None:
                    jpeg = b"".join(jpeg_chunks[i] for i in range(expected_chunks))
                    return frame_json, jpeg[:jpeg_len]
                continue

            if header.startswith("IMX500_ROI_MVP_FRAME_END"):
                if frame_json is not None and expected_chunks is not None and len(jpeg_chunks) >= expected_chunks:
                    jpeg = b"".join(jpeg_chunks[i] for i in range(expected_chunks))
                    return frame_json, jpeg[:jpeg_len]

    if saw_pending:
        raise socket.timeout("board accepted frame request but did not send frame data before timeout")
    raise socket.timeout("timed out waiting for frame response; board did not acknowledge GET_FRAME")


def request_frame_once(ip: str, port: int, timeout: float, source_ip: Optional[str], broadcasts, verbose: bool = True):
    request_id = int(time.monotonic() * 1000) & 0xFFFFFFFF
    try:
        return request_frame_to_target(ip, ip, port, timeout, source_ip, False, request_id, verbose)
    except OSError as exc:
        if not is_route_error(exc):
            raise
        if verbose:
            print(f"Direct AI frame request failed: {exc}")
            print("Falling back to broadcast frame request.")

    last_error = None
    first_timeout = None
    for broadcast in broadcasts:
        try:
            return request_frame_to_target(broadcast, ip, port, timeout, source_ip, True, request_id, verbose)
        except socket.timeout as exc:
            if verbose:
                print(f"  no frame response via {broadcast}: {exc}")
            if first_timeout is None:
                first_timeout = exc
            last_error = exc
        except OSError as exc:
            if verbose:
                print(f"  broadcast frame request via {broadcast} failed: {exc}")
            last_error = exc

    if first_timeout is not None:
        raise first_timeout
    if last_error is not None:
        raise last_error
    raise socket.timeout("timed out waiting for frame response")


def request_frame(ip: str, port: int, timeout: float, source_ip: Optional[str], broadcasts, verbose: bool = True, retries: int = 2):
    last_error = None
    retries = max(1, retries)
    for attempt in range(1, retries + 1):
        if verbose and retries > 1:
            print(f"Frame request attempt {attempt}/{retries}")
        try:
            return request_frame_once(ip, port, timeout, source_ip, broadcasts, verbose)
        except socket.timeout as exc:
            last_error = exc
            if verbose:
                print(f"Frame request attempt {attempt}/{retries} timed out: {exc}")
            time.sleep(0.2)

    if last_error is not None:
        raise last_error
    raise socket.timeout("timed out waiting for frame response")


def roi_payload(points, request_id: int):
    point_text = ";".join(f"{x:.6f},{y:.6f}" for x, y in points)
    return SET_ROI_REQUEST + f" id={request_id} points={point_text}".encode("ascii")


def send_roi_to_target(
    target: str,
    board_ip: str,
    port: int,
    timeout: float,
    source_ip: Optional[str],
    broadcast: bool,
    points,
    request_id: int,
):
    suffix = f" via broadcast {target}" if broadcast else f" via direct {target}"
    print(f"Setting ROI on {board_ip}:{port} id={request_id}{suffix}")
    payload = roi_payload(points, request_id)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        if broadcast:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if source_ip:
            sock.bind((source_ip, 0))

        deadline = time.monotonic() + timeout
        next_send = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                sock.sendto(payload, (target, port))
                next_send = now + 0.35

            sock.settimeout(min(0.2, max(0.05, deadline - time.monotonic())))
            try:
                data, addr = sock.recvfrom(512)
            except socket.timeout:
                continue

            if addr[0] != board_ip:
                print(f"  ignored ROI response from {addr[0]}:{addr[1]}")
                continue
            response = data.decode("utf-8", errors="replace")
            fields = parse_header_fields(response)
            response_id = int(fields.get("id", str(request_id)) or request_id)
            if response_id != request_id:
                print(f"  ignored stale ROI response id={response_id}")
                continue
            if response.startswith("IMX500_ROI_MVP_SET_ROI_OK"):
                return response, addr
            if response.startswith("IMX500_ROI_MVP_SET_ROI_ERR"):
                raise RuntimeError("board rejected ROI payload")

    raise socket.timeout("timed out waiting for SET_ROI acknowledgement")


def set_roi(ip: str, port: int, timeout: float, source_ip: Optional[str], broadcasts, points):
    request_id = int(time.monotonic() * 1000) & 0xFFFFFFFF
    timeout = max(timeout, 5.0)
    try:
        return send_roi_to_target(ip, ip, port, timeout, source_ip, False, points, request_id)
    except OSError as exc:
        if not is_route_error(exc):
            raise
        print(f"Direct ROI request failed: {exc}")
        print("Falling back to broadcast ROI request.")

    last_error = None
    first_timeout = None
    for broadcast in broadcasts:
        try:
            return send_roi_to_target(broadcast, ip, port, timeout, source_ip, True, points, request_id)
        except socket.timeout as exc:
            print(f"  no ROI acknowledgement via {broadcast}: {exc}")
            if first_timeout is None:
                first_timeout = exc
            last_error = exc
        except OSError as exc:
            print(f"  broadcast ROI request via {broadcast} failed: {exc}")
            last_error = exc

    if first_timeout is not None:
        raise first_timeout
    if last_error is not None:
        raise last_error
    raise socket.timeout("timed out waiting for ROI acknowledgement")


def print_ai_results(frame_json):
    detections = frame_json.get("detections", [])
    image = frame_json.get("image", {})
    print(
        f"AI frame={frame_json.get('frame')} detections={len(detections)} "
        f"jpeg_bytes={image.get('bytes', 0)} mode={frame_json.get('mode')}"
    )
    for index, item in enumerate(detections):
        box = item.get("box", [0, 0, 0, 0])
        print(
            f"  #{index} {item.get('label', item.get('class_id'))} "
            f"score={item.get('score', 0):.3f} "
            f"box_rel=({box[0]:.4f}, {box[1]:.4f}, {box[2]:.4f}, {box[3]:.4f})"
        )


def save_and_draw_frame(frame_json, jpeg_bytes: bytes, output: str, display: bool):
    print_ai_results(frame_json)

    with open(output + ".json", "w", encoding="utf-8") as fp:
        json.dump(frame_json, fp, indent=2)
        fp.write("\n")

    if not jpeg_bytes:
        print("No JPEG payload returned; wrote JSON result only.")
        return

    try:
        from PIL import Image, ImageDraw
    except ImportError:
        with open(output, "wb") as fp:
            fp.write(jpeg_bytes)
        print(f"Pillow is not installed; saved raw JPEG to {output}")
        return

    image = Image.open(io.BytesIO(jpeg_bytes)).convert("RGB")
    draw = ImageDraw.Draw(image)
    width, height = image.size

    for item in frame_json.get("detections", []):
        box = item.get("box", [0, 0, 0, 0])
        x1 = max(0, min(width - 1, int(box[0] * width)))
        y1 = max(0, min(height - 1, int(box[1] * height)))
        x2 = max(0, min(width - 1, int(box[2] * width)))
        y2 = max(0, min(height - 1, int(box[3] * height)))
        label = f"{item.get('label', 'obj')} {item.get('score', 0):.2f}"
        draw.rectangle((x1, y1, x2, y2), outline=(255, 48, 48), width=3)
        label_box = draw.textbbox((x1, y1), label)
        draw.rectangle(label_box, fill=(255, 48, 48))
        draw.text((x1, y1), label, fill=(255, 255, 255))

    image.save(output, quality=92)
    print(f"Saved annotated frame to {output}")
    print(f"Saved metadata JSON to {output}.json")
    if display:
        image.show()


def import_cv2_numpy():
    try:
        import cv2
        import numpy as np
    except ImportError as exc:
        raise RuntimeError("OpenCV preview requires opencv-python and numpy. Install with: pip install -r requirements.txt") from exc
    return cv2, np


def decode_jpeg_bgr(jpeg_bytes: bytes):
    cv2, np = import_cv2_numpy()
    data = np.frombuffer(jpeg_bytes, dtype=np.uint8)
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError("failed to decode JPEG frame")
    return image


def draw_frame_overlay(image, frame_json):
    cv2, np = import_cv2_numpy()
    height, width = image.shape[:2]

    roi = frame_json.get("roi", {})
    points = roi.get("points", [])
    if roi.get("configured") and len(points) == 4:
        pts = np.array(
            [[int(max(0, min(1, p[0])) * width), int(max(0, min(1, p[1])) * height)] for p in points],
            dtype=np.int32,
        )
        cv2.polylines(image, [pts], True, (0, 220, 255), 2, lineType=cv2.LINE_AA)
        for index, point in enumerate(pts):
            cv2.circle(image, tuple(point), 4, (0, 220, 255), -1, lineType=cv2.LINE_AA)
            cv2.putText(image, str(index + 1), tuple(point + 6), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 220, 255), 1)

    for item in frame_json.get("detections", []):
        box = item.get("box", [0, 0, 0, 0])
        x1 = int(max(0, min(1, box[0])) * width)
        y1 = int(max(0, min(1, box[1])) * height)
        x2 = int(max(0, min(1, box[2])) * width)
        y2 = int(max(0, min(1, box[3])) * height)
        warn = bool(item.get("roi_intersects"))
        color = (0, 0, 255) if warn else (0, 220, 0)
        label = f"{'WARN ' if warn else ''}{item.get('label', 'obj')} {item.get('score', 0):.2f}"
        cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)
        label_size, baseline = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        label_y = max(0, y1 - label_size[1] - baseline - 4)
        cv2.rectangle(image, (x1, label_y), (x1 + label_size[0] + 8, label_y + label_size[1] + baseline + 6), color, -1)
        cv2.putText(image, label, (x1 + 4, label_y + label_size[1] + 2), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

    status = f"frame={frame_json.get('frame')} detections={len(frame_json.get('detections', []))} q=quit s=save"
    cv2.putText(image, status, (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(image, status, (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (20, 20, 20), 1, cv2.LINE_AA)
    return image


def collect_roi_points(image):
    cv2, np = import_cv2_numpy()
    window = "IMX500 ROI setup"
    points = []

    def on_mouse(event, x, y, flags, userdata):
        del flags, userdata
        if event == cv2.EVENT_LBUTTONDOWN and len(points) < 4:
            points.append((x, y))

    cv2.namedWindow(window, cv2.WINDOW_NORMAL)
    cv2.setMouseCallback(window, on_mouse)

    try:
        while True:
            canvas = image.copy()
            for index, point in enumerate(points):
                cv2.circle(canvas, point, 5, (0, 220, 255), -1, lineType=cv2.LINE_AA)
                cv2.putText(canvas, str(index + 1), (point[0] + 7, point[1] + 7), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 220, 255), 1)
            if len(points) >= 2:
                cv2.polylines(canvas, [np.array(points, dtype=np.int32)], False, (0, 220, 255), 2, lineType=cv2.LINE_AA)
            if len(points) == 4:
                cv2.polylines(canvas, [np.array(points, dtype=np.int32)], True, (0, 220, 255), 2, lineType=cv2.LINE_AA)

            prompt = "click 4 ROI points, Enter/Space=accept, r=reset, q=skip"
            cv2.putText(canvas, prompt, (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2, cv2.LINE_AA)
            cv2.putText(canvas, prompt, (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (20, 20, 20), 1, cv2.LINE_AA)
            cv2.imshow(window, canvas)

            key = cv2.waitKey(20) & 0xFF
            if key in (13, 10, 32) and len(points) == 4:
                break
            if key == ord("r"):
                points.clear()
            if key in (ord("q"), 27):
                return None
    finally:
        cv2.destroyWindow(window)

    height, width = image.shape[:2]
    return [(x / float(width), y / float(height)) for x, y in points]


def run_opencv_roi_monitor(board_ip: str, args):
    broadcasts = unique_broadcasts(args.broadcast)
    frame_json, jpeg_bytes = request_frame(
        board_ip,
        args.port,
        args.frame_timeout,
        args.source_ip,
        broadcasts,
        verbose=True,
        retries=args.frame_retries,
    )
    if not jpeg_bytes:
        raise RuntimeError("ROI setup needs JPEG frames. Build firmware with JPEG input tensor + output tensor mode.")

    first_image = decode_jpeg_bgr(jpeg_bytes)
    if not args.skip_roi:
        roi_points = collect_roi_points(first_image)
        if roi_points is not None:
            response, addr = set_roi(board_ip, args.port, args.timeout, args.source_ip, broadcasts, roi_points)
            print(f"ROI response from {addr[0]}:{addr[1]}: {response}")

    cv2, _ = import_cv2_numpy()
    window = "IMX500 ROI monitor"
    try:
        cv2.namedWindow(window, cv2.WINDOW_NORMAL)

        while True:
            try:
                frame_json, jpeg_bytes = request_frame(
                    board_ip,
                    args.port,
                    args.frame_timeout,
                    args.source_ip,
                    broadcasts,
                    verbose=False,
                    retries=1,
                )
            except socket.timeout as exc:
                print(f"Frame timeout: {exc}")
                continue

            if not jpeg_bytes:
                continue

            image = decode_jpeg_bgr(jpeg_bytes)
            image = draw_frame_overlay(image, frame_json)
            cv2.imshow(window, image)

            key = cv2.waitKey(max(1, int(1000 / max(1.0, args.preview_fps)))) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord("s"):
                cv2.imwrite(args.output, image)
                with open(args.output + ".json", "w", encoding="utf-8") as fp:
                    json.dump(frame_json, fp, indent=2)
                    fp.write("\n")
                print(f"Saved annotated frame to {args.output}")
    finally:
        cv2.destroyAllWindows()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Discover and verify UDP communication with imx500_person_detect_roi_mvp firmware."
    )
    parser.add_argument("--ip", help="Board IP address. If omitted, the script broadcasts a discovery packet first.")
    parser.add_argument("--broadcast", default="255.255.255.255", help="Broadcast address to use for discovery.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port used by the firmware.")
    parser.add_argument("--source-ip", help="Local IPv4 address to bind before sending, for example 192.168.1.75.")
    parser.add_argument("--timeout", type=float, default=3.0, help="Response timeout in seconds.")
    parser.add_argument("--discovery-retries", type=int, default=5, help="Discovery retry rounds before giving up.")
    parser.add_argument("--frame-timeout", type=float, default=8.0, help="Timeout for the AI frame backend response.")
    parser.add_argument("--no-frame", action="store_true", help="Only verify communication; do not request an AI frame.")
    parser.add_argument("--require-ping", action="store_true", help="Fail if the post-discovery ping check does not respond.")
    parser.add_argument("--output", default="imx500_roi_result.jpg", help="Path for the annotated JPEG output.")
    parser.add_argument("--display", action="store_true", help="Open the annotated image after saving when Pillow is installed.")
    parser.add_argument("--single-frame", action="store_true", help="Save one frame and exit instead of opening the OpenCV ROI monitor.")
    parser.add_argument("--skip-roi", action="store_true", help="Do not prompt for ROI drawing; use the ROI already stored on the board.")
    parser.add_argument("--preview-fps", type=float, default=8.0, help="OpenCV preview refresh limit.")
    parser.add_argument("--frame-retries", type=int, default=2, help="Frame request retries when the board acknowledges but frame packets are lost.")
    args = parser.parse_args()

    try:
        board_ip = None

        if args.ip:
            try:
                response, addr, elapsed_ms = try_ping_board(args.ip, args.port, args.timeout, args.source_ip)
                print(f"Ping response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
                print(f"  {response}")
                board_ip = parse_ip(response, addr[0])
                if args.no_frame:
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

            if board_ip is None:
                response, addr, elapsed_ms = resolve_board(
                    unique_broadcasts(args.broadcast),
                    args.port,
                    args.timeout,
                    args.source_ip,
                    args.discovery_retries,
                )
                board_ip = parse_ip(response, addr[0])
                print(f"Discovery response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
                print(f"  {response}")
                print(f"Board IP: {board_ip}")
        else:
            print_interface_hint()
            response, addr, elapsed_ms = resolve_board(
                unique_broadcasts(args.broadcast),
                args.port,
                args.timeout,
                args.source_ip,
                args.discovery_retries,
            )
            board_ip = parse_ip(response, addr[0])
            print(f"Discovery response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
            print(f"  {response}")
            print(f"Board IP: {board_ip}")

        try:
            response, addr, elapsed_ms = try_ping_board(board_ip, args.port, args.timeout, args.source_ip)
            print(f"Ping response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
            print(f"  {response}")
        except socket.timeout as exc:
            print(f"Direct ping to {board_ip} timed out after discovery.")
            print("Trying broadcast ping once; discovery already proved the board can reply.")
            try:
                response, addr, elapsed_ms = try_broadcast_ping_board(
                    board_ip,
                    unique_broadcasts(args.broadcast),
                    args.port,
                    args.timeout,
                    args.source_ip,
                )
                print(f"Broadcast ping response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
                print(f"  {response}")
            except (socket.timeout, OSError) as ping_exc:
                print(f"Post-discovery ping check failed: {ping_exc}")
                if args.require_ping or args.no_frame:
                    raise ping_exc
                print("Continuing to frame request because discovery already succeeded.")
        except OSError as exc:
            if not is_route_error(exc):
                raise
            print(f"Direct ping to {board_ip} failed after discovery: {exc}")
            print("Trying broadcast ping once; discovery already proved the board can reply.")
            try:
                response, addr, elapsed_ms = try_broadcast_ping_board(
                    board_ip,
                    unique_broadcasts(args.broadcast),
                    args.port,
                    args.timeout,
                    args.source_ip,
                )
                print(f"Broadcast ping response from {addr[0]}:{addr[1]} in {elapsed_ms:.1f} ms")
                print(f"  {response}")
            except (socket.timeout, OSError) as ping_exc:
                print(f"Post-discovery ping check failed: {ping_exc}")
                if args.require_ping or args.no_frame:
                    raise ping_exc
                print("Continuing to frame request because discovery already succeeded.")
        if not args.no_frame:
            if args.single_frame:
                frame_json, jpeg_bytes = request_frame(
                    board_ip,
                    args.port,
                    args.frame_timeout,
                    args.source_ip,
                    unique_broadcasts(args.broadcast),
                    retries=args.frame_retries,
                )
                save_and_draw_frame(frame_json, jpeg_bytes, args.output, args.display)
            else:
                run_opencv_roi_monitor(board_ip, args)
        print("Communication OK")
        return 0
    except RuntimeError as exc:
        print(f"Runtime error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        try:
            import cv2
            cv2.destroyAllWindows()
        except ImportError:
            pass
        print("\nInterrupted.")
        return 130
    except socket.timeout:
        print("Timed out waiting for board response.", file=sys.stderr)
        print("Check that the board is powered, firmware is flashed, Wi-Fi is connected, and the computer is on the same LAN.", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"UDP communication failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
