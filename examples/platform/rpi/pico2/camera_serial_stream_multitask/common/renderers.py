from __future__ import annotations

import time

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


def _short_label(label: str) -> str:
    label = label.strip()
    if not label or label == "-":
        return ""
    primary = label.split(",", 1)[0].strip()
    return primary or label


def _format_score_percent(score: float) -> str:
    value = float(score)
    if not np.isfinite(value):
        value = 0.0
    if value <= 1.0 + 1e-6:
        value *= 100.0
    return f"{value:.1f}%"


def _truncate_text(text: str, max_chars: int) -> str:
    if len(text) <= max_chars:
        return text
    return f"{text[: max_chars - 3].rstrip()}..."


def _draw_info_panel(
    image: np.ndarray,
    lines: list[tuple[str, tuple[int, int, int]]],
    *,
    origin: tuple[int, int],
    font_scale: float,
    thickness: int,
    padding: int = 10,
    line_gap: int = 8,
    panel_color: tuple[int, int, int] = (24, 24, 24),
    panel_alpha: float = 0.72,
) -> None:
    if not lines:
        return

    metrics = [
        cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)
        for text, _color in lines
    ]
    text_width = max(size[0][0] for size in metrics)
    text_height = sum(size[0][1] for size in metrics)
    baseline = max(size[1] for size in metrics)
    total_height = text_height + line_gap * (len(lines) - 1) + baseline

    x, y = origin
    x1 = max(0, x)
    y1 = max(0, y)
    x2 = min(image.shape[1] - 1, x1 + text_width + padding * 2)
    y2 = min(image.shape[0] - 1, y1 + total_height + padding * 2)

    overlay = image.copy()
    cv2.rectangle(overlay, (x1, y1), (x2, y2), panel_color, thickness=-1)
    cv2.addWeighted(overlay, panel_alpha, image, 1.0 - panel_alpha, 0.0, image)

    cursor_y = y1 + padding
    for (text, color), (size, text_baseline) in zip(lines, metrics):
        cursor_y += size[1]
        cv2.putText(
            image,
            text,
            (x1 + padding, cursor_y),
            cv2.FONT_HERSHEY_SIMPLEX,
            font_scale,
            color,
            thickness,
            cv2.LINE_AA,
        )
        cursor_y += line_gap + text_baseline


def _draw_text_tag(
    image: np.ndarray,
    text: str,
    *,
    anchor: tuple[int, int],
    font_scale: float,
    thickness: int,
    bg_color: tuple[int, int, int],
) -> None:
    (text_w, text_h), baseline = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)
    pad_x = max(6, int(font_scale * 10))
    pad_y = max(5, int(font_scale * 8))
    x, y = anchor

    x1 = max(0, x)
    y2 = max(text_h + baseline + pad_y * 2, y)
    y1 = max(0, y2 - text_h - baseline - pad_y * 2)
    x2 = min(image.shape[1] - 1, x1 + text_w + pad_x * 2)
    y2 = min(image.shape[0] - 1, y2)

    luminance = 0.299 * bg_color[2] + 0.587 * bg_color[1] + 0.114 * bg_color[0]
    text_color = (16, 16, 16) if luminance > 150 else (245, 245, 245)

    cv2.rectangle(image, (x1, y1), (x2, y2), bg_color, thickness=-1)
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


class _RendererBase:
    def __init__(self, window_name: str) -> None:
        self.window_name = window_name
        self._processed_count = 0
        self._fps_start_time = time.time()

    def _finalize_render(self, annotated: np.ndarray, *, show_img: bool, show_fps: bool) -> np.ndarray:
        self._processed_count += 1
        now = time.time()
        elapsed = now - self._fps_start_time
        if elapsed >= 1.0:
            if show_fps:
                fps = self._processed_count / elapsed
                print(f"[FPS] {self.window_name}: {fps:.2f}")
            self._processed_count = 0
            self._fps_start_time = now

        if show_img:
            cv2.imshow(self.window_name, annotated)
            cv2.waitKey(1)
        return annotated


class ClassificationRenderer(_RendererBase):
    def __init__(self, label_file: str | None = None) -> None:
        super().__init__("Classification")
        self.labels = _load_labels(label_file)

    def render(
        self,
        image_bgr: np.ndarray,
        networks: list[Network],
        top_k: int = 3,
        show_img: bool = False,
        show_fps: bool = False,
    ) -> np.ndarray:
        scores = np.asarray(networks[0].output_tensors[0].data).reshape(-1)
        top_k = max(1, min(top_k, scores.size))
        top_indices = np.argpartition(-scores, top_k - 1)[:top_k]
        top_indices = top_indices[np.argsort(-scores[top_indices])]

        annotated = image_bgr.copy()
        font_scale = max(0.42, min(annotated.shape[:2]) / 1280.0)
        thickness = max(1, int(font_scale * 2))
        label_offset = 0
        if len(self.labels) == scores.size + 1 and self.labels[0].strip().lower() == "background":
            label_offset = 1

        lines: list[tuple[str, tuple[int, int, int]]] = []
        for rank, class_id in enumerate(top_indices, start=1):
            label_index = int(class_id) + label_offset
            label = self.labels[label_index] if label_index < len(self.labels) else f"Class {class_id}"
            label = _short_label(label) or f"Class {class_id}"
            score_text = _format_score_percent(scores[class_id])
            if rank == 1:
                text = f"Top1 {_truncate_text(label, 20)} {score_text}"
                color = (120, 255, 120)
            else:
                text = f"{rank}. {_truncate_text(label, 16)} {score_text}"
                color = (230, 230, 230)
            lines.append((text, color))

        _draw_info_panel(
            annotated,
            lines,
            origin=(10, 10),
            font_scale=font_scale,
            thickness=thickness,
            padding=6,
            line_gap=4,
            panel_color=(20, 20, 20),
            panel_alpha=0.52,
        )
        return self._finalize_render(annotated, show_img=show_img, show_fps=show_fps)


class DetectionRenderer(_RendererBase):
    def __init__(self, label_file: str | None = None) -> None:
        super().__init__("Detection")
        self.labels = _load_labels(label_file)
        self.rng = np.random.default_rng(3)
        self.colors = self.rng.uniform(0, 255, size=(max(1, len(self.labels) or 91), 3))

    def render(
        self,
        image_bgr: np.ndarray,
        networks: list[Network],
        score_thr: float = 0.3,
        show_img: bool = False,
        show_fps: bool = False,
    ) -> np.ndarray:
        network = networks[0]
        boxes = np.asarray(network.output_tensors[0].data, dtype=np.float32)
        scores = np.asarray(network.output_tensors[1].data, dtype=np.float32).reshape(-1)
        class_ids = np.rint(np.asarray(network.output_tensors[2].data, dtype=np.float32).reshape(-1)).astype(np.int32)
        valid_count = int(np.asarray(network.output_tensors[3].data).reshape(-1)[0])

        boxes = boxes[:valid_count].copy()
        scores = scores[:valid_count]
        class_ids = class_ids[:valid_count]
        keep = scores > score_thr
        boxes = boxes[keep]
        class_ids = class_ids[keep]
        scores = scores[keep]
        if self.labels and class_ids.size and np.min(class_ids) >= 1 and np.max(class_ids) >= len(self.labels):
            class_ids = class_ids - 1

        input_h, input_w = network.input_tensors[0].data.shape[:2]
        image_h, image_w = image_bgr.shape[:2]
        boxes[:, 1] *= input_w
        boxes[:, 0] *= input_h
        boxes[:, 3] *= input_w
        boxes[:, 2] *= input_h
        boxes[:, [1, 3]] *= image_w / input_w
        boxes[:, [0, 2]] *= image_h / input_h

        annotated = image_bgr.copy()
        font_scale = max(0.5, min(annotated.shape[:2]) / 960.0)
        thickness = max(1, int(font_scale * 2))
        order = np.argsort(-scores)
        boxes = boxes[order].astype(np.int32)
        class_ids = class_ids[order]
        scores = scores[order]

        if scores.size:
            _draw_info_panel(
                annotated,
                [(f"Detections > {_format_score_percent(score_thr)}: {scores.size}", (255, 255, 255))],
                origin=(16, 16),
                font_scale=max(0.45, font_scale * 0.9),
                thickness=thickness,
                padding=8,
                line_gap=4,
                panel_color=(18, 18, 18),
                panel_alpha=0.6,
            )

        for box, score, class_id in zip(boxes, scores, class_ids):
            y1, x1, y2, x2 = box
            x1 = int(np.clip(x1, 0, image_w - 1))
            x2 = int(np.clip(x2, 0, image_w - 1))
            y1 = int(np.clip(y1, 0, image_h - 1))
            y2 = int(np.clip(y2, 0, image_h - 1))
            if x2 <= x1 or y2 <= y1:
                continue
            color = self.colors[int(class_id) % len(self.colors)].tolist()
            color = tuple(int(channel) for channel in color)
            label = self.labels[class_id] if 0 <= class_id < len(self.labels) else f"Class {class_id}"
            label = _short_label(label) or f"Class {class_id}"
            caption = _truncate_text(f"{label} {_format_score_percent(score)}", 30)
            cv2.rectangle(annotated, (x1, y1), (x2, y2), color, thickness)
            tag_bottom = y1 - 4 if y1 > 28 else min(image_h - 1, y1 + int(28 * font_scale) + 10)
            _draw_text_tag(
                annotated,
                caption,
                anchor=(x1, tag_bottom),
                font_scale=font_scale,
                thickness=thickness,
                bg_color=color,
            )
        return self._finalize_render(annotated, show_img=show_img, show_fps=show_fps)


class SegmentationRenderer(_RendererBase):
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

    def __init__(self) -> None:
        super().__init__("Segmentation")

    def render(
        self,
        image_bgr: np.ndarray,
        networks: list[Network],
        alpha: float = 0.45,
        show_img: bool = False,
        show_fps: bool = False,
    ) -> np.ndarray:
        mask = np.asarray(networks[0].output_tensors[0].data, dtype=np.int32)
        if mask.ndim == 3 and mask.shape[2] == 1:
            mask = mask[:, :, 0]
        mask = cv2.resize(mask.astype(np.uint8), (image_bgr.shape[1], image_bgr.shape[0]), interpolation=cv2.INTER_NEAREST)
        overlay = np.zeros_like(image_bgr)
        for class_id in np.unique(mask):
            if class_id == 0:
                continue
            overlay[mask == class_id] = self.COLOURS[class_id % len(self.COLOURS)]
        annotated = cv2.addWeighted(overlay, alpha, image_bgr, 1.0 - alpha, 0.0)
        return self._finalize_render(annotated, show_img=show_img, show_fps=show_fps)


class PoseRenderer(_RendererBase):
    SKELETON = [
        (0, 1), (0, 2), (1, 3), (2, 4),
        (5, 6), (5, 11), (11, 12), (12, 6),
        (5, 7), (7, 9), (6, 8), (8, 10),
        (11, 13), (13, 15), (12, 14), (14, 16),
    ]
    DEFAULT_JOINT_ORDER = [0, 1, 2, 3, 4, 5, 6, 11, 12, 7, 8, 9, 10, 13, 14, 15, 16]

    def __init__(self) -> None:
        super().__init__("Pose")
        self._munkres = None
        self._box_color = tuple(int(channel) for channel in np.random.default_rng(3).uniform(0, 255, size=3))

    def _load_munkres(self):
        if self._munkres is not None:
            return self._munkres
        try:
            from munkres import Munkres
        except ImportError as exc:
            raise RuntimeError(
                "Pose rendering requires `munkres`. Install it first, for example: pip install munkres"
            ) from exc
        self._munkres = Munkres
        return self._munkres

    def _py_max_match(self, scores: np.ndarray) -> np.ndarray:
        munkres_cls = self._load_munkres()
        return np.asarray(munkres_cls().compute(scores), dtype=np.int32)

    def _match_by_tag(
        self,
        tag_k: np.ndarray,
        loc_k: np.ndarray,
        val_k: np.ndarray,
        detection_threshold: float,
        max_num_people: int,
        tag_threshold: float,
    ) -> np.ndarray:
        default_person = np.zeros((17, 4), dtype=np.float32)
        joint_dict: dict[float, np.ndarray] = {}
        tag_dict: dict[float, list[np.ndarray]] = {}

        for idx in self.DEFAULT_JOINT_ORDER:
            tags = tag_k[:, idx : idx + 1]
            joints = np.concatenate((loc_k[:, idx, :], val_k[:, idx : idx + 1], tags), axis=1)
            mask = joints[:, 2] > detection_threshold
            tags = tags[mask]
            joints = joints[mask]
            if joints.shape[0] == 0:
                continue

            if not joint_dict:
                for tag, joint in zip(tags, joints):
                    key = float(tag[0])
                    joint_dict.setdefault(key, np.copy(default_person))[idx] = joint
                    tag_dict[key] = [tag]
                continue

            grouped_keys = list(joint_dict.keys())[:max_num_people]
            grouped_tags = [np.mean(tag_dict[key], axis=0) for key in grouped_keys]
            diff = joints[:, None, 3:] - np.asarray(grouped_tags)[None, :, :]
            diff_normed = np.linalg.norm(diff, ord=2, axis=2)
            diff_saved = np.copy(diff_normed)
            diff_normed = np.round(diff_normed) * 100 - joints[:, 2:3]

            num_added = diff.shape[0]
            num_grouped = diff.shape[1]
            if num_added > num_grouped:
                diff_normed = np.concatenate(
                    (diff_normed, np.full((num_added, num_added - num_grouped), 1e10, dtype=diff_normed.dtype)),
                    axis=1,
                )

            pairs = self._py_max_match(diff_normed)
            for row, col in pairs:
                if row < num_added and col < num_grouped and diff_saved[row, col] < tag_threshold:
                    key = grouped_keys[col]
                    joint_dict[key][idx] = joints[row]
                    tag_dict[key].append(tags[row])
                else:
                    key = float(tags[row][0])
                    joint_dict.setdefault(key, np.copy(default_person))[idx] = joints[row]
                    tag_dict[key] = [tags[row]]

        return np.asarray([joint_dict[key] for key in joint_dict], dtype=np.float32)

    def _parse_network_postprocess_outputs(
        self,
        outputs: list[np.ndarray],
        output_shape: tuple[int, int],
        detection_threshold: float,
        max_num_people: int,
        tag_threshold: float,
    ) -> tuple[np.ndarray, list[float]]:
        tag_k, ind_k, val_k = outputs
        x = ind_k % output_shape[1]
        y = (ind_k / output_shape[1]).astype(ind_k.dtype)
        loc_k = np.stack([x, y], axis=2)
        people = self._match_by_tag(
            tag_k=tag_k,
            loc_k=loc_k,
            val_k=val_k,
            detection_threshold=detection_threshold,
            max_num_people=max_num_people,
            tag_threshold=tag_threshold,
        )
        if people.size == 0:
            return people, []
        scores = [float(person[:, 2].mean()) for person in people]
        return people, scores

    def _process_keypoints(self, keypoints: np.ndarray) -> np.ndarray:
        processed = keypoints.copy()
        if keypoints[:, 2].max() > 0:
            for i in range(keypoints.shape[0]):
                processed[i][0:3] = [
                    float(keypoints[i][0]),
                    float(keypoints[i][1]),
                    float(keypoints[i][2]),
                ]
        return processed

    def _decode_pose_outputs(
        self,
        network: Network,
        image_shape: tuple[int, int],
        detection_threshold: float,
        tag_threshold: float,
        max_num_people: int,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        output_shape = (144, 192)
        np_outputs = [np.expand_dims(np.asarray(tensor.data, dtype=np.float32), axis=0) for tensor in network.output_tensors]
        grouped, scores = self._parse_network_postprocess_outputs(
            outputs=[np_outputs[0][0, ...], np_outputs[1][0, ...], np_outputs[2][0, ...]],
            output_shape=output_shape,
            detection_threshold=detection_threshold,
            max_num_people=max_num_people,
            tag_threshold=tag_threshold,
        )
        if grouped.size == 0 or not scores:
            return (
                np.empty((0, 17, 3), dtype=np.float32),
                np.empty((0,), dtype=np.float32),
                np.empty((0, 4), dtype=np.float32),
            )

        image_h, image_w = image_shape
        input_h, input_w = network.input_tensors[0].data.shape[:2]

        # The pose decoder returns coordinates in the 144x192 heatmap domain.
        # Map them back to the network input domain first, then to the preview.
        grouped[:, :, 0] *= input_w / float(output_shape[1])
        grouped[:, :, 1] *= input_h / float(output_shape[0])
        if input_w != image_w or input_h != image_h:
            grouped[:, :, 0] *= image_w / float(input_w)
            grouped[:, :, 1] *= image_h / float(input_h)

        keypoints_list: list[np.ndarray] = []
        scores_list: list[float] = []
        boxes_list: list[list[float]] = []
        for person, score in zip(grouped, scores):
            person = self._process_keypoints(person)
            valid_points = person[person[:, 2] > 0]
            if valid_points.size == 0:
                continue
            left_top = np.min(valid_points[:, :2], axis=0)
            right_bottom = np.max(valid_points[:, :2], axis=0)
            keypoints_list.append(person[:, 0:3])
            scores_list.append(float(score))
            boxes_list.append([left_top[1], left_top[0], right_bottom[1], right_bottom[0]])

        if not keypoints_list:
            return (
                np.empty((0, 17, 3), dtype=np.float32),
                np.empty((0,), dtype=np.float32),
                np.empty((0, 4), dtype=np.float32),
            )

        return (
            np.asarray(keypoints_list, dtype=np.float32),
            np.asarray(scores_list, dtype=np.float32),
            np.asarray(boxes_list, dtype=np.float32),
        )

    def _draw_pose(self, image_bgr: np.ndarray, keypoints: np.ndarray, scores: np.ndarray, boxes: np.ndarray, score_thr: float) -> np.ndarray:
        annotated = image_bgr.copy()
        h, w = annotated.shape[:2]
        base_scale = min(h, w) / 640.0
        font_scale = max(0.4, base_scale * 1.2)
        thickness = max(1, int(base_scale * 2))
        kpt_radius = max(2, int(base_scale * 3))
        line_thickness = max(1, int(base_scale * 2))
        txt_color = (255, 255, 255)

        for person_keypoints, person_score, person_box in zip(keypoints, scores, boxes):
            if person_score < score_thr:
                continue

            y1, x1, y2, x2 = map(int, person_box)
            text = f"person:{person_score * 100:.1f}%"
            cv2.rectangle(annotated, (x1, y1), (x2, y2), self._box_color, thickness=thickness)
            txt_size = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)[0]
            cv2.rectangle(
                annotated,
                (x1, y1 + 1),
                (x1 + txt_size[0] + 2, y1 + int(1.5 * txt_size[1])),
                self._box_color,
                thickness=-1,
            )
            cv2.putText(
                annotated,
                text,
                (x1, y1 + txt_size[1]),
                cv2.FONT_HERSHEY_SIMPLEX,
                font_scale,
                txt_color,
                thickness=thickness,
            )

            for x, y, conf in person_keypoints:
                if conf > score_thr:
                    cv2.circle(annotated, (int(x), int(y)), kpt_radius, (0, 0, 255), -1)

            for joint_start, joint_end in self.SKELETON:
                x1_, y1_, c1 = person_keypoints[joint_start]
                x2_, y2_, c2 = person_keypoints[joint_end]
                if c1 > score_thr and c2 > score_thr:
                    cv2.line(
                        annotated,
                        (int(x1_), int(y1_)),
                        (int(x2_), int(y2_)),
                        (255, 255, 255),
                        thickness=line_thickness,
                    )
        return annotated

    def render(
        self,
        image_bgr: np.ndarray,
        networks: list[Network],
        score_thr: float = 0.2,
        show_img: bool = False,
        show_fps: bool = False,
    ) -> np.ndarray:
        keypoints, scores, boxes = self._decode_pose_outputs(
            network=networks[0],
            image_shape=image_bgr.shape[:2],
            detection_threshold=0.3,
            tag_threshold=1.0,
            max_num_people=30,
        )
        annotated = self._draw_pose(image_bgr, keypoints, scores, boxes, score_thr=score_thr)
        return self._finalize_render(annotated, show_img=show_img, show_fps=show_fps)


