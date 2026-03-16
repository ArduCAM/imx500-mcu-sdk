from __future__ import annotations

import sys
from pathlib import Path

PICO_EXAMPLE_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[5]
if str(PICO_EXAMPLE_DIR) not in sys.path:
    sys.path.insert(0, str(PICO_EXAMPLE_DIR))

from camera_serial_stream_common.host_runtime import ExampleConfig, main_from_config
from camera_serial_stream_common.model_info import load_model_info
from camera_serial_stream_common.renderers import SegmentationRenderer
from metadata_bridge import adapt_metadata_to_networks

MODEL_DIR = REPO_ROOT / "tools" / "assets" / "models" / "deeplabv3plus"
MODEL_INFO = load_model_info(MODEL_DIR)
RENDERER = SegmentationRenderer()


def annotate_frame(parsed_frame, _args):
    return RENDERER.render(parsed_frame.image_bgr, adapt_metadata_to_networks(parsed_frame), alpha=0.45)


CONFIG = ExampleConfig(
    task_name="segmentation",
    model_info=MODEL_INFO,
    default_output_dir="segmentation_frames",
    default_max_payload=384 * 1024,
    annotate_frame=annotate_frame,
)


if __name__ == "__main__":
    raise SystemExit(main_from_config(CONFIG))
