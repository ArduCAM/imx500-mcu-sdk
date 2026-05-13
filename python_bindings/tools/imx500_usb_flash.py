#!/usr/bin/env python3
"""Flash IMX500 model blobs through the Python MCU SDK USB transport."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from types import ModuleType


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"

DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 60.0
DEFAULT_CHUNK = 1024
PROTO_MAX_CHUNK = 4096
DEFAULT_FINALIZE_TIMEOUT = 180.0

FLASH_OP_NAMES = {
    0: "idle",
    1: "wait_header",
    2: "receiving",
    3: "parsing",
    4: "success",
    5: "failed",
}

FLASH_RESULT_NAMES = {
    0: "none",
    1: "ok",
    2: "timeout",
    3: "bad_header",
    4: "bad_size",
    5: "write_fail",
    6: "crc_mismatch",
    7: "parse_fail",
    8: "flash_blob_missing",
}

SPI_MODE_NAMES = {
    0: "none",
    1: "slave_from_imx500_mspi",
    2: "master_from_imx500_mspi",
    3: "slave_from_imx500_sspi",
    4: "master_from_imx500_sspi",
    5: "slave_to_imx500_sspi",
    6: "write_model_to_flash",
    7: "write_network_info_to_flash",
    8: "load_network_info_to_memory",
    9: "forwarding_mode_switching",
}


def load_imx500_mcu_sdk() -> ModuleType:
    try:
        import imx500_mcu_sdk
    except ImportError as exc:
        if PYTHON_DIR.exists() and str(PYTHON_DIR) not in sys.path:
            sys.path.insert(0, str(PYTHON_DIR))
        try:
            import imx500_mcu_sdk
        except ImportError as local_exc:
            raise SystemExit(
                "Failed to import imx500_mcu_sdk. Install or build the SDK binding first:\n"
                "  python3 -m pip install -e . --no-build-isolation\n"
                "or:\n"
                "  python3 setup.py build_ext --inplace"
            ) from local_exc
    return imx500_mcu_sdk


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Flash IMX500 .fpk and network_info.txt through the Python MCU SDK "
            "registered USB I2C/SPI transport."
        )
    )
    parser.add_argument("--port", help="USB bridge CDC port, for example COM7 or /dev/cu.usbmodemXXXX")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
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
        help=(
            f"Compatibility option from the old CDC flasher. Accepted range is "
            f"1..{PROTO_MAX_CHUNK}; SDK transport manages actual chunking."
        ),
    )
    parser.add_argument(
        "--finalize-timeout",
        type=float,
        default=DEFAULT_FINALIZE_TIMEOUT,
        help=(
            "Compatibility option from the old CDC flasher. The SDK flash API "
            "uses its own internal finalize timeout."
        ),
    )
    parser.add_argument(
        "--status",
        action="store_true",
        help="Only query and print the current SDK flash/bridge status",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.chunk_size <= 0 or args.chunk_size > PROTO_MAX_CHUNK:
        raise SystemExit(f"--chunk-size must be between 1 and {PROTO_MAX_CHUNK}")
    if args.finalize_timeout <= 0:
        raise SystemExit("--finalize-timeout must be > 0")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be > 0")
    if args.model is None and args.network_info is None and not args.status:
        raise SystemExit("Provide --model and/or --network-info, or use --status")


def flash_status_summary(status: dict) -> str:
    op = int(status.get("status", 0))
    result = int(status.get("result", 0))
    bytes_done = int(status.get("bytes_done", 0))
    bytes_total = int(status.get("bytes_total", 0))
    ok = bool(status.get("ok", False))
    return (
        f"ok={ok} "
        f"state={FLASH_OP_NAMES.get(op, op)} "
        f"result={FLASH_RESULT_NAMES.get(result, result)} "
        f"bytes={bytes_done}/{bytes_total}"
    )


def bridge_status_summary(response: object) -> str:
    boot_state = getattr(response, "value0", 0)
    spi_mode = getattr(response, "value1", 0)
    metadata_size = getattr(response, "value2", 0)
    return (
        f"boot_state={boot_state} "
        f"spi_mode={SPI_MODE_NAMES.get(spi_mode, spi_mode)} "
        f"metadata_size={metadata_size}"
    )


def read_blob(path: Path, label: str) -> bytes:
    data = path.read_bytes()
    if not data:
        raise SystemExit(f"{label} file is empty: {path}")
    print(f"{label}: loaded {path} ({len(data)} bytes)", flush=True)
    return data


def print_status(imx500_mcu_sdk: ModuleType, bridge: object) -> None:
    print(f"bridge: {bridge_status_summary(bridge.status())}", flush=True)
    print(
        f"flash: {flash_status_summary(imx500_mcu_sdk.get_spi_flash_status())}",
        flush=True,
    )


def flash_one(
    imx500_mcu_sdk: ModuleType,
    bridge: object,
    label: str,
    path: Path,
    writer_name: str,
) -> bool:
    payload = read_blob(path, label)
    print(
        f"{label}: flashing through imx500_mcu_sdk.{writer_name}() ...",
        flush=True,
    )
    writer = getattr(imx500_mcu_sdk, writer_name)
    ok = bool(writer(payload))
    print(f"{label}: {'done' if ok else 'failed'}", flush=True)
    print_status(imx500_mcu_sdk, bridge)
    return ok


def main() -> int:
    args = parse_args()
    validate_args(args)
    imx500_mcu_sdk = load_imx500_mcu_sdk()

    bridge = imx500_mcu_sdk.connect_usb_bridge(
        args.port,
        baudrate=args.baudrate,
        timeout=args.timeout,
    )
    try:
        print(f"Using port: {bridge.port}", flush=True)
        print(f"Ping: {bridge_status_summary(bridge.ping())}", flush=True)

        if args.status and args.model is None and args.network_info is None:
            print_status(imx500_mcu_sdk, bridge)
            return 0

        success = True
        if args.model is not None:
            success = flash_one(
                imx500_mcu_sdk,
                bridge,
                "model",
                args.model,
                "write_model_to_cam_flash",
            )
            if not success:
                return 1

        if args.network_info is not None:
            success = flash_one(
                imx500_mcu_sdk,
                bridge,
                "network_info",
                args.network_info,
                "write_nn_info_to_cam_flash",
            )
            if not success:
                return 1

        return 0
    finally:
        bridge.close()


if __name__ == "__main__":
    raise SystemExit(main())
