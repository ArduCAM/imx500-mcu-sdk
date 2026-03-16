from __future__ import annotations

import sys
from pathlib import Path

import cv2
import numpy as np

from .metadata_parser import Network


def _load_labels(label_file: str | None) -> list[str]:
    if not label_file:
        return []
    labels: list[str] = []
    with open(label_file, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            parts = line.split(":", 1)
            labels.append(parts[1].strip() if len(parts) == 2 else line)
    return labels


class ClassificationRenderer:
    def __init__(self, label_file: str | None = None) -> None:
        self.labels = _load_labels(label_file)

    def render(self, image_bgr: np.ndarray, networks: list[Network], top_k: int = 3) -> np.ndarray:
        scores = np.asarray(networks[0].output_tensors[0].data).reshape(-1)
        top_k = max(1, min(top_k, scores.size))
        top_indices = np.argpartition(-scores, top_k - 1)[:top_k]
        top_indices = top_indices[np.argsort(-scores[top_indices])]

        annotated = image_bgr.copy()
        font_scale = max(0.6, min(annotated.shape[:2]) / 720.0)
        thickness = max(1, int(font_scale * 2))
        y = 30
        for rank, class_id in enumerate(top_indices, start=1):
            label = self.labels[class_id] if class_id < len(self.labels) else f"Class {class_id}"
            text = f"Top{rank}: {label} {scores[class_id]:.1f}%"
            cv2.putText(
                annotated,
                text,
                (20, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                font_scale,
                (0, 255, 0),
                thickness,
                cv2.LINE_AA,
            )
            y += int(36 * font_scale)
        return annotated


class DetectionRenderer:
    def __init__(self, label_file: str | None = None) -> None:
        self.labels = _load_labels(label_file)
        self.rng = np.random.default_rng(3)
        self.colors = self.rng.uniform(0, 255, size=(max(1, len(self.labels) or 91), 3))

    def render(self, image_bgr: np.ndarray, networks: list[Network], score_thr: float = 0.3) -> np.ndarray:
        network = networks[0]
        boxes = np.asarray(network.output_tensors[0].data, dtype=np.float32)
        scores = np.asarray(network.output_tensors[1].data, dtype=np.float32).reshape(-1)
        class_ids = np.asarray(network.output_tensors[2].data, dtype=np.int32).reshape(-1)
        valid_count = int(np.asarray(network.output_tensors[3].data).reshape(-1)[0])

        boxes = boxes[:valid_count].copy()
        scores = scores[:valid_count]
        class_ids = class_ids[:valid_count]
        keep = scores > score_thr
        boxes = boxes[keep]
        class_ids = class_ids[keep]
        scores = scores[keep]

        input_h, input_w = network.input_tensors[0].data.shape[:2]
        image_h, image_w = image_bgr.shape[:2]
        boxes[:, 1] *= input_w
        boxes[:, 0] *= input_h
        boxes[:, 3] *= input_w
        boxes[:, 2] *= input_h
        boxes[:, [1, 3]] *= image_w / input_w
        boxes[:, [0, 2]] *= image_h / input_h

        annotated = image_bgr.copy()
        font_scale = max(0.5, min(annotated.shape[:2]) / 900.0)
        thickness = max(1, int(font_scale * 2))
        for box, score, class_id in zip(boxes.astype(np.int32), scores, class_ids):
            y1, x1, y2, x2 = box
            color = self.colors[int(class_id) % len(self.colors)].tolist()
            color = tuple(int(channel) for channel in color)
            label = self.labels[class_id] if class_id < len(self.labels) else f"Class {class_id}"
            caption = f"{label} {score * 100:.0f}%"
            cv2.rectangle(annotated, (x1, y1), (x2, y2), color, thickness)
            cv2.putText(
                annotated,
                caption,
                (x1, max(20, y1 - 8)),
                cv2.FONT_HERSHEY_SIMPLEX,
                font_scale,
                color,
                thickness,
                cv2.LINE_AA,
            )
        return annotated


class SegmentationRenderer:
    COLOURS = np.array(
        [
            [0, 0, 0],
            [128, 0, 0],
            [0, 128, 0],
            [128, 128, 0],
            [0, 0, 128],
            [128, 0, 128],
            [0, 128, 128],
            [128, 128, 128],
            [64, 0, 0],
            [192, 0, 0],
            [64, 128, 0],
            [192, 128, 0],
            [64, 0, 128],
            [192, 0, 128],
            [64, 128, 128],
            [192, 128, 128],
            [0, 64, 0],
            [128, 64, 0],
            [0, 192, 0],
            [128, 192, 0],
            [0, 64, 128],
        ],
        dtype=np.uint8,
    )

    def render(self, image_bgr: np.ndarray, networks: list[Network], alpha: float = 0.45) -> np.ndarray:
        mask = np.asarray(networks[0].output_tensors[0].data, dtype=np.int32)
        if mask.ndim == 3 and mask.shape[2] == 1:
            mask = mask[:, :, 0]
        mask = cv2.resize(mask.astype(np.uint8), (image_bgr.shape[1], image_bgr.shape[0]), interpolation=cv2.INTER_NEAREST)
        overlay = np.zeros_like(image_bgr)
        for class_id in np.unique(mask):
            if class_id == 0:
                continue
            overlay[mask == class_id] = self.COLOURS[class_id % len(self.COLOURS)]
        return cv2.addWeighted(overlay, alpha, image_bgr, 1.0 - alpha, 0.0)


class PoseRenderer:
    def __init__(self) -> None:
        self._higherhrnet_parser = None

    def _load_higherhrnet_parser(self):
        if self._higherhrnet_parser is not None:
            return self._higherhrnet_parser

        repo_root = Path(__file__).resolve().parents[5]
        if str(repo_root) not in sys.path:
            sys.path.insert(0, str(repo_root))

        try:
            from postprocess.parse_higherhrnet import parse_higherhrnet
        except ImportError as exc:
            raise RuntimeError(
                "HigherHRNet postprocess dependencies are missing. Install `munkres` first, for example: pip install munkres"
            ) from exc

        self._higherhrnet_parser = parse_higherhrnet
        return self._higherhrnet_parser

    def render(
        self,
        image_bgr: np.ndarray,
        networks: list[Network],
        score_thr: float = 0.2,
        is_show_img: bool = False,
        is_show_input_tensor: bool = False,
        is_print_fps: bool = False,
    ) -> np.ndarray:
        parse_higherhrnet = self._load_higherhrnet_parser()
        rendered_img, _ = parse_higherhrnet(
            networks,
            image_bgr.copy(),
            score_thr=score_thr,
            is_show_input_tensor=is_show_input_tensor,
            is_show_img=is_show_img,
            is_print_fps=is_print_fps,
        )
        if rendered_img is None:
            return image_bgr.copy()
        return rendered_img
