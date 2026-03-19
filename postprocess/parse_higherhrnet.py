import os
import cv2
import time
from .postprocess_highernet import postprocess_higherhrnet
import numpy as np
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

COLORS = np.random.default_rng(3).uniform(0, 255, size=(30, 3))

def draw_keypoints_and_boxes(img, keypoints, scores, boxes, threshold=0.3):
    skeleton = [
        (0, 1), (0, 2), (1, 3), (2, 4),             # head
        (5, 6), (5, 11), (11, 12), (12, 6),         # shoulders and torso
        (5, 7), (7, 9), (6, 8), (8, 10),            # arms
        (11, 13), (13, 15), (12, 14), (14, 16)      # legs
    ]

    h, w = img.shape[:2]
    base_scale = min(h, w) / 640.0
    font_scale = max(0.4, base_scale * 1.2)
    thickness = max(1, int(base_scale * 2))
    kpt_radius = max(2, int(base_scale * 3))
    line_thickness = max(1, int(base_scale * 2))
    font = cv2.FONT_HERSHEY_SIMPLEX

    for idx, (kps, score, box) in enumerate(zip(keypoints, scores, boxes)):
        if score < threshold:
            continue

        y1, x1, y2, x2 = map(int, box)
        color = (COLORS[0] * 255).astype(np.uint8).tolist()
        txt_color = (0, 0, 0) if np.mean(COLORS[0]) > 0.5 else (255, 255, 255)
        text = f'person:{score * 100:.1f}%'

        cv2.rectangle(img, (x1, y1), (x2, y2), color, thickness=thickness)

        txt_size = cv2.getTextSize(text, font, font_scale, thickness)[0]
        txt_bk_color = (COLORS[0] * 255 * 0.7).astype(np.uint8).tolist()
        cv2.rectangle(img,
                      (x1, y1 + 1),
                      (x1 + txt_size[0] + 2, y1 + int(1.5 * txt_size[1])),
                      txt_bk_color,
                      thickness=-1)
        cv2.putText(img, text, (x1, y1 + txt_size[1]), font, font_scale, txt_color, thickness=thickness)

        for x, y, conf in kps:
            if conf > 0.2:
                cv2.circle(img, (int(x), int(y)), kpt_radius, (0, 0, 255), -1)

        for joint_start, joint_end in skeleton:
            if joint_start < len(kps) and joint_end < len(kps):
                x1_, y1_, c1 = kps[joint_start]
                x2_, y2_, c2 = kps[joint_end]
                if c1 > 0.2 and c2 > 0.2:
                    cv2.line(img, (int(x1_), int(y1_)), (int(x2_), int(y2_)), (255, 255, 255), thickness=line_thickness)

    return img

higherhrnet_processed_count = 0
higherhrnet_start_time = time.time()

def parse_higherhrnet(network, img, score_thr=0.5, is_show_input_tensor=False, is_show_img=False, is_print_fps=True, nn_input_map=(0.0, 0.0, 1.0, 1.0)):
    global higherhrnet_processed_count, higherhrnet_start_time

    dnn_output_tensor = network[0].output_tensors[0].data
    if dnn_output_tensor is None:
        logger.warning("Output tensor is None")
        return None, None

    higherhrnet_processed_count += 1
    current_time = time.time()
    elapsed = current_time - higherhrnet_start_time
    if elapsed >= 1.0:
        fps = higherhrnet_processed_count / elapsed
        if is_print_fps:
            logger.debug(f"[FPS]: {fps:.2f}")
        higherhrnet_processed_count = 0
        higherhrnet_start_time = current_time

    np_outputs = [np.expand_dims(x.data, axis=0) for x in network[0].output_tensors]
    keypoints, scores, boxes = postprocess_higherhrnet(
        outputs=np_outputs,
        img_size=(288, 384),
        img_w_pad=(0, 0),
        img_h_pad=(0, 0),
        detection_threshold=0.3,
        network_postprocess=True
    )

    if scores is not None and len(scores) > 0:

        last_keypoints = np.reshape(np.stack(keypoints, axis=0), (len(scores), 17, 3))
        last_boxes = np.array([np.array(b) for b in boxes])
        last_scores = np.array(scores)
        
        img = draw_keypoints_and_boxes(img, last_keypoints, last_scores, last_boxes)

    if is_show_img:
        cv2.imshow("DNN", img)
        cv2.waitKey(1)

    return img, None

        