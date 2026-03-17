import argparse
import os
from pathlib import Path

import cv2


def sanitize_symbol_name(name: str) -> str:
    sanitized = []
    for ch in name:
        if ch.isalnum() or ch == "_":
            sanitized.append(ch)
        else:
            sanitized.append("_")
    result = "".join(sanitized).strip("_")
    return result or "image_data"


def format_cpp_byte_array(data: bytes, values_per_line: int = 12) -> str:
    values = [f"0x{byte:02X}" for byte in data]
    lines = []
    for idx in range(0, len(values), values_per_line):
        lines.append("    " + ", ".join(values[idx:idx + values_per_line]))
    return ",\n".join(lines)


def convert_image_to_cpp(
    input_file: str,
    output_dir: str,
    width: int,
    height: int,
    color_order: str,
    array_name: str | None,
) -> tuple[str, str]:
    image = cv2.imread(input_file, cv2.IMREAD_COLOR)
    if image is None:
        raise ValueError(f"Failed to read image: {input_file}")

    resized = cv2.resize(image, (width, height), interpolation=cv2.INTER_LINEAR)
    if color_order.upper() == "RGB":
        resized = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    elif color_order.upper() != "BGR":
        raise ValueError(f"Unsupported color order: {color_order}")

    image_bytes = resized.tobytes()

    base_name = sanitize_symbol_name(array_name or Path(input_file).stem)
    header_file = os.path.join(output_dir, f"{base_name}.h")
    cpp_file = os.path.join(output_dir, f"{base_name}.cpp")

    os.makedirs(output_dir, exist_ok=True)

    header_guard = f"{base_name.upper()}_H"
    array_len = len(image_bytes)
    data_array = format_cpp_byte_array(image_bytes)

    header_content = f'''#ifndef {header_guard}
#define {header_guard}

#include <stddef.h>
#include <stdint.h>

extern const size_t {base_name}_size;
extern const uint32_t {base_name}_width;
extern const uint32_t {base_name}_height;
extern const uint32_t {base_name}_channels;
extern const uint8_t {base_name}_data[{array_len}];

#endif // {header_guard}
'''

    cpp_content = f'''#include "{os.path.basename(output_dir)}/{base_name}.h"

const size_t {base_name}_size = {array_len};
const uint32_t {base_name}_width = {width};
const uint32_t {base_name}_height = {height};
const uint32_t {base_name}_channels = 3;
const uint8_t {base_name}_data[{array_len}] = {{
{data_array}
}};
'''

    with open(header_file, "w", encoding="utf-8") as file:
        file.write(header_content)

    with open(cpp_file, "w", encoding="utf-8") as file:
        file.write(cpp_content)

    return header_file, cpp_file


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Resize an image and export it as C++ byte-array source files."
    )
    parser.add_argument("input_file", help="Input image path.")
    parser.add_argument("output_dir", help="Directory to save the generated .h/.cpp files.")
    parser.add_argument("--width", type=int, required=True, help="Target image width.")
    parser.add_argument("--height", type=int, required=True, help="Target image height.")
    parser.add_argument(
        "--color-order",
        choices=["RGB", "BGR"],
        default="RGB",
        help="Channel order stored in the generated array.",
    )
    parser.add_argument(
        "--array-name",
        help="Optional C/C++ symbol base name. Defaults to the input file stem.",
    )

    args = parser.parse_args()
    header_file, cpp_file = convert_image_to_cpp(
        input_file=args.input_file,
        output_dir=args.output_dir,
        width=args.width,
        height=args.height,
        color_order=args.color_order,
        array_name=args.array_name,
    )
    print(f"Generated files: {header_file}, {cpp_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
