import numpy as np
import cv2
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


class ParserMobilenetSsd:

    def __init__(self, label_file_path="labels/coco_ssd.txt"):
        self.rng = np.random.default_rng(3)
        self.class_names = self.load_labels(label_file_path)
        self.colors = self.rng.uniform(0, 255, size=(len(self.class_names), 3))

    def load_labels(self, filepath):
        labels = []
        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split(":", 1)
                if len(parts) == 2:
                    label = parts[1].strip()
                else:
                    label = line
                labels.append(label)
        return labels

    def draw_masks(self, image, boxes, class_ids, mask_alpha=0.3, mask_maps=None):
        mask_img = image.copy()

        # Draw bounding boxes and labels of detections
        for i, (box, class_id) in enumerate(zip(boxes, class_ids)):
            color = self.colors[class_id]

            x1, y1, x2, y2 = box.astype(int)

            # Draw fill mask image
            if mask_maps is None:
                cv2.rectangle(mask_img, (x1, y1), (x2, y2), color, -1)
            else:
                crop_mask = mask_maps[i][y1:y2, x1:x2, np.newaxis]
                crop_mask_img = mask_img[y1:y2, x1:x2]
                crop_mask_img = crop_mask_img * (1 - crop_mask) + crop_mask * color
                mask_img[y1:y2, x1:x2] = crop_mask_img

        return cv2.addWeighted(mask_img, mask_alpha, image, 1 - mask_alpha, 0)

    def draw_detections(self, image, boxes, confs, class_ids, mask_alpha=0.3, mask_maps=None):
        img_height, img_width = image.shape[:2]

        base_scale = min(img_height, img_width) / 640
        font_scale = max(0.4, base_scale * 0.7)
        text_thickness = max(1, int(base_scale * 2))
        box_thickness = max(1, int(base_scale * 2))

        mask_img = self.draw_masks(image, boxes, class_ids, mask_alpha, mask_maps)

        for box, score, class_id in zip(boxes, confs, class_ids):
            color = self.colors[class_id]
            x1, y1, x2, y2 = box.astype(int)

            cv2.rectangle(mask_img, (x1, y1), (x2, y2), color, thickness=box_thickness)

            label = self.class_names[class_id]
            caption = f'{label} {int(score * 100)}%'

            (tw, th), _ = cv2.getTextSize(caption, cv2.FONT_HERSHEY_SIMPLEX, font_scale, text_thickness)
            th = int(th * 1.2)

            cv2.rectangle(mask_img, (x1, y1), (x1 + tw, y1 - th), color, -1)
            cv2.putText(mask_img, caption, (x1, y1 - 4),
                        cv2.FONT_HERSHEY_SIMPLEX, font_scale,
                        (255, 255, 255), thickness=text_thickness, lineType=cv2.LINE_AA)

        return mask_img

    def __call__(self, network, img, score_thr=0.3, is_show_input_tensor=False, is_show_img=False, is_print_fps=False, nn_input_map=(0.0, 0.0, 1.0, 1.0)):
        dnn_input_img = network[0].input_tensors[0].data.copy()
        if dnn_input_img is None:
            raise Exception("Input tensor is None")

        if dnn_input_img.shape[0] == 3:
            w, h, c = dnn_input_img.shape[1], dnn_input_img.shape[2], dnn_input_img.shape[0]
            dnn_input_img = dnn_input_img.transpose(2, 0, 1).reshape(c, h, w).transpose(1, 2, 0)
        else:
            w, h, c = dnn_input_img.shape[1], dnn_input_img.shape[0], dnn_input_img.shape[2]

        dnn_input_img = cv2.cvtColor(dnn_input_img, cv2.COLOR_RGB2BGR)
        dnn_output_tensor = network[0].output_tensors[0].data
        if dnn_output_tensor is None:
            logger.warning("Output tensor is None")
            return None, None

        boxes = np.array(network[0].output_tensors[0].data)
        confs = np.array(network[0].output_tensors[1].data)
        cls_ids = np.array(network[0].output_tensors[2].data)
        valid_data_items_num = np.array(network[0].output_tensors[3].data)
        valid_data_items_num = valid_data_items_num.astype(np.int32)[0]

        boxes[:, 1] *= w
        boxes[:, 0] *= h
        boxes[:, 3] *= w
        boxes[:, 2] *= h

        boxes = boxes.astype(np.int32)[:valid_data_items_num, :]
        confs = confs[:valid_data_items_num]
        cls_ids = cls_ids.astype(np.int32)[:valid_data_items_num]

        confs_mask = confs > score_thr
        boxes = boxes[confs_mask, :]
        confs = confs[confs_mask]
        cls_ids = cls_ids[confs_mask]

        img_h, img_w = img.shape[:2]
        xmin, ymin, xmax, ymax = nn_input_map
        xmin_abs = int(xmin * img_w)
        ymin_abs = int(ymin * img_h)
        xmax_abs = int(xmax * img_w)
        ymax_abs = int(ymax * img_h)

        crop_w = xmax_abs - xmin_abs
        crop_h = ymax_abs - ymin_abs

        dnn_input_h, dnn_input_w = dnn_input_img.shape[:2]
        x_scale = crop_w / dnn_input_w
        y_scale = crop_h / dnn_input_h

        boxes_yuv = boxes.astype(np.float32)
        boxes_yuv[:, 0] = boxes_yuv[:, 0] * x_scale + xmin_abs
        boxes_yuv[:, 2] = boxes_yuv[:, 2] * x_scale + xmin_abs
        boxes_yuv[:, 1] = boxes_yuv[:, 1] * y_scale + ymin_abs
        boxes_yuv[:, 3] = boxes_yuv[:, 3] * y_scale + ymin_abs
        boxes_yuv = boxes_yuv.astype(np.int32)

        if len(confs) > 0:
            dnn_input_img = self.draw_detections(image=dnn_input_img, boxes=boxes, confs=confs, class_ids=cls_ids)
            img = self.draw_detections(image=img, boxes=boxes_yuv, confs=confs, class_ids=cls_ids)

        if is_show_input_tensor:
            cv2.imshow("DNN", dnn_input_img)
            cv2.waitKey(1)

        if is_show_img:
            cv2.imshow("YUV_DNN", img)
            cv2.waitKey(1)

        return img, dnn_input_img
