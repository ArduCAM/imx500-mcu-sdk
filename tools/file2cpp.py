import os
import argparse
import struct


def convert_file_to_cpp(input_file, output_dir, byteswap):
    base_name = os.path.splitext(os.path.basename(input_file))[0]
    header_file = os.path.join(output_dir, f"{base_name}.h")
    cpp_file = os.path.join(output_dir, f"{base_name}.cpp")

    with open(input_file, 'rb') as f:
        input_file_data = f.read()

    if byteswap:
        data_length = len(input_file_data)
        if data_length % 4 != 0:
            raise ValueError("File size must be a multiple of 4 for byteswap operation.")
        
        input_file_data = struct.pack(f'<{data_length // 4}I', *struct.unpack(f'>{data_length // 4}I', input_file_data))

    data_array = ', '.join(f'0x{byte:02X}' for byte in input_file_data)

    data_size = len(input_file_data)

    header_content = f"""#ifndef {base_name.upper()}_H
#define {base_name.upper()}_H

#include <stdint.h>
#include <stddef.h>

// File size
extern const size_t {base_name}_size;

// File content
extern const uint8_t {base_name}_data[{data_size}];

#endif // {base_name.upper()}_H
"""

    cpp_content = f"""#include "{os.path.basename(output_dir)}/{base_name}.h"

// File content
const size_t {base_name}_size = {data_size};
const uint8_t {base_name}_data[{data_size}] = {{
    {data_array}
}};
"""

    with open(header_file, 'w') as file:
        file.write(header_content)

    with open(cpp_file, 'w') as file:
        file.write(cpp_content)

    print(f"Generated files: {header_file}, {cpp_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert a binary file to C++ header and source files.")
    parser.add_argument("input_file", help="The input binary file to convert.")
    parser.add_argument("output_dir", help="The directory to save the generated C++ files.")
    parser.add_argument("--byteswap", action="store_true", help="Enable byteswap for uint32 data.")

    args = parser.parse_args()

    convert_file_to_cpp(args.input_file, args.output_dir, args.byteswap)