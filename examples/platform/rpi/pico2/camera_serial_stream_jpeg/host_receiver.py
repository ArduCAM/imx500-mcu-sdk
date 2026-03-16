#!/usr/bin/env python3
"""Receive IMX500 packets from Pico2, parse frame payload, decode JPEG, and save images."""

import argparse
import os
import struct
import sys
import time

import cv2
import numpy as np
import serial

from output_parser import IMX500OutputParser

MAGIC = b"IMX5"
HEADER_FMT = "<4sBBHIiI"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
PACKET_TYPE_FRAME = 1

K_JPEG_DATA_PADDING_FACTOR = 1024
K_JPEG_DATA_MINIMUM_SIZE_KB = 4


def parse_args():
    parser = argparse.ArgumentParser(description="Receive IMX500 frame packets and save JPEGs")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM7 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=921600, help="Baudrate (USB CDC may ignore this)")
    parser.add_argument("--output", default="jpeg_frames", help="Output directory for decoded JPEG files")
    parser.add_argument("--save-raw", action="store_true", help="Also save raw payload as .bin")
    parser.add_argument("--max-frames", type=int, default=0, help="Stop after N decoded JPEG frames (0 = no limit)")
    parser.add_argument("--max-payload", type=int, default=131072, help="Reject packet if payload exceeds this")
    return parser.parse_args()


def checksum32(data: bytes) -> int:
    return sum(data) & 0xFFFFFFFF


def calculate_jpeg_input_tensor_size(jpeg_size: int, padding_factor: int, min_size_kb: int) -> int:
    jpeg_size = ((jpeg_size + 4 - 1 + padding_factor) // padding_factor) * padding_factor
    min_size = min_size_kb * 1024
    if jpeg_size < min_size:
        jpeg_size = min_size
    return jpeg_size


def extract_jpeg_bytes(frame_payload: bytes) -> bytes:
    data = np.frombuffer(frame_payload, dtype=np.uint8)

    if data.size < 16:
        raise ValueError("frame too short")

    input_tensor_header = IMX500OutputParser._unpack_header(data)
    image_data_offset = 12 + int(input_tensor_header["size_of_ap_parameter"])

    if image_data_offset + 4 > data.size:
        raise ValueError("invalid image_data_offset")

    jpeg_size = int(np.frombuffer(data[image_data_offset:image_data_offset + 4].tobytes(), dtype=np.uint32)[0])
    jpeg_size = calculate_jpeg_input_tensor_size(
        jpeg_size,
        K_JPEG_DATA_PADDING_FACTOR,
        K_JPEG_DATA_MINIMUM_SIZE_KB,
    )

    jpeg_start = image_data_offset + 4
    jpeg_end = jpeg_start + jpeg_size - 4
    if jpeg_end > data.size:
        raise ValueError(f"jpeg block out of range: need {jpeg_end}, got {data.size}")

    jpeg_data = data[jpeg_start:jpeg_end].tobytes()

    soi = jpeg_data.find(b"\xff\xd8")
    eoi = jpeg_data.rfind(b"\xff\xd9")
    if soi >= 0 and eoi >= 0 and eoi > soi:
        jpeg_data = jpeg_data[soi:eoi + 2]

    return jpeg_data


def main():
    args = parse_args()
    os.makedirs(args.output, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"[INFO] Opened {args.port} at {args.baud}")

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
                start = buf.find(MAGIC)
                if start < 0:
                    if len(buf) > 3:
                        del buf[:-3]
                    break

                if start > 0:
                    del buf[:start]

                if len(buf) < HEADER_SIZE:
                    break

                magic, version, pkt_type, header_len, seq, payload_len, csum = struct.unpack(
                    HEADER_FMT, buf[:HEADER_SIZE]
                )

                if magic != MAGIC or version != 1 or header_len != HEADER_SIZE:
                    del buf[0]
                    continue

                if payload_len < 0:
                    print(f"[WARN] seq={seq} frame read failed on device")
                    del buf[:HEADER_SIZE]
                    continue

                if payload_len > args.max_payload:
                    print(f"[WARN] drop oversized payload: {payload_len}")
                    del buf[0]
                    continue

                full_len = HEADER_SIZE + payload_len
                if len(buf) < full_len:
                    break

                payload = bytes(buf[HEADER_SIZE:full_len])
                del buf[:full_len]

                calc = checksum32(payload)
                if calc != csum:
                    print(f"[WARN] checksum mismatch seq={seq} recv=0x{csum:08X} calc=0x{calc:08X}")
                    continue

                if pkt_type != PACKET_TYPE_FRAME:
                    print(f"[INFO] ignore packet type={pkt_type} seq={seq}")
                    continue

                if args.save_raw:
                    raw_path = os.path.join(args.output, f"frame_{seq:06d}.bin")
                    with open(raw_path, "wb") as f:
                        f.write(payload)

                try:
                    jpeg_data = extract_jpeg_bytes(payload)
                except Exception as exc:
                    print(f"[WARN] seq={seq} jpeg extract failed: {exc}")
                    continue

                img_np = np.frombuffer(jpeg_data, dtype=np.uint8)
                img = cv2.imdecode(img_np, cv2.IMREAD_COLOR)
                if img is None:
                    print(f"[WARN] seq={seq} JPEG decode failed")
                    continue

                out_path = os.path.join(args.output, f"frame_{seq:06d}.jpg")
                ok = cv2.imwrite(out_path, img)
                if not ok:
                    print(f"[WARN] seq={seq} failed to save image")
                    continue

                decoded += 1
                print(f"[OK] seq={seq} -> {out_path} shape={img.shape}")

                if args.max_frames > 0 and decoded >= args.max_frames:
                    print(f"[INFO] reached max-frames={args.max_frames}, exit")
                    return 0
    except KeyboardInterrupt:
        print("\n[INFO] stopped by user")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())