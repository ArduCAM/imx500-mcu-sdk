from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from typing import Any

import cv2
import numpy as np

from .apParams.fb.FBApParams import FBApParams

INPUT_HEADER_SIZE = 12
JPEG_ALIGNMENT = 1024
JPEG_MIN_SIZE_BYTES = 4 * 1024

DATA_TYPE_MAP: dict[int, dict[int, Any]] = {
    0: {8: np.int8, 16: np.int16, 32: np.int32},
    1: {8: np.uint8, 16: np.uint16, 32: np.uint32},
}


class Dimension:
    def __init__(self) -> None:
        self.id: int = 0
        self.size: int = 0
        self.serialization_index: int = 0
        self.padding: int = 0


class InputTensor:
    def __init__(self) -> None:
        self.id: int = 0
        self.name: str = ""
        self.dimensions: list[Dimension] = []
        self.shift: int = 0
        self.scale: float = 0.0
        self.format: int = 0
        self.data: np.ndarray | None = None

    def get_dimensions(self) -> list[int]:
        return [dim.size for dim in self.dimensions]

    def shape(self) -> tuple[int, ...]:
        return tuple(self.get_dimensions())


class OutputTensor(InputTensor):
    def __init__(self) -> None:
        super().__init__()
        self.bits_per_element: int = 0


class NNContext:
    def __init__(self) -> None:
        self.id: int = 0
        self.name: str = ""
        self.type: str = ""
        self.input_tensors: list[InputTensor] = []
        self.output_tensors: list[OutputTensor] = []

    def _to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "input_tensors": [tensor_to_summary(tensor) for tensor in self.input_tensors],
            "output_tensors": [tensor_to_summary(tensor) for tensor in self.output_tensors],
        }


Network = NNContext


@dataclass
class ParsedFrame:
    payload: bytes
    input_header: dict[str, int]
    output_header: dict[str, int] | None
    jpeg_size: int
    jpeg_aligned_size: int
    jpeg_data: bytes
    image_bgr: np.ndarray
    networks: list[NNContext]

    def to_summary_dict(self) -> dict[str, Any]:
        return {
            "input_header": self.input_header,
            "output_header": self.output_header,
            "jpeg_size": self.jpeg_size,
            "jpeg_aligned_size": self.jpeg_aligned_size,
            "networks": [network._to_dict() for network in self.networks],
        }


def align_up(value: int, base: int) -> int:
    return ((value + base - 1) // base) * base


def checksum32(data: bytes) -> int:
    return sum(data) & 0xFFFFFFFF


def unpack_imx500_header(data: bytes | np.ndarray) -> dict[str, int]:
    result = struct.unpack("BBHHHB", bytes(data[:9]))
    return {
        "valid_flag": int(result[0]),
        "frame_count": int(result[1]),
        "max_length_of_line": int(result[2]),
        "size_of_ap_parameter": int(result[3]),
        "network_ordinal": int(result[4]),
        "indicator": int(result[5]),
    }


def calculate_jpeg_aligned_size(jpeg_size: int) -> int:
    return max(JPEG_MIN_SIZE_BYTES, align_up(jpeg_size + 3, JPEG_ALIGNMENT))


def trim_jpeg_bytes(jpeg_bytes: bytes) -> bytes:
    soi = jpeg_bytes.find(b"\xff\xd8")
    eoi = jpeg_bytes.rfind(b"\xff\xd9")
    if soi >= 0 and eoi >= 0 and eoi > soi:
        return jpeg_bytes[soi : eoi + 2]
    return jpeg_bytes


def parse_spi_jpeg_frame(payload: bytes) -> ParsedFrame:
    data = np.frombuffer(payload, dtype=np.uint8)
    if data.size < INPUT_HEADER_SIZE:
        raise ValueError(f"payload too short: {data.size}")

    input_header = unpack_imx500_header(data)
    ap_param_offset = INPUT_HEADER_SIZE
    ap_param_size = int(input_header["size_of_ap_parameter"])
    ap_param_end = ap_param_offset + ap_param_size
    if ap_param_end + 4 > data.size:
        raise ValueError("ap params or jpeg size field out of range")

    networks = parse_ap_params(data[ap_param_offset:ap_param_end].tobytes())
    if not networks:
        raise ValueError("No network metadata found in payload")
    if len(networks[0].input_tensors) != 1:
        raise RuntimeError(f"Expected 1 input tensor, got {len(networks[0].input_tensors)}")

    jpeg_size = int(np.frombuffer(data[ap_param_end:ap_param_end + 4].tobytes(), dtype=np.uint32)[0])
    jpeg_aligned_size = calculate_jpeg_aligned_size(jpeg_size)
    jpeg_start = ap_param_end + 4
    jpeg_end = jpeg_start + jpeg_aligned_size - 4
    if jpeg_end > data.size:
        raise ValueError(f"jpeg block out of range: need {jpeg_end}, got {data.size}")

    jpeg_data = trim_jpeg_bytes(data[jpeg_start:jpeg_end].tobytes())
    image_bgr = cv2.imdecode(np.frombuffer(jpeg_data, dtype=np.uint8), cv2.IMREAD_COLOR)
    if image_bgr is None:
        raise ValueError("failed to decode JPEG payload")

    bind_input_tensor_image(networks[0].input_tensors[0], image_bgr)

    output_header = None
    output_header_offset = jpeg_end
    if output_header_offset + INPUT_HEADER_SIZE <= data.size:
        output_header = unpack_imx500_header(data[output_header_offset:])
        bind_output_tensors(networks, data[output_header_offset + INPUT_HEADER_SIZE :], output_header)

    return ParsedFrame(
        payload=payload,
        input_header=input_header,
        output_header=output_header,
        jpeg_size=jpeg_size,
        jpeg_aligned_size=jpeg_aligned_size,
        jpeg_data=jpeg_data,
        image_bgr=image_bgr,
        networks=networks,
    )


def bind_input_tensor_image(input_tensor: InputTensor, image_bgr: np.ndarray) -> None:
    dimensions = input_tensor.get_dimensions()
    if len(dimensions) != 3:
        input_tensor.data = None
        return

    input_height, input_width, _input_channel = dimensions
    resized = cv2.resize(image_bgr, (input_width, input_height), interpolation=cv2.INTER_LINEAR)
    input_tensor.data = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)


def bind_output_tensors(
    networks: list[NNContext],
    raw_output_tensor_data: bytes | np.ndarray,
    output_header: dict[str, int],
) -> None:
    if output_header["valid_flag"] != 1:
        for network in networks:
            for output_tensor in network.output_tensors:
                output_tensor.data = None
        return

    if isinstance(raw_output_tensor_data, bytes):
        raw = np.frombuffer(raw_output_tensor_data, dtype=np.uint8)
    else:
        raw = np.ascontiguousarray(raw_output_tensor_data).reshape(-1)

    offset = 0
    for network in networks:
        for output_tensor in network.output_tensors:
            dimensions = output_tensor.get_dimensions()
            data_size = math.prod(dimensions) * math.ceil(output_tensor.bits_per_element / 8)
            if offset + data_size > raw.size:
                raise ValueError(
                    f"Output tensor {output_tensor.name or output_tensor.id} truncated: "
                    f"need {offset + data_size}, got {raw.size}"
                )

            tensor_bytes = raw[offset : offset + data_size].tobytes()
            dtype = DATA_TYPE_MAP[output_tensor.format][output_tensor.bits_per_element]
            output_data = np.frombuffer(tensor_bytes, dtype=dtype).astype(np.float32)
            output_data = (output_data - output_tensor.shift) * output_tensor.scale
            output_tensor.data = reshape_output_tensor(output_data, dimensions)
            offset += align_up(data_size, 4)


def reshape_output_tensor(output_data: np.ndarray, dimensions: list[int]) -> np.ndarray:
    reshaped_dimensions = list(dimensions)
    reshaped_dimensions.reverse()
    output_data = output_data.reshape(reshaped_dimensions)
    return np.transpose(output_data)


def parse_ap_params(data: bytes) -> list[NNContext]:
    ap_parameter = FBApParams.GetRootAsFBApParams(data, 0)
    networks: list[NNContext] = []

    for index in range(ap_parameter.NetworksLength()):
        network_fb = ap_parameter.Networks(index)
        network = NNContext()
        network.id = network_fb.Id()
        network.name = network_fb.Name()
        network.type = network_fb.Type()

        for input_index in range(network_fb.InputTensorsLength()):
            tensor_fb = network_fb.InputTensors(input_index)
            network.input_tensors.append(parse_input_tensor(tensor_fb))

        for output_index in range(network_fb.OutputTensorsLength()):
            tensor_fb = network_fb.OutputTensors(output_index)
            network.output_tensors.append(parse_output_tensor(tensor_fb))

        networks.append(network)

    return networks


def parse_input_tensor(tensor_fb: Any) -> InputTensor:
    tensor = InputTensor()
    tensor.id = tensor_fb.Id()
    tensor.name = tensor_fb.Name()
    tensor.shift = tensor_fb.Shift()
    tensor.scale = tensor_fb.Scale()
    tensor.format = tensor_fb.Format()
    tensor.dimensions = parse_dimensions(tensor_fb)
    return tensor


def parse_output_tensor(tensor_fb: Any) -> OutputTensor:
    tensor = OutputTensor()
    tensor.id = tensor_fb.Id()
    tensor.name = tensor_fb.Name()
    tensor.shift = tensor_fb.Shift()
    tensor.scale = tensor_fb.Scale()
    tensor.format = tensor_fb.Format()
    tensor.bits_per_element = tensor_fb.BitsPerElement()
    tensor.dimensions = parse_dimensions(tensor_fb)
    return tensor


def parse_dimensions(tensor_fb: Any) -> list[Dimension]:
    dimensions: list[Dimension] = []
    for index in range(tensor_fb.DimensionsLength()):
        dim_fb = tensor_fb.Dimensions(index)
        dimension = Dimension()
        dimension.id = dim_fb.Id()
        dimension.size = dim_fb.Size()
        dimension.serialization_index = dim_fb.SerializationIndex()
        dimension.padding = dim_fb.Padding()
        dimensions.append(dimension)
    return dimensions


def tensor_to_summary(tensor: InputTensor) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "id": tensor.id,
        "name": tensor.name,
        "dimensions": [dimension_to_summary(dim) for dim in tensor.dimensions],
        "shift": tensor.shift,
        "scale": tensor.scale,
        "format": tensor.format,
    }
    if isinstance(tensor, OutputTensor):
        summary["bits_per_element"] = tensor.bits_per_element
    if isinstance(tensor.data, np.ndarray):
        summary["data_shape"] = list(tensor.data.shape)
        summary["data_dtype"] = str(tensor.data.dtype)
        summary["data_min"] = float(np.min(tensor.data))
        summary["data_max"] = float(np.max(tensor.data))
    return summary


def dimension_to_summary(dimension: Dimension) -> dict[str, int]:
    return {
        "id": dimension.id,
        "size": dimension.size,
        "serialization_index": dimension.serialization_index,
        "padding": dimension.padding,
    }
