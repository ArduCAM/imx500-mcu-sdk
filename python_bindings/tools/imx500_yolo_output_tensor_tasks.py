#!/usr/bin/env python3
"""Load bundled YOLO-series IMX500 models and print output tensor data."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Any


BINDINGS_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = BINDINGS_ROOT.parent
PYTHON_DIR = BINDINGS_ROOT / "python"
MODEL_ROOT = SDK_ROOT / "tools" / "assets" / "models"
LABEL_ROOT = SDK_ROOT / "labels"
PICO_EXAMPLE_DIR = SDK_ROOT / "examples" / "platform" / "rpi" / "pico2"

DEFAULT_BAUDRATE = 115200
DEFAULT_TIMEOUT = 60.0
METADATA_SIZE_POLL_INTERVAL_SEC = 0.05
DEFAULT_PREVIEW_MAX_BYTES = 2 * 1024 * 1024
PREVIEW_TASKS = {"classification", "object_detection", "pose_estimation", "segmentation"}
YOLOV8_SEG_DEFAULT_MASK_THRESHOLD = 0.5
YOLOV8_SEG_DEFAULT_ALPHA = 0.45
YOLOV8_SEG_FALLBACK_PROTO_CHANNELS = 32
YOLOV8_POSE_DEFAULT_KEYPOINT_THRESHOLD = 0.2
YOLOV8_POSE_SKELETON = (
    (0, 1), (1, 3), (0, 2), (2, 4),
    (5, 7), (7, 9), (6, 8), (8, 10),
    (5, 6), (5, 11), (6, 12), (11, 12),
    (12, 14), (14, 16), (11, 13), (13, 15),
)
YOLOV8_SEG_COLOURS = (
    (255, 83, 73),
    (64, 180, 255),
    (90, 220, 120),
    (210, 120, 255),
    (80, 210, 210),
    (230, 170, 80),
    (170, 120, 255),
    (120, 220, 200),
    (255, 120, 170),
    (160, 230, 100),
)


@dataclass(frozen=True)
class TaskModel:
    name: str
    model_dir_name: str
    label_file_name: str | None = None

    @property
    def model_dir(self) -> Path:
        return MODEL_ROOT / self.model_dir_name

    @property
    def label_file(self) -> Path | None:
        if self.label_file_name is None:
            return None
        return LABEL_ROOT / self.label_file_name


TASK_MODELS = {
    "classification": TaskModel("classification", "yolov8n-cls", "imagenet_labels.txt"),
    "object_detection": TaskModel("object_detection", "yolov8n", "coco.txt"),
    "pose_estimation": TaskModel("pose_estimation", "yolov8n-pose"),
    "segmentation": TaskModel("segmentation", "yolov8n-seg", "coco.txt"),
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
                "Failed to import imx500_mcu_sdk. Install or build the binding first:\n"
                "  python3 -m pip install -e . --no-build-isolation\n"
                "or:\n"
                "  python3 setup.py build_ext --inplace"
            ) from local_exc

    if not hasattr(imx500_mcu_sdk, "parse_metadata"):
        raise SystemExit(
            "imx500_mcu_sdk.parse_metadata() is not available. Rebuild the binding:\n"
            "  python3 setup.py build_ext --inplace\n"
            "or reinstall editable mode:\n"
            "  python3 -m pip install -e . --no-build-isolation"
        )
    return imx500_mcu_sdk


def load_numpy() -> Any | None:
    try:
        import numpy as np
    except ImportError:
        return None
    return np


def load_preview_modules() -> dict[str, Any]:
    if str(PICO_EXAMPLE_DIR) not in sys.path:
        sys.path.insert(0, str(PICO_EXAMPLE_DIR))

    try:
        import cv2
        from camera_serial_stream_multitask.common import metadata_parser
        from camera_serial_stream_multitask.common.renderers import (
            ClassificationRenderer,
        )
    except ImportError as exc:
        raise SystemExit(
            "--preview requires OpenCV, NumPy, flatbuffers, and the bundled "
            "camera_serial_stream_multitask helpers. Install missing packages, for example:\n"
            "  python3 -m pip install opencv-python numpy flatbuffers"
        ) from exc

    return {
        "cv2": cv2,
        "metadata_parser": metadata_parser,
        "classification_renderer": ClassificationRenderer(str(LABEL_ROOT / "imagenet_labels.txt")),
        "detection_labels": load_labels(LABEL_ROOT / "coco.txt"),
        "segmentation_labels": load_labels(LABEL_ROOT / "coco.txt"),
    }


def relative_path(path: Path) -> str:
    try:
        return str(path.relative_to(SDK_ROOT))
    except ValueError:
        return str(path)


def read_nonempty(path: Path, label: str) -> bytes:
    data = path.read_bytes()
    if not data:
        raise RuntimeError(f"{label} file is empty: {path}")
    return data


def load_labels(path: Path | None) -> list[str]:
    if path is None or not path.is_file():
        return []
    labels: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(":", 1)
        labels.append(parts[1].strip() if len(parts) == 2 else line)
    return labels


def short_label(label: str) -> str:
    label = label.strip()
    if not label or label == "-":
        return ""
    primary = label.split(",", 1)[0].strip()
    return primary or label


def label_for_class(labels: list[str], class_id: int) -> str:
    if 0 <= class_id < len(labels):
        return short_label(labels[class_id]) or f"class {class_id}"
    return f"class {class_id}"


def format_score_percent(score: float) -> str:
    value = float(score)
    if value <= 1.5:
        value *= 100.0
    return f"{value:.1f}%"


def find_model_file(model_dir: Path) -> Path:
    files = sorted(model_dir.glob("*.fpk"))
    if not files:
        raise RuntimeError(f"no .fpk model found in {model_dir}")
    if len(files) > 1:
        names = ", ".join(path.name for path in files)
        raise RuntimeError(f"multiple .fpk models found in {model_dir}: {names}")
    return files[0]


def load_network_info_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def align_up(value: int, base: int) -> int:
    return ((value + base - 1) // base) * base


def estimate_metadata_read_size(network_info_path: Path) -> int:
    values = load_network_info_values(network_info_path)
    ap_param_size = int(values.get("apParamSize", "0"))
    tensor_count = int(values.get("outputTensorNum", "0"))
    output_bytes = 0
    for index in range(1, tensor_count + 1):
        elements = int(values.get(f"outputTensorDimSize{index}", "0"))
        bytes_per_element = int(values.get(f"outputTensorBytesPerElement{index}", "1"))
        output_bytes += align_up(elements * bytes_per_element, 4)
    return 12 + ap_param_size + output_bytes + 4096


def metadata_read_size(
    imx500_mcu_sdk: ModuleType,
    fallback_size: int,
    max_bytes: int,
    timeout: float,
) -> int:
    if max_bytes > 0:
        return max_bytes

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        size = imx500_mcu_sdk.get_metadata_size()
        if size > 0:
            return max(size, fallback_size)
        time.sleep(METADATA_SIZE_POLL_INTERVAL_SEC)

    return fallback_size


def read_parsed_metadata(
    imx500_mcu_sdk: ModuleType,
    read_size: int,
    preview_len: int,
    retries: int,
) -> tuple[bytes, dict, int]:
    buffer = bytearray(read_size)
    spi_format = imx500_mcu_sdk.SpiDataFormat.METADATA_OUTPUT_TENSOR
    for attempt in range(1, retries + 1):
        n = imx500_mcu_sdk.read_metadata(buffer)
        if n <= 0:
            print(f"  frame attempt {attempt}: empty metadata frame", flush=True)
            continue
        frame = bytes(memoryview(buffer)[:n])
        parsed = imx500_mcu_sdk.parse_metadata(
            frame,
            length=n,
            spi_format=spi_format,
            preview_len=preview_len,
        )
        if parsed is not None:
            return frame, parsed, attempt
        print(f"  frame attempt {attempt}: parse_metadata returned None", flush=True)

    raise RuntimeError(f"failed to parse metadata after {retries} frame attempts")


def tensor_dimensions(tensor: dict) -> list[int]:
    return [int(dim.get("size", 0)) for dim in tensor.get("dimensions", ())]


def dims_text(tensor: dict) -> str:
    dims = tensor_dimensions(tensor)
    return "[" + " x ".join(str(dim) for dim in dims) + "]" if dims else "[]"


def signed_value(value: int, bits: int) -> int:
    sign_bit = 1 << (bits - 1)
    if value & sign_bit:
        value -= 1 << bits
    return value


def read_le(data: bytes, offset: int, byte_count: int) -> int:
    value = 0
    for index in range(byte_count):
        value |= data[offset + index] << (8 * index)
    return value


def tensor_raw_bytes(frame: bytes, tensor: dict) -> bytes:
    offset = int(tensor.get("data_offset", 0))
    data_bytes = int(tensor.get("data_bytes", 0))
    if offset < 0 or data_bytes < 0 or offset + data_bytes > len(frame):
        raise RuntimeError(
            f"tensor {tensor.get('name') or tensor.get('id')} points outside the metadata frame: "
            f"offset={offset} bytes={data_bytes} frame={len(frame)}"
        )
    return frame[offset : offset + data_bytes]


def decode_tensor_preview(raw: bytes, tensor: dict, limit: int) -> tuple[list[int], list[float]]:
    bits = int(tensor.get("bits_per_element", 0))
    if bits not in (8, 16, 32):
        return [], []
    byte_count = (bits + 7) // 8
    count = min(int(tensor.get("element_count", 0)), len(raw) // byte_count, limit)
    signed = int(tensor.get("format", 0)) == 0
    zero_point = int(tensor.get("zero_point", 0))
    scale = float(tensor.get("scale", 1.0))

    raw_values: list[int] = []
    values: list[float] = []
    for index in range(count):
        raw_value = read_le(raw, index * byte_count, byte_count)
        if signed:
            raw_value = signed_value(raw_value, bits)
        raw_values.append(raw_value)
        values.append((raw_value - zero_point) * scale)
    return raw_values, values


def tensor_to_array(frame: bytes, tensor: dict, np: Any) -> Any:
    raw = tensor_raw_bytes(frame, tensor)
    bits = int(tensor.get("bits_per_element", 0))
    signed = int(tensor.get("format", 0)) == 0
    dtype_map = {
        (True, 8): "i1",
        (True, 16): "<i2",
        (True, 32): "<i4",
        (False, 8): "u1",
        (False, 16): "<u2",
        (False, 32): "<u4",
    }
    dtype = dtype_map.get((signed, bits))
    if dtype is None:
        return np.asarray([], dtype=np.float32)

    count = min(int(tensor.get("element_count", 0)), len(raw) // ((bits + 7) // 8))
    array = np.frombuffer(raw, dtype=np.dtype(dtype), count=count).astype(np.float32)
    array = (array - int(tensor.get("zero_point", 0))) * float(tensor.get("scale", 1.0))

    dims = tensor_dimensions(tensor)
    expected = 1
    for dim in dims:
        expected *= dim
    if dims and expected == array.size:
        reshaped_dims = list(dims)
        reshaped_dims.reverse()
        array = np.transpose(array.reshape(reshaped_dims))
    return array


def reshape_rows(array: Any, columns: int, np: Any) -> Any:
    array = np.asarray(array, dtype=np.float32).squeeze()
    if array.size == 0:
        return np.empty((0, columns), dtype=np.float32)
    if array.ndim >= 2 and array.shape[-1] == columns:
        return array.reshape(-1, columns)
    if array.ndim >= 2 and array.shape[0] == columns:
        return np.moveaxis(array, 0, -1).reshape(-1, columns)
    return array.reshape(-1, columns)


def decode_yolo_detection_arrays(
    boxes_data: Any,
    scores_data: Any,
    class_ids_data: Any,
    valid_count_data: Any,
    args: argparse.Namespace,
    np: Any,
) -> dict[str, Any]:
    """Decode the IMX500 YOLOv8 post-processing tensors.

    This follows ``examples/postprocess/parse_yolov8n_det.py``: the first
    tensor stores absolute ``x1, y1, x2, y2`` coordinates in DNN-input pixels;
    the fourth tensor provides the number of valid rows.
    """
    boxes = reshape_rows(boxes_data, 4, np)
    scores = np.asarray(scores_data, dtype=np.float32).reshape(-1)
    class_ids = np.rint(np.asarray(class_ids_data, dtype=np.float32).reshape(-1)).astype(np.int32)
    valid_items = np.asarray(valid_count_data, dtype=np.float32).reshape(-1)
    valid_count = int(valid_items[0]) if valid_items.size else boxes.shape[0]
    valid_count = max(0, min(valid_count, boxes.shape[0], scores.size, class_ids.size))

    boxes = np.nan_to_num(boxes[:valid_count], nan=0.0, posinf=0.0, neginf=0.0).astype(np.int32)
    scores = np.nan_to_num(scores[:valid_count], nan=0.0, posinf=0.0, neginf=0.0)
    class_ids = class_ids[:valid_count]
    keep = scores > args.score_threshold
    boxes = boxes[keep]
    scores = scores[keep]
    class_ids = class_ids[keep]
    return {
        "boxes": boxes,
        "scores": scores,
        "class_ids": class_ids,
    }


def decode_yolo_pose_arrays(
    boxes_data: Any,
    scores_data: Any,
    class_ids_data: Any,
    keypoints_data: Any,
    _args: argparse.Namespace,
    np: Any,
) -> dict[str, Any]:
    """Read the YOLOv8 pose tensors in the layout used by the reference parser.

    ``parse_yolov8n_pos.py`` consumes the post-processing tensors directly as
    ``boxes[N, 4]``, ``scores[N]``, ``class_ids[N]``, and
    ``keypoints[N, 51]``.  Keep that row association intact here rather than
    transposing or otherwise inferring a keypoint layout.
    """
    boxes = np.asarray(boxes_data, dtype=np.float32).squeeze()
    scores = np.asarray(scores_data, dtype=np.float32).reshape(-1)
    class_ids = np.rint(np.asarray(class_ids_data, dtype=np.float32).reshape(-1)).astype(np.int32)
    keypoints = np.asarray(keypoints_data, dtype=np.float32).squeeze()

    if boxes.ndim == 1 and boxes.size == 4:
        boxes = boxes.reshape(1, 4)
    if keypoints.ndim == 1 and keypoints.size == 51:
        keypoints = keypoints.reshape(1, 51)

    if boxes.ndim != 2 or boxes.shape[1] != 4:
        raise ValueError(f"YOLOv8 pose boxes must have shape [N, 4], got {boxes.shape}")
    if keypoints.ndim != 2 or keypoints.shape[1] != 51:
        raise ValueError(f"YOLOv8 pose keypoints must have shape [N, 51], got {keypoints.shape}")
    count = min(boxes.shape[0], scores.size, class_ids.size, keypoints.shape[0])

    return {
        "boxes": boxes[:count],
        "scores": scores[:count],
        "class_ids": class_ids[:count],
        "keypoints": keypoints[:count],
    }


def reshape_yolo_seg_mask_coefficients(array: Any, detection_count: int, np: Any) -> Any:
    array = np.asarray(array, dtype=np.float32).squeeze()
    if array.size == 0 or detection_count <= 0:
        return np.empty((0, 0), dtype=np.float32)

    if array.ndim >= 2:
        if array.shape[0] == detection_count:
            return array.reshape(detection_count, -1)
        if array.shape[-1] == detection_count:
            return np.moveaxis(array, -1, 0).reshape(detection_count, -1)
        if array.shape[-1] <= 128 and array.size % array.shape[-1] == 0:
            rows = array.reshape(-1, array.shape[-1])
            if rows.shape[0] >= detection_count:
                return rows[:detection_count]
        if array.shape[0] <= 128 and array.size % array.shape[0] == 0:
            rows = np.moveaxis(array, 0, -1).reshape(-1, array.shape[0])
            if rows.shape[0] >= detection_count:
                return rows[:detection_count]

    if array.size % detection_count != 0:
        return np.empty((0, 0), dtype=np.float32)
    return array.reshape(detection_count, array.size // detection_count)


def reshape_yolo_seg_proto(array: Any, channel_count: int, np: Any) -> Any:
    array = np.asarray(array, dtype=np.float32).squeeze()
    if array.size == 0:
        return np.empty((0, 0, 0), dtype=np.float32)

    channel_count = channel_count or YOLOV8_SEG_FALLBACK_PROTO_CHANNELS
    if array.ndim == 3:
        matching_axes = [axis for axis, size in enumerate(array.shape) if int(size) == channel_count]
        if matching_axes:
            return np.moveaxis(array, matching_axes[0], 0).astype(np.float32, copy=False)
        channel_axis = int(np.argmin(array.shape))
        return np.moveaxis(array, channel_axis, 0).astype(np.float32, copy=False)

    if array.ndim == 2 and channel_count in array.shape:
        channel_axis = 0 if array.shape[0] == channel_count else 1
        flat = np.moveaxis(array, channel_axis, 0).reshape(channel_count, -1)
        side = int(round(float(flat.shape[1]) ** 0.5))
        if side * side == flat.shape[1]:
            return flat.reshape(channel_count, side, side)
        return flat.reshape(channel_count, 1, flat.shape[1])

    if array.size % channel_count != 0:
        return np.empty((0, 0, 0), dtype=np.float32)
    pixels = array.size // channel_count
    side = int(round(float(pixels) ** 0.5))
    if side * side == pixels:
        return array.reshape(channel_count, side, side)
    return array.reshape(channel_count, 1, pixels)


def sigmoid(array: Any, np: Any) -> Any:
    return 1.0 / (1.0 + np.exp(-np.clip(array, -80.0, 80.0)))


def decode_yolo_segmentation_arrays(
    boxes_data: Any,
    scores_data: Any,
    class_ids_data: Any,
    mask_coefficients_data: Any,
    proto_data: Any,
    args: argparse.Namespace,
    np: Any,
) -> dict[str, Any]:
    boxes = reshape_rows(boxes_data, 4, np)
    scores = np.asarray(scores_data, dtype=np.float32).reshape(-1)
    class_ids = np.rint(np.asarray(class_ids_data, dtype=np.float32).reshape(-1)).astype(np.int32)
    count = min(boxes.shape[0], scores.size, class_ids.size)
    coefficients = reshape_yolo_seg_mask_coefficients(mask_coefficients_data, count, np)
    count = min(count, coefficients.shape[0])

    boxes = np.nan_to_num(boxes[:count], nan=0.0, posinf=0.0, neginf=0.0)
    scores = np.nan_to_num(scores[:count], nan=0.0, posinf=0.0, neginf=0.0)
    class_ids = class_ids[:count]
    coefficients = np.nan_to_num(coefficients[:count], nan=0.0, posinf=0.0, neginf=0.0)

    keep = scores >= args.score_threshold
    boxes = boxes[keep]
    scores = scores[keep]
    class_ids = class_ids[keep]
    coefficients = coefficients[keep]

    order = np.argsort(-scores)
    boxes = boxes[order]
    scores = scores[order]
    class_ids = class_ids[order]
    coefficients = coefficients[order]

    coefficient_channels = coefficients.shape[1] if coefficients.ndim == 2 and coefficients.size else 0
    proto = reshape_yolo_seg_proto(proto_data, coefficient_channels, np)
    if proto.size and coefficients.size:
        channels = min(coefficients.shape[1], proto.shape[0])
        coefficients = coefficients[:, :channels]
        proto = proto[:channels]

    return {
        "boxes": boxes,
        "scores": scores,
        "class_ids": class_ids,
        "coefficients": coefficients,
        "proto": proto,
    }


def selected_network_dict(parsed: dict) -> dict:
    networks = parsed.get("networks", ())
    selected = int(parsed.get("selected_network_index", 0))
    if not networks:
        return {}
    if selected < 0 or selected >= len(networks):
        selected = 0
    return networks[selected]


def print_classification_summary(frame: bytes, network: dict, labels: list[str], args: argparse.Namespace, np: Any) -> None:
    outputs = network.get("output_tensors", ())
    if not outputs:
        return
    scores = np.asarray(tensor_to_array(frame, outputs[0], np), dtype=np.float32).reshape(-1)
    if scores.size == 0:
        return
    top_k = max(1, min(args.top_k, scores.size))
    top_indices = np.argpartition(-scores, top_k - 1)[:top_k]
    top_indices = top_indices[np.argsort(-scores[top_indices])]
    print("  postprocess classification:", flush=True)
    for rank, class_id in enumerate(top_indices, start=1):
        class_id = int(class_id)
        print(
            f"    top{rank}: id={class_id} label={label_for_class(labels, class_id)} "
            f"score={format_score_percent(float(scores[class_id]))}",
            flush=True,
        )


def print_detection_summary(frame: bytes, network: dict, labels: list[str], args: argparse.Namespace, np: Any) -> None:
    outputs = network.get("output_tensors", ())
    if len(outputs) < 4:
        return
    decoded = decode_yolo_detection_arrays(
        tensor_to_array(frame, outputs[0], np),
        tensor_to_array(frame, outputs[1], np),
        tensor_to_array(frame, outputs[2], np),
        tensor_to_array(frame, outputs[3], np),
        args,
        np,
    )
    boxes = decoded["boxes"]
    scores = decoded["scores"]
    class_ids = decoded["class_ids"]

    print(
        f"  postprocess detection: {scores.size} boxes >= {format_score_percent(args.score_threshold)}",
        flush=True,
    )
    for index, (box, score, class_id) in enumerate(zip(boxes, scores, class_ids)):
        if index >= args.max_detections:
            break
        x1, y1, x2, y2 = [float(value) for value in box]
        class_id = int(class_id)
        print(
            f"    det[{index}] id={class_id} label={label_for_class(labels, class_id)} "
            f"score={format_score_percent(float(score))} xyxy=({x1:.1f},{y1:.1f},{x2:.1f},{y2:.1f})",
            flush=True,
        )


def print_pose_summary(frame: bytes, network: dict, args: argparse.Namespace, np: Any) -> None:
    outputs = network.get("output_tensors", ())
    if len(outputs) < 4:
        return
    decoded = decode_yolo_pose_arrays(
        tensor_to_array(frame, outputs[0], np),
        tensor_to_array(frame, outputs[1], np),
        tensor_to_array(frame, outputs[2], np),
        tensor_to_array(frame, outputs[3], np),
        args,
        np,
    )
    boxes = decoded["boxes"]
    scores = decoded["scores"]
    keypoints = decoded["keypoints"]
    keep = scores >= args.score_threshold
    boxes = boxes[keep]
    scores = scores[keep]
    keypoints = keypoints[keep]

    print(
        f"  postprocess pose: {scores.size} people >= {format_score_percent(args.score_threshold)}",
        flush=True,
    )
    for index, (box, score, person_keypoints) in enumerate(zip(boxes, scores, keypoints)):
        if index >= args.max_detections:
            break
        visible = int(np.count_nonzero(person_keypoints.reshape(17, 3)[:, 2] >= args.keypoint_threshold))
        x, y, w, h = [float(value) for value in box]
        print(
            f"    pose[{index}] score={format_score_percent(float(score))} "
            f"visible_keypoints={visible}/17 xywh=({x:.1f},{y:.1f},{w:.1f},{h:.1f})",
            flush=True,
        )


def print_segmentation_summary(frame: bytes, network: dict, labels: list[str], args: argparse.Namespace, np: Any) -> None:
    outputs = network.get("output_tensors", ())
    if len(outputs) < 5:
        return

    decoded = decode_yolo_segmentation_arrays(
        tensor_to_array(frame, outputs[0], np),
        tensor_to_array(frame, outputs[1], np),
        tensor_to_array(frame, outputs[2], np),
        tensor_to_array(frame, outputs[3], np),
        tensor_to_array(frame, outputs[4], np),
        args,
        np,
    )
    boxes = decoded["boxes"]
    scores = decoded["scores"]
    class_ids = decoded["class_ids"]
    proto = decoded["proto"]
    proto_text = "none"
    if getattr(proto, "ndim", 0) == 3 and proto.size:
        proto_text = "%dx%dx%d" % (int(proto.shape[1]), int(proto.shape[2]), int(proto.shape[0]))

    print(
        f"  postprocess segmentation: {scores.size} instances >= "
        f"{format_score_percent(args.score_threshold)} proto={proto_text}",
        flush=True,
    )
    for index, (box, score, class_id) in enumerate(zip(boxes, scores, class_ids)):
        if index >= args.max_detections:
            break
        x1, y1, x2, y2 = [float(value) for value in box]
        class_id = int(class_id)
        print(
            f"    seg[{index}] id={class_id} label={label_for_class(labels, class_id)} "
            f"score={format_score_percent(float(score))} xyxy=({x1:.1f},{y1:.1f},{x2:.1f},{y2:.1f})",
            flush=True,
        )


def print_ai_postprocess_summary(frame: bytes, parsed: dict, task: TaskModel, args: argparse.Namespace) -> None:
    if task.name not in PREVIEW_TASKS:
        return
    np = load_numpy()
    if np is None:
        print("  postprocess skipped: NumPy is not installed", flush=True)
        return
    network = selected_network_dict(parsed)
    if not network:
        return
    labels = load_labels(task.label_file)
    if task.name == "classification":
        print_classification_summary(frame, network, labels, args, np)
    elif task.name == "object_detection":
        print_detection_summary(frame, network, labels, args, np)
    elif task.name == "pose_estimation":
        print_pose_summary(frame, network, args, np)
    elif task.name == "segmentation":
        print_segmentation_summary(frame, network, labels, args, np)


def format_float_list(values: list[float]) -> str:
    return "[" + ", ".join(f"{value:.6g}" for value in values) + "]"


def format_int_list(values: list[int]) -> str:
    return "[" + ", ".join(str(value) for value in values) + "]"


def format_name(tensor: dict) -> str:
    return "signed" if int(tensor.get("format", 0)) == 0 else "unsigned"


def save_tensor_raw(
    save_dir: Path | None,
    task_name: str,
    frame_index: int,
    network_index: int,
    tensor_index: int,
    raw: bytes,
) -> Path | None:
    if save_dir is None:
        return None
    save_dir.mkdir(parents=True, exist_ok=True)
    path = save_dir / f"{task_name}_frame{frame_index:03d}_net{network_index}_out{tensor_index}.bin"
    path.write_bytes(raw)
    return path


def print_tensor(
    frame: bytes,
    task_name: str,
    frame_index: int,
    network_index: int,
    tensor_index: int,
    tensor: dict,
    args: argparse.Namespace,
) -> None:
    raw = tensor_raw_bytes(frame, tensor)
    raw_values, dequantized = decode_tensor_preview(raw, tensor, args.tensor_preview_elements)
    raw_preview = raw[: args.raw_preview_bytes].hex(" ")
    saved_path = save_tensor_raw(
        args.save_raw_dir,
        task_name,
        frame_index,
        network_index,
        tensor_index,
        raw,
    )

    print(
        "    output[%d] id=%s name=%s dims=%s elements=%d bytes=%d aligned=%d off=%d "
        "format=%s/%d zero_point=%s scale=%.9g"
        % (
            tensor_index,
            tensor.get("id"),
            tensor.get("name"),
            dims_text(tensor),
            int(tensor.get("element_count", 0)),
            int(tensor.get("data_bytes", 0)),
            int(tensor.get("aligned_data_bytes", 0)),
            int(tensor.get("data_offset", 0)),
            format_name(tensor),
            int(tensor.get("bits_per_element", 0)),
            tensor.get("zero_point"),
            float(tensor.get("scale", 0.0)),
        ),
        flush=True,
    )
    if raw_preview:
        print(f"      raw[0:{min(len(raw), args.raw_preview_bytes)}]: {raw_preview}", flush=True)
    if raw_values:
        print(f"      quantized[0:{len(raw_values)}]: {format_int_list(raw_values)}", flush=True)
        print(f"      dequantized[0:{len(dequantized)}]: {format_float_list(dequantized)}", flush=True)
    if saved_path is not None:
        print(f"      saved raw tensor: {relative_path(saved_path)}", flush=True)


def print_metadata_frame(
    frame: bytes,
    parsed: dict,
    task_name: str,
    frame_index: int,
    read_attempts: int,
    args: argparse.Namespace,
) -> None:
    header = parsed.get("primary_header") or {}
    print(
        "  frame[%d] bytes=%d attempts=%d frame_count=%s valid=%s ap=%s network=%s"
        % (
            frame_index,
            len(frame),
            read_attempts,
            header.get("frame_count"),
            header.get("valid_flag"),
            header.get("size_of_ap_parameter"),
            header.get("network_ordinal"),
        ),
        flush=True,
    )
    print(
        "    ap=[%d,%d) output=[%d,+%d) networks=%d selected=%d"
        % (
            int(parsed.get("ap_param_offset", 0)),
            int(parsed.get("ap_param_end_offset", 0)),
            int(parsed.get("output_payload_offset", 0)),
            int(parsed.get("output_payload_length", 0)),
            int(parsed.get("network_count", 0)),
            int(parsed.get("selected_network_index", 0)),
        ),
        flush=True,
    )

    for network_index, network in enumerate(parsed.get("networks", ())):
        print(
            "  network[%d] id=%s name=%s type=%s"
            % (
                network_index,
                network.get("id"),
                network.get("name"),
                network.get("type"),
            ),
            flush=True,
        )
        for tensor_index, tensor in enumerate(network.get("output_tensors", ())):
            print_tensor(
                frame,
                task_name,
                frame_index,
                network_index,
                tensor_index,
                tensor,
                args,
            )


def read_preview_metadata(
    imx500_mcu_sdk: ModuleType,
    read_size: int,
    retries: int,
    metadata_parser: ModuleType,
) -> tuple[bytes, Any, int]:
    buffer = bytearray(read_size)
    for attempt in range(1, retries + 1):
        n = imx500_mcu_sdk.read_metadata(buffer)
        if n <= 0:
            print(f"  frame attempt {attempt}: empty metadata frame", flush=True)
            continue
        frame = bytes(memoryview(buffer)[:n])
        try:
            parsed_frame = metadata_parser.parse_metadata(
                frame,
                format=0,
            )
        except Exception as exc:  # noqa: BLE001 - user-facing preview reader.
            print(f"  frame attempt {attempt}: preview parse failed: {exc}", flush=True)
            continue
        return frame, parsed_frame, attempt

    raise RuntimeError(f"failed to parse preview metadata after {retries} frame attempts")


def network_input_hw(network: Any) -> tuple[int, int]:
    """Return the DNN input height/width from its descriptor.

    The YOLOv8 pose package describes its input as CHW ``[3, 320, 320]``.
    Use that descriptor before looking at preview-parser image data: the latter
    is a resized display helper and must not be used to infer a CHW model's
    spatial dimensions.
    """
    input_tensor = network.input_tensors[0]
    dims = [int(dim) for dim in input_tensor.get_dimensions()]
    if len(dims) == 3:
        if dims[0] in (1, 3, 4) and dims[1] > 4 and dims[2] > 4:
            return dims[1], dims[2]
        if dims[2] in (1, 3, 4) and dims[0] > 4 and dims[1] > 4:
            return dims[0], dims[1]
    if len(dims) >= 2 and dims[0] > 0 and dims[1] > 0:
        return dims[0], dims[1]

    input_data = getattr(input_tensor, "data", None)
    if input_data is not None and getattr(input_data, "ndim", 0) >= 2:
        if input_data.ndim == 3 and input_data.shape[0] in (1, 3, 4) and input_data.shape[1] > 4:
            return int(input_data.shape[1]), int(input_data.shape[2])
        return int(input_data.shape[0]), int(input_data.shape[1])

    raise ValueError("network input tensor does not provide usable spatial dimensions")


def scale_xyxy_to_image(boxes: Any, network: Any, image_shape: tuple[int, int], np: Any) -> Any:
    boxes = np.asarray(boxes, dtype=np.float32).copy()
    if boxes.size == 0:
        return boxes.reshape(0, 4)

    image_h, image_w = image_shape
    input_h, input_w = network_input_hw(network)

    if float(np.nanmax(np.abs(boxes))) <= 1.5:
        boxes[:, [0, 2]] *= float(input_w)
        boxes[:, [1, 3]] *= float(input_h)

    boxes[:, [0, 2]] *= image_w / float(input_w)
    boxes[:, [1, 3]] *= image_h / float(input_h)
    boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0, image_w - 1)
    boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0, image_h - 1)
    return boxes


def scale_yolov8_detection_xyxy_to_image(
    boxes: Any,
    network: Any,
    image_shape: tuple[int, int],
    np: Any,
) -> Any:
    """Map absolute YOLOv8 DNN-input coordinates onto the preview JPEG.

    The reference parser uses its default ``nn_input_map=(0, 0, 1, 1)`` for
    this model, so the model input covers the full preview image.
    """
    boxes = np.asarray(boxes, dtype=np.float32).copy()
    if boxes.size == 0:
        return boxes.reshape(0, 4)

    image_h, image_w = image_shape
    input_h, input_w = network_input_hw(network)
    boxes[:, [0, 2]] *= image_w / float(input_w)
    boxes[:, [1, 3]] *= image_h / float(input_h)
    boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0, image_w - 1)
    boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0, image_h - 1)
    return boxes


def map_yolov8_pose_to_preview(
    boxes: Any,
    keypoints: Any,
    network: Any,
    image_shape: tuple[int, int],
    np: Any,
) -> tuple[Any, Any]:
    """Map the reference parser's raw pose tensors to preview coordinates.

    This is the full-image ``nn_input_map=(0, 0, 1, 1)`` path from
    ``parse_yolov8n_pos.py``.  Its output[3] is an ``N x 51`` matrix with
    interleaved ``x, y, confidence`` values; x/y are already in DNN-input
    pixels and must not be normalized or clipped.
    """
    boxes_yuv = np.asarray(boxes, dtype=np.float32).copy()
    keypoints_yuv = np.asarray(keypoints, dtype=np.float32).copy()
    if boxes_yuv.size == 0 or keypoints_yuv.size == 0:
        return boxes_yuv.reshape(-1, 4), keypoints_yuv.reshape(-1, 51)

    image_h, image_w = image_shape
    input_h, input_w = network_input_hw(network)
    x_scale = image_w / float(input_w)
    y_scale = image_h / float(input_h)

    boxes_yuv[:, 0] = boxes_yuv[:, 0] * x_scale
    boxes_yuv[:, 2] = boxes_yuv[:, 2] * x_scale
    boxes_yuv[:, 1] = boxes_yuv[:, 1] * y_scale
    boxes_yuv[:, 3] = boxes_yuv[:, 3] * y_scale
    keypoints_yuv[:, 0::3] = keypoints_yuv[:, 0::3] * x_scale
    keypoints_yuv[:, 1::3] = keypoints_yuv[:, 1::3] * y_scale
    return boxes_yuv, keypoints_yuv


def yolo_segmentation_colour(class_id: int) -> tuple[int, int, int]:
    return YOLOV8_SEG_COLOURS[int(class_id) % len(YOLOV8_SEG_COLOURS)]


def create_yolo_segmentation_masks(
    coefficients: Any,
    proto: Any,
    boxes: Any,
    network: Any,
    image_shape: tuple[int, int],
    args: argparse.Namespace,
    modules: dict[str, Any],
    np: Any,
) -> Any:
    cv2 = modules["cv2"]
    image_h, image_w = image_shape
    coefficients = np.asarray(coefficients, dtype=np.float32)
    proto = np.asarray(proto, dtype=np.float32)
    if coefficients.size == 0 or proto.size == 0 or proto.ndim != 3:
        return np.zeros((0, image_h, image_w), dtype=np.bool_)

    channels = min(coefficients.shape[1], proto.shape[0])
    if channels <= 0:
        return np.zeros((0, image_h, image_w), dtype=np.bool_)

    proto = proto[:channels]
    coefficients = coefficients[:, :channels]
    proto_h, proto_w = int(proto.shape[1]), int(proto.shape[2])
    logits = np.matmul(coefficients, proto.reshape(channels, -1))
    probabilities = sigmoid(logits, np).reshape(-1, proto_h, proto_w)
    proto_boxes = scale_xyxy_to_image(boxes[: probabilities.shape[0]], network, (proto_h, proto_w), np)

    masks: list[Any] = []
    for probability, box in zip(probabilities, proto_boxes):
        x1, y1, x2, y2 = [int(round(float(value))) for value in box]
        x1 = max(0, min(proto_w - 1, x1))
        x2 = max(0, min(proto_w - 1, x2))
        y1 = max(0, min(proto_h - 1, y1))
        y2 = max(0, min(proto_h - 1, y2))

        cropped = np.zeros_like(probability, dtype=np.float32)
        if x2 > x1 and y2 > y1:
            cropped[y1 : y2 + 1, x1 : x2 + 1] = probability[y1 : y2 + 1, x1 : x2 + 1]
        resized = cv2.resize(cropped, (image_w, image_h), interpolation=cv2.INTER_LINEAR)
        masks.append(resized >= args.mask_threshold)

    if not masks:
        return np.zeros((0, image_h, image_w), dtype=np.bool_)
    return np.asarray(masks, dtype=np.bool_)


def draw_preview_text_tag(
    image: Any,
    text: str,
    anchor: tuple[int, int],
    color: tuple[int, int, int],
    font_scale: float,
    thickness: int,
    modules: dict[str, Any],
) -> None:
    cv2 = modules["cv2"]
    text = text if len(text) <= 32 else f"{text[:29].rstrip()}..."
    (text_w, text_h), baseline = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)
    pad_x = max(5, int(font_scale * 9))
    pad_y = max(4, int(font_scale * 7))
    x, y = anchor
    x1 = max(0, min(image.shape[1] - 1, x))
    y2 = y - 4
    if y2 < text_h + baseline + pad_y * 2:
        y2 = min(image.shape[0] - 1, y + text_h + baseline + pad_y * 2 + 4)
    y1 = max(0, y2 - text_h - baseline - pad_y * 2)
    x2 = min(image.shape[1] - 1, x1 + text_w + pad_x * 2)
    cv2.rectangle(image, (x1, y1), (x2, y2), color, thickness=-1)
    luminance = 0.299 * color[2] + 0.587 * color[1] + 0.114 * color[0]
    text_color = (16, 16, 16) if luminance > 150 else (245, 245, 245)
    cv2.putText(
        image,
        text,
        (x1 + pad_x, y2 - baseline - pad_y),
        cv2.FONT_HERSHEY_SIMPLEX,
        font_scale,
        text_color,
        thickness,
        cv2.LINE_AA,
    )


def render_yolo_detection_preview(parsed_frame: Any, args: argparse.Namespace, modules: dict[str, Any]) -> Any:
    """Render the IMX500 YOLOv8 detection tensors on the JPEG preview image."""
    np = load_numpy()
    if np is None:
        raise RuntimeError("object-detection preview requires NumPy")

    cv2 = modules["cv2"]
    annotated = parsed_frame.image_bgr.copy()
    network = parsed_frame.networks[0]
    outputs = network.output_tensors
    if len(outputs) < 4:
        return annotated

    decoded = decode_yolo_detection_arrays(
        outputs[0].data,
        outputs[1].data,
        outputs[2].data,
        outputs[3].data,
        args,
        np,
    )
    limit = min(args.max_detections, decoded["scores"].size)
    if limit <= 0:
        return annotated

    boxes = scale_yolov8_detection_xyxy_to_image(decoded["boxes"][:limit], network, annotated.shape[:2], np)
    scores = decoded["scores"][:limit]
    class_ids = decoded["class_ids"][:limit]
    image_h, image_w = annotated.shape[:2]
    base_scale = max(0.5, min(image_h, image_w) / 640.0)
    thickness = max(1, int(base_scale * 2))
    font_scale = max(0.45, base_scale * 0.7)
    labels = modules["detection_labels"]

    for box, score, class_id in zip(boxes, scores, class_ids):
        x1, y1, x2, y2 = [int(round(float(value))) for value in box]
        if x2 <= x1 or y2 <= y1:
            continue
        color = yolo_segmentation_colour(int(class_id))
        cv2.rectangle(annotated, (x1, y1), (x2, y2), color, thickness)
        caption = f"{label_for_class(labels, int(class_id))} {format_score_percent(float(score))}"
        draw_preview_text_tag(annotated, caption, (x1, y1), color, font_scale, thickness, modules)

    return annotated


def render_yolo_segmentation_preview(parsed_frame: Any, args: argparse.Namespace, modules: dict[str, Any]) -> Any:
    np = load_numpy()
    if np is None:
        raise RuntimeError("segmentation preview requires NumPy")
    cv2 = modules["cv2"]
    network = parsed_frame.networks[0]
    outputs = network.output_tensors
    if len(outputs) < 5:
        return parsed_frame.image_bgr.copy()

    decoded = decode_yolo_segmentation_arrays(
        outputs[0].data,
        outputs[1].data,
        outputs[2].data,
        outputs[3].data,
        outputs[4].data,
        args,
        np,
    )
    boxes = decoded["boxes"]
    scores = decoded["scores"]
    class_ids = decoded["class_ids"]
    coefficients = decoded["coefficients"]
    proto = decoded["proto"]

    annotated = parsed_frame.image_bgr.copy()
    limit = min(args.max_detections, scores.size)
    if limit <= 0:
        return annotated

    boxes = boxes[:limit]
    scores = scores[:limit]
    class_ids = class_ids[:limit]
    coefficients = coefficients[:limit]
    boxes_image = scale_xyxy_to_image(boxes, network, annotated.shape[:2], np)
    masks = create_yolo_segmentation_masks(coefficients, proto, boxes, network, annotated.shape[:2], args, modules, np)

    for index in range(min(limit, masks.shape[0]) - 1, -1, -1):
        mask = masks[index]
        if not bool(np.any(mask)):
            continue
        color = np.asarray(yolo_segmentation_colour(int(class_ids[index])), dtype=np.float32)
        annotated[mask] = (annotated[mask].astype(np.float32) * (1.0 - args.mask_alpha) + color * args.mask_alpha).astype(np.uint8)

    h, w = annotated.shape[:2]
    base_scale = max(0.5, min(h, w) / 640.0)
    thickness = max(1, int(base_scale * 2))
    font_scale = max(0.45, base_scale * 0.7)
    labels = modules["segmentation_labels"]

    for box, score, class_id in zip(boxes_image, scores, class_ids):
        x1, y1, x2, y2 = [int(round(float(value))) for value in box]
        if x2 <= x1 or y2 <= y1:
            continue
        color = yolo_segmentation_colour(int(class_id))
        cv2.rectangle(annotated, (x1, y1), (x2, y2), color, thickness)
        label = label_for_class(labels, int(class_id))
        caption = f"{label} {format_score_percent(float(score))}"
        draw_preview_text_tag(annotated, caption, (x1, y1), color, font_scale, thickness, modules)

    return annotated


def render_yolo_pose_preview(parsed_frame: Any, args: argparse.Namespace, modules: dict[str, Any]) -> Any:
    np = load_numpy()
    if np is None:
        raise RuntimeError("pose preview requires NumPy")
    cv2 = modules["cv2"]
    network = parsed_frame.networks[0]
    outputs = network.output_tensors
    if len(outputs) < 4:
        return parsed_frame.image_bgr.copy()

    decoded = decode_yolo_pose_arrays(
        outputs[0].data,
        outputs[1].data,
        outputs[2].data,
        outputs[3].data,
        args,
        np,
    )
    boxes = decoded["boxes"]
    scores = decoded["scores"]
    keypoints = decoded["keypoints"]

    annotated = parsed_frame.image_bgr.copy()
    boxes, keypoints = map_yolov8_pose_to_preview(boxes, keypoints, network, annotated.shape[:2], np)
    h, w = annotated.shape[:2]
    base_scale = max(0.5, min(h, w) / 640.0)
    thickness = max(1, int(base_scale * 2))
    radius = max(2, int(base_scale * 3))
    font_scale = max(0.45, base_scale * 0.7)
    color = (50, 220, 255)

    drawn_people = 0
    for box, score, keypoints_raw in zip(boxes, scores, keypoints):
        if score < args.score_threshold:
            continue
        if drawn_people >= args.max_detections:
            break
        drawn_people += 1
        person_keypoints = keypoints_raw.reshape(17, 3)
        x1, y1, box_w, box_h = [int(value) for value in box]
        x2 = x1 + box_w
        y2 = y1 + box_h
        cv2.rectangle(annotated, (x1, y1), (x2, y2), color, thickness)
        cv2.putText(
            annotated,
            f"person {format_score_percent(float(score))}",
            (x1, max(16, y1 - 6)),
            cv2.FONT_HERSHEY_SIMPLEX,
            font_scale,
            color,
            thickness,
            cv2.LINE_AA,
        )
        for joint_a, joint_b in YOLOV8_POSE_SKELETON:
            xa, ya, ca = person_keypoints[joint_a]
            xb, yb, cb = person_keypoints[joint_b]
            if ca >= args.keypoint_threshold and cb >= args.keypoint_threshold:
                cv2.line(annotated, (int(xa), int(ya)), (int(xb), int(yb)), (255, 255, 255), thickness)
        for x, y, conf in person_keypoints:
            if conf >= args.keypoint_threshold:
                cv2.circle(annotated, (int(x), int(y)), radius, (0, 0, 255), -1)
    return annotated


def save_preview_image(
    image: Any,
    save_dir: Path | None,
    task_name: str,
    frame_index: int,
    modules: dict[str, Any],
) -> Path | None:
    if save_dir is None:
        return None
    save_dir.mkdir(parents=True, exist_ok=True)
    path = save_dir / f"{task_name}_preview_{frame_index:03d}.jpg"
    if not modules["cv2"].imwrite(str(path), image):
        raise RuntimeError(f"failed to save preview image: {path}")
    return path


def render_preview_frame(parsed_frame: Any, task: TaskModel, frame_index: int, args: argparse.Namespace, modules: dict[str, Any]) -> None:
    cv2 = modules["cv2"]
    if task.name == "classification":
        annotated = modules["classification_renderer"].render(
            parsed_frame.image_bgr,
            parsed_frame.networks,
            top_k=args.top_k,
            show_img=False,
            show_fps=args.show_fps,
        )
    elif task.name == "object_detection":
        annotated = render_yolo_detection_preview(parsed_frame, args, modules)
    elif task.name == "pose_estimation":
        annotated = render_yolo_pose_preview(parsed_frame, args, modules)
    elif task.name == "segmentation":
        annotated = render_yolo_segmentation_preview(parsed_frame, args, modules)
    else:
        return

    saved_path = save_preview_image(annotated, args.preview_save_dir, task.name, frame_index, modules)
    if saved_path is not None:
        print(f"  preview saved: {relative_path(saved_path)}", flush=True)
    cv2.imshow(f"IMX500 {task.name}", annotated)
    key = cv2.waitKey(args.preview_wait_ms) & 0xFF
    if key in (ord("q"), 27):
        raise KeyboardInterrupt


def close_preview_windows(args: argparse.Namespace) -> None:
    if not args.preview:
        return
    try:
        import cv2
    except ImportError:
        return
    try:
        cv2.destroyAllWindows()
    except Exception:
        pass


def run_task(imx500_mcu_sdk: ModuleType, task: TaskModel, args: argparse.Namespace) -> None:
    if args.preview and task.name not in PREVIEW_TASKS:
        print(f"\n=== {task.name} ({task.model_dir_name}) ===", flush=True)
        print("  preview skipped: AI preview is implemented for classification, object_detection, pose_estimation, and segmentation", flush=True)
        return

    model_dir = task.model_dir
    model_path = find_model_file(model_dir)
    network_info_path = model_dir / "network_info.txt"
    if not network_info_path.is_file():
        raise RuntimeError(f"network_info.txt not found: {network_info_path}")

    model = read_nonempty(model_path, "model")
    network_info = read_nonempty(network_info_path, "network_info")
    fallback_read_size = estimate_metadata_read_size(network_info_path)

    print(f"\n=== {task.name} ({task.model_dir_name}) ===", flush=True)
    print(f"  model: {relative_path(model_path)} ({len(model)} bytes)", flush=True)
    print(f"  network_info: {relative_path(network_info_path)} ({len(network_info)} bytes)", flush=True)
    spi_format = (
        imx500_mcu_sdk.SpiDataFormat.METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR
        if args.preview
        else imx500_mcu_sdk.SpiDataFormat.METADATA_OUTPUT_TENSOR
    )
    spi_name = "jpeg-output" if args.preview else "output"
    print(f"  calling imx500_mcu_sdk.imx500_open(..., spi={spi_name})", flush=True)

    opened = imx500_mcu_sdk.imx500_open(
        model,
        network_info,
        imx500_mcu_sdk.MipiDataFormat.IMAGE,
        spi_format,
        args.fps,
    )
    print(f"  imx500_open result: {opened}", flush=True)
    if not opened:
        raise RuntimeError("imx500_mcu_sdk.imx500_open() returned false")

    print("  calling imx500_mcu_sdk.stream_on()", flush=True)
    imx500_mcu_sdk.stream_on()

    if args.preview:
        read_size = args.metadata_max_bytes if args.metadata_max_bytes > 0 else max(fallback_read_size, args.preview_max_bytes)
        preview_modules = load_preview_modules()
    else:
        read_size = metadata_read_size(
            imx500_mcu_sdk,
            fallback_read_size,
            args.metadata_max_bytes,
            args.timeout,
        )
        preview_modules = None
    print(f"  metadata read buffer: {read_size} bytes", flush=True)
    if args.preview and args.frames_per_task == 0:
        print("  preview mode: continuous; press q or Esc in the preview window to stop", flush=True)

    frame_index = 0
    while args.frames_per_task == 0 or frame_index < args.frames_per_task:
        if args.preview:
            frame, parsed_frame, attempts = read_preview_metadata(
                imx500_mcu_sdk,
                read_size,
                args.parse_retries,
                preview_modules["metadata_parser"],
            )
            print(
                "  preview frame[%d] bytes=%d attempts=%d jpeg=%d output_payload=%d"
                % (
                    frame_index,
                    len(frame),
                    attempts,
                    int(getattr(parsed_frame, "jpeg_data_len", 0)),
                    int(getattr(parsed_frame, "output_payload_length", 0)),
                ),
                flush=True,
            )
            render_preview_frame(parsed_frame, task, frame_index, args, preview_modules)
        else:
            frame, parsed, attempts = read_parsed_metadata(
                imx500_mcu_sdk,
                read_size,
                args.raw_preview_bytes,
                args.parse_retries,
            )
            print_metadata_frame(frame, parsed, task.name, frame_index, attempts, args)
            print_ai_postprocess_summary(frame, parsed, task, args)
        frame_index += 1


def selected_tasks(task_name: str) -> list[TaskModel]:
    if task_name == "all":
        return list(TASK_MODELS.values())
    return [TASK_MODELS[task_name]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--task",
        choices=["all", *TASK_MODELS.keys()],
        default="all",
        help="YOLO-series task model to load. Default loads the four bundled YOLO task models in sequence.",
    )
    parser.add_argument("--port", help="USB bridge CDC serial port")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument(
        "--frames-per-task",
        type=int,
        default=None,
        help=(
            "Number of parsed metadata frames to read after each model is loaded. "
            "Default is 1, or continuous for --preview with a single task. "
            "Use 0 with --preview to run until q or Esc is pressed."
        ),
    )
    parser.add_argument(
        "--parse-retries",
        type=int,
        default=8,
        help="Maximum metadata frames to read while looking for a parseable output-tensor frame.",
    )
    parser.add_argument(
        "--metadata-max-bytes",
        type=int,
        default=0,
        help="Metadata read buffer capacity. 0 means use SDK size or a model-based fallback.",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        help=(
            "Request JPEG+output metadata and show AI post-processing overlays. "
            "Implemented for classification, object_detection, pose_estimation, and segmentation."
        ),
    )
    parser.add_argument(
        "--preview-max-bytes",
        type=int,
        default=DEFAULT_PREVIEW_MAX_BYTES,
        help="Read buffer size used by --preview when --metadata-max-bytes is 0.",
    )
    parser.add_argument(
        "--preview-save-dir",
        type=Path,
        help="Optional directory for saving annotated --preview JPEG frames.",
    )
    parser.add_argument(
        "--preview-wait-ms",
        type=int,
        default=1,
        help="OpenCV wait time after each preview frame. Press q or Esc to stop.",
    )
    parser.add_argument(
        "--show-fps",
        action="store_true",
        help="Print renderer FPS while previewing multiple frames.",
    )
    parser.add_argument(
        "--top-k",
        type=int,
        default=3,
        help="Number of classification results to show in post-processing.",
    )
    parser.add_argument(
        "--score-threshold",
        type=float,
        default=0.3,
        help="Score threshold for object_detection, pose_estimation, and segmentation post-processing.",
    )
    parser.add_argument(
        "--keypoint-threshold",
        type=float,
        default=YOLOV8_POSE_DEFAULT_KEYPOINT_THRESHOLD,
        help="Keypoint score threshold for pose_estimation preview drawing (default: 0.2).",
    )
    parser.add_argument(
        "--mask-threshold",
        type=float,
        default=YOLOV8_SEG_DEFAULT_MASK_THRESHOLD,
        help="Mask probability threshold for segmentation preview drawing.",
    )
    parser.add_argument(
        "--mask-alpha",
        type=float,
        default=YOLOV8_SEG_DEFAULT_ALPHA,
        help="Segmentation mask overlay opacity for --preview.",
    )
    parser.add_argument(
        "--max-detections",
        type=int,
        default=8,
        help="Maximum detections, people, or segmentation instances to print/draw per frame.",
    )
    parser.add_argument(
        "--raw-preview-bytes",
        type=int,
        default=32,
        help="Number of raw tensor bytes to print per output tensor.",
    )
    parser.add_argument(
        "--tensor-preview-elements",
        type=int,
        default=12,
        help="Number of quantized/dequantized tensor elements to print per output tensor.",
    )
    parser.add_argument(
        "--save-raw-dir",
        type=Path,
        help="Optional directory for saving each output tensor payload as a .bin file.",
    )
    parser.add_argument(
        "--no-probe",
        action="store_true",
        help="Skip imx500_mcu_sdk.probe_imx500_module() before loading models.",
    )
    args = parser.parse_args()
    if args.frames_per_task is None:
        args.frames_per_task = 0 if args.preview and args.task != "all" else 1
    return args


def validate_args(args: argparse.Namespace) -> None:
    if args.timeout <= 0:
        raise SystemExit("--timeout must be > 0")
    if args.fps <= 0:
        raise SystemExit("--fps must be > 0")
    if args.frames_per_task < 0:
        raise SystemExit("--frames-per-task must be >= 0")
    if args.frames_per_task == 0 and not args.preview:
        raise SystemExit("--frames-per-task 0 is only supported with --preview")
    if args.parse_retries <= 0:
        raise SystemExit("--parse-retries must be > 0")
    if args.metadata_max_bytes < 0:
        raise SystemExit("--metadata-max-bytes must be >= 0")
    if args.preview_max_bytes <= 0:
        raise SystemExit("--preview-max-bytes must be > 0")
    if args.preview_wait_ms < 1:
        raise SystemExit("--preview-wait-ms must be >= 1")
    if args.top_k <= 0:
        raise SystemExit("--top-k must be > 0")
    if args.score_threshold < 0:
        raise SystemExit("--score-threshold must be >= 0")
    if args.keypoint_threshold < 0:
        raise SystemExit("--keypoint-threshold must be >= 0")
    if not 0.0 <= args.mask_threshold <= 1.0:
        raise SystemExit("--mask-threshold must be between 0 and 1")
    if not 0.0 <= args.mask_alpha <= 1.0:
        raise SystemExit("--mask-alpha must be between 0 and 1")
    if args.max_detections <= 0:
        raise SystemExit("--max-detections must be > 0")
    if args.raw_preview_bytes < 0:
        raise SystemExit("--raw-preview-bytes must be >= 0")
    if args.tensor_preview_elements < 0:
        raise SystemExit("--tensor-preview-elements must be >= 0")


def main() -> int:
    args = parse_args()
    validate_args(args)
    imx500_mcu_sdk = load_imx500_mcu_sdk()

    print("opening USB bridge...", flush=True)
    bridge = imx500_mcu_sdk.connect_usb_bridge(
        args.port,
        baudrate=args.baudrate,
        timeout=args.timeout,
    )

    try:
        print(f"bridge port: {bridge.port}", flush=True)
        print(f"ping: {bridge.ping()}", flush=True)
        print(f"status: {bridge.status()}", flush=True)

        if not args.no_probe:
            ok, device_id, boot_status = imx500_mcu_sdk.probe_imx500_module()
            print(
                f"probe: ok={ok} device_id=0x{device_id:08x} boot_status={boot_status}",
                flush=True,
            )

        try:
            for task in selected_tasks(args.task):
                run_task(imx500_mcu_sdk, task, args)
        except KeyboardInterrupt:
            print("\nstopped by user", flush=True)

    finally:
        close_preview_windows(args)
        bridge.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
