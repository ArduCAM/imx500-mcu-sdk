from __future__ import annotations

import argparse
from pathlib import Path

FPK_END_PATTERN = bytes([0x33, 0x36, 0x39, 0x35])
NETWORK_INFO_START_PATTERN = b"network_info.txt\x00\x00"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract .fpk and network_info.txt from a .rpk file, then 32-bit endian-swap the .fpk data.",
    )
    parser.add_argument("input_rpk", type=Path, help="Path to the input .rpk file.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Directory for generated files. Defaults to a folder named after the input stem.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing output files.",
    )
    return parser.parse_args()


def extract_fpk_data(data: bytes) -> bytes:
    end_index = data.rfind(FPK_END_PATTERN)
    if end_index == -1:
        raise ValueError("Failed to find the .fpk end pattern in the .rpk file.")
    return data[: end_index + len(FPK_END_PATTERN)]


def extract_network_info_data(data: bytes) -> bytes:
    start_index = data.rfind(NETWORK_INFO_START_PATTERN)
    if start_index == -1:
        raise ValueError("Failed to find the network_info.txt start pattern in the .rpk file.")

    search_from = start_index + len(NETWORK_INFO_START_PATTERN)
    end_index = data.find(b"\x00", search_from)
    if end_index == -1:
        raise ValueError("Failed to find the network_info.txt terminator in the .rpk file.")

    return data[search_from:end_index]


def build_output_paths(input_rpk: Path, output_dir: Path | None) -> tuple[Path, Path]:
    target_dir = output_dir or (input_rpk.parent / input_rpk.stem)
    return target_dir / f"{input_rpk.stem}.fpk", target_dir / "network_info.txt"


def ensure_writable(path: Path, overwrite: bool) -> None:
    if path.exists() and not overwrite:
        raise FileExistsError(f"Output file already exists: {path}")


def swap_32bit_endian(data: bytes) -> bytes:
    if len(data) % 4 != 0:
        raise ValueError("Cannot 32-bit endian-swap .fpk data because its size is not a multiple of 4 bytes.")

    return b"".join(data[index : index + 4][::-1] for index in range(0, len(data), 4))


def convert_rpk(input_rpk: Path, output_dir: Path | None = None, overwrite: bool = False) -> tuple[Path, Path]:
    if not input_rpk.is_file():
        raise FileNotFoundError(f"Input .rpk file not found: {input_rpk}")

    fpk_output, network_info_output = build_output_paths(input_rpk, output_dir)
    fpk_output.parent.mkdir(parents=True, exist_ok=True)

    ensure_writable(fpk_output, overwrite)
    ensure_writable(network_info_output, overwrite)

    data = input_rpk.read_bytes()
    fpk_output.write_bytes(swap_32bit_endian(extract_fpk_data(data)))
    network_info_output.write_bytes(extract_network_info_data(data))
    return fpk_output, network_info_output


def main() -> int:
    args = parse_args()

    try:
        input_rpk = args.input_rpk.resolve()
        output_dir = args.output_dir.resolve() if args.output_dir else None
        fpk_output, network_info_output = convert_rpk(
            input_rpk=input_rpk,
            output_dir=output_dir,
            overwrite=args.overwrite,
        )
    except Exception as exc:
        print(f"Conversion failed: {exc}")
        return 1

    print(f"Generated: {fpk_output}")
    print(f"Generated: {network_info_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
