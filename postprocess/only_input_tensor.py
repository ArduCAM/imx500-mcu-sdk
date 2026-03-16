import os
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

def only_input_tensor(network, img, score_thr=0.5, is_show_input_tensor=False, is_show_img=False, is_print_fps=False, nn_input_map=(0.0, 0.0, 1.0, 1.0)):

    dnn_input_img = network[0].input_tensors[0].data.copy()
    if dnn_input_img is None:
        raise Exception("Input tensor is None")

    if (dnn_input_img.shape[0] == 3):
        w = dnn_input_img.shape[1]
        h = dnn_input_img.shape[2]
        c = dnn_input_img.shape[0]
        dnn_input_img = dnn_input_img.transpose(2, 0, 1).reshape(c, h, w).transpose(1, 2, 0)
    else:
        w = dnn_input_img.shape[1]
        h = dnn_input_img.shape[0]
        c = dnn_input_img.shape[2]
        
    dnn_input_img = cv2.cvtColor(dnn_input_img, cv2.COLOR_RGB2BGR)
    
    dnn_output_tensor = network[0].output_tensors[0].data
    if dnn_output_tensor is None:
        logger.warning("Output tensor is None")
        return None, None
    
    return img, dnn_input_img