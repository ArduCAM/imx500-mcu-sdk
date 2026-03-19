from __future__ import annotations

import argparse
import sys
from pathlib import Path

PICO_EXAMPLE_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[5]
if str(PICO_EXAMPLE_DIR) not in sys.path:
    sys.path.insert(0, str(PICO_EXAMPLE_DIR))

from camera_serial_stream_multitask.common.host_runtime import ExampleConfig, add_common_arguments, run_serial_receiver_with_args
from camera_serial_stream_multitask.common.model_info import load_model_info
from camera_serial_stream_multitask.common.renderers import (
    ClassificationRenderer,
    DetectionRenderer,
    PoseRenderer,
    SegmentationRenderer,
)
from camera_serial_stream_multitask.common.spi_output_adapter import adapt_spi_jpeg_metadata_to_networks

MODEL_ROOT = REPO_ROOT / "tools" / "assets" / "models"

CLASSIFICATION_RENDERER = ClassificationRenderer()
DETECTION_RENDERER = DetectionRenderer()
POSE_RENDERER = PoseRenderer()
SEGMENTATION_RENDERER = SegmentationRenderer()


def adapt_metadata_to_networks(parsed_frame):
    return adapt_spi_jpeg_metadata_to_networks(parsed_frame)


def annotate_classification(parsed_frame, args):
    return CLASSIFICATION_RENDERER.render(
        parsed_frame.image_bgr,
        adapt_metadata_to_networks(parsed_frame),
        show_img=args.show_img,
        show_fps=args.show_fps,
    )


def annotate_object_detection(parsed_frame, args):
    return DETECTION_RENDERER.render(
        parsed_frame.image_bgr,
        adapt_metadata_to_networks(parsed_frame),
        score_thr=0.3,
        show_img=args.show_img,
        show_fps=args.show_fps,
    )


def annotate_pose_estimation(parsed_frame, args):
    return POSE_RENDERER.render(
        parsed_frame.image_bgr,
        adapt_metadata_to_networks(parsed_frame),
        score_thr=0.2,
        show_img=args.show_img,
        show_fps=args.show_fps,
    )


def annotate_segmentation(parsed_frame, args):
    return SEGMENTATION_RENDERER.render(
        parsed_frame.image_bgr,
        adapt_metadata_to_networks(parsed_frame),
        alpha=0.45,
        show_img=args.show_img,
        show_fps=args.show_fps,
    )


TASK_CONFIGS = {
    "classification": ExampleConfig(
        task_name="classification",
        model_info=load_model_info(MODEL_ROOT / "mobilenet_v2"),
        default_output_dir="classification_frames",
        default_max_payload=192 * 1024,
        annotate_frame=annotate_classification,
    ),
    "object_detection": ExampleConfig(
        task_name="object_detection",
        model_info=load_model_info(MODEL_ROOT / "ssd_mobilenetv2_fpnlite"),
        default_output_dir="object_detection_frames",
        default_max_payload=256 * 1024,
        annotate_frame=annotate_object_detection,
    ),
    "pose_estimation": ExampleConfig(
        task_name="pose_estimation",
        model_info=load_model_info(MODEL_ROOT / "higherhrnet"),
        default_output_dir="pose_estimation_frames",
        default_max_payload=256 * 1024,
        annotate_frame=annotate_pose_estimation,
    ),
    "segmentation": ExampleConfig(
        task_name="segmentation",
        model_info=load_model_info(MODEL_ROOT / "deeplabv3plus"),
        default_output_dir="segmentation_frames",
        default_max_payload=384 * 1024,
        annotate_frame=annotate_segmentation,
    ),
}


def build_dispatch_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Receive IMX500 packets from Pico2 and parse/save annotated JPEGs for multiple tasks."
    )
    parser.add_argument(
        "--task",
        choices=TASK_CONFIGS.keys(),
        help="Host-side task pipeline. This must match the network already loaded on the IMX500 module.",
    )
    parser.add_argument("--list-tasks", action="store_true", help="List supported task names and exit")
    add_common_arguments(
        parser,
        default_output_dir=None,
        default_max_payload=None,
    )
    return parser


def build_list_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--list-tasks", action="store_true")
    return parser


def print_supported_tasks() -> None:
    for task_name, config in TASK_CONFIGS.items():
        print(
            f"{task_name}: model={config.model_info.name} "
            f"default_output={config.default_output_dir} "
            f"default_max_payload={config.default_max_payload}"
        )


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    list_args, _ = build_list_parser().parse_known_args(argv)
    if list_args.list_tasks:
        print_supported_tasks()
        return 0

    parser = build_dispatch_parser()
    args = parser.parse_args(argv)
    if not args.task:
        parser.error("--task is required unless --list-tasks is used")

    config = TASK_CONFIGS[args.task]
    if args.output is None:
        args.output = config.default_output_dir
    if args.max_payload is None:
        args.max_payload = config.default_max_payload
    if not args.save_original:
        args.save_original = config.save_original

    return run_serial_receiver_with_args(config, args)


if __name__ == "__main__":
    raise SystemExit(main())

