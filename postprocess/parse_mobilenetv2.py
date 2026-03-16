import os
import cv2
import numpy as np
import time
try:
    from logger import logger
except ImportError:
    class _SimpleLogger:
        def debug(self, msg):
            print(f"[DEBUG] {msg}")

        def info(self, msg):
            print(f"[INFO] {msg}")

        def warning(self, msg):
            print(f"[WARN] {msg}")
    logger = _SimpleLogger()


def load_labels(filepath):
    labels = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split(":", 1)
            if len(parts) == 2:
                label = parts[1].strip()
                labels.append(label)
    return labels


class ParserMobilenetv2:

    def __init__(self, label_file_path="labels/imagenet_labels.txt"):
        self.labels = load_labels(label_file_path)

    def split_text_to_lines(self, text, max_line_length=20):
        lines = []
        while len(text) > max_line_length:
            break_index = text.rfind(' ', 0, max_line_length)
            if break_index == -1:
                break_index = max_line_length
            lines.append(text[:break_index])
            text = text[break_index:].lstrip()
        lines.append(text)
        return "\n".join(lines)

    def draw_multiline_text(self, image, text, origin, font_scale, color, thickness, spacing=1.2):
        font = cv2.FONT_HERSHEY_SIMPLEX
        lines = text.splitlines()
        for i, line in enumerate(lines):
            y = origin[1] + int((i + 1) * font_scale * 25 * spacing)
            cv2.putText(image, line, (origin[0], y), font, font_scale, color, thickness, cv2.LINE_AA)

    def __call__(self, network, img, score_thr=0.0, is_show_input_tensor=False, is_show_img=False, is_print_fps=False, nn_input_map=(0.0, 0.0, 1.0, 1.0)):

        dnn_input_img = network[0].input_tensors[0].data.copy()
        if dnn_input_img is None:
            raise Exception("Input tensor is None")

        if dnn_input_img.shape[0] == 3:
            w, h, c = dnn_input_img.shape[1], dnn_input_img.shape[2], dnn_input_img.shape[0]
            dnn_input_img = dnn_input_img.transpose(2, 0, 1).reshape(c, h, w).transpose(1, 2, 0)
        else:
            w, h, c = dnn_input_img.shape[1], dnn_input_img.shape[0], dnn_input_img.shape[2]

        dnn_input_img = cv2.cvtColor(dnn_input_img, cv2.COLOR_RGB2BGR)

        output_scores = network[0].output_tensors[0].data
        if output_scores is None:
            logger.warning("Output tensor is None")
            return None, None

        top_indices = np.argpartition(-output_scores, 1)[:1]
        top_indices = top_indices[np.argsort(-output_scores[top_indices])]

        text_lines = []
        for i, cls_id in enumerate(top_indices):
            score = output_scores[cls_id]
            label = self.labels[cls_id] if cls_id < len(self.labels) else f"Class {cls_id}"
            line = f"{label}: {score:.1f}%"
            text_lines.append(line)
            # logger.debug(f"[TOP{i+1}] {line}")

        display_text = "\n".join([self.split_text_to_lines(line) for line in text_lines])

        h_input, w_input = dnn_input_img.shape[:2]
        h_img, w_img = img.shape[:2]
        font_scale_input = max(0.4, min(h_input, w_input) / 640 * 0.8)
        font_scale_img = max(1.0, min(h_img, w_img) / 640 * 1.5)
        thickness_input = max(1, int(font_scale_input * 2))
        thickness_img = max(1, int(font_scale_img * 2))

        self.draw_multiline_text(dnn_input_img, display_text, (10, 10), font_scale_input, (0, 255, 0), thickness_input)
        self.draw_multiline_text(img, display_text, (10, 10), font_scale_img, (0, 255, 0), thickness_img)

        if is_show_input_tensor:
            cv2.imshow("DNN", dnn_input_img)
            cv2.waitKey(1)
        if is_show_img:
            cv2.imshow("YUV_DNN", img)
            cv2.waitKey(1)

        return img, dnn_input_img
