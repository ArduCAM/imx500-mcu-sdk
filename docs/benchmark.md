# IMX500 Model Benchmark

This page summarizes the observed frame rates for the benchmarked models.
All values are frames per second (FPS).

## Results

| Model | Task | MIPI image | SPI: JPEG input tensor + output tensor | SPI: output tensor only |
| --- | --- | ---: | ---: | ---: |
| YOLOv8n | Object detection | 30 | 3.5 | 15 |
| YOLOv8n-pose | Pose estimation | 30 | 3.5 | 10 |
| YOLOv8n-cls | Image classification | 30 | 2.13 | 30 |
| YOLOv8n-seg | Instance segmentation | 30 | 3.3 | 7.5 |
| SSD MobileNet | Object detection | 30 | 3.3 | 15 |
| HigherHRNet | Pose estimation | 30 | 2.45 | 4 |
| MobileNetV2 | Image classification | 30 | 6.5 | 30 |
| DeepLabV3+ | Semantic segmentation | 30 | 2 | 4.46 |

## Metric Definitions

- **MIPI image** is the image frame rate delivered through the MIPI image path.
- **SPI: JPEG input tensor + output tensor** is the frame rate when SPI metadata
  includes both the JPEG input tensor and the model output tensor.
- **SPI: output tensor only** is the frame rate when SPI metadata includes only
  the model output tensor.

MIPI image FPS and SPI tensor FPS describe different data paths. Use the MIPI
result to evaluate image delivery and the SPI result that matches the metadata
mode used by your application.

## Reproducing SPI Measurements

The [Pico 2 inference FPS benchmark](../examples/platform/rpi/pico2/inference_fps_benchmark/README.md)
measures inference-frame delivery without parsing, forwarding, or retaining the
metadata payload. It supports both SPI modes shown above and documents its build
options, defaults, and reported metrics.
