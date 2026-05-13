# Mission 04: Model Zoo Validation

Use this mission to try a known IMX500 model, inspect its output, and decide
whether it is close enough for your application before converting a custom model.

## Goal

Validate one known model against your application data and produce a clear
feedback artifact: visible demo output, parsed metadata, or a comparison result.

Model zoo entry: [https://github.com/ArduCAM/arducam_imx500_model_zoo](https://github.com/ArduCAM/arducam_imx500_model_zoo)

## Start With

Choose one known model from the model zoo, then pick the fastest validation
route:

| Need | Route |
| --- | --- |
| Quick hardware signal | [USB first-run validation](../paths/usb-validation-to-uvc.md) |
| Visual MCU demo | [ESP32-P4 example](../../examples/platform/esp/esp32p4/README.md) |
| MCU metadata consumption | [SPI metadata path](../paths/spi-mcu-product-path.md) |

## Run

For hardware validation, first confirm the camera path:

```bash
cd python_bindings
PYTHONPATH=python python3 tools/imx500_first_run.py
```

If the model needs to be written to module Flash, use the USB flash tool:

```bash
cd python_bindings
PYTHONPATH=python python3 tools/imx500_usb_flash.py \
  --model path/to/model.fpk \
  --network-info path/to/network_info.txt
```

## Expected Feedback

You should get at least one of these outputs:

- A visible demo output from the selected host or board.
- One metadata frame from real hardware.
- A parsed metadata artifact, such as `parsed_metadata.json`.
- A comparison between expected labels/classes/boxes/keypoints and actual output.

## You Passed This Mission When

- The model task matches the application class, detection, pose, or segmentation
  requirement.
- A known sample produces the expected output shape and labels.
- Real hardware can produce metadata for the selected model.
- The next product path can consume the output format.

## If It Fails

Check:

- The model package and `network_info` match.
- The selected post-processing matches the model task.
- The input image size, color format, and preprocessing assumptions are correct.
- The metadata parser expects the same tensor layout as the model output.
- Lighting, FOV, and object scale match the model's training assumptions.

## Next Unlock

| Result | Next |
| --- | --- |
| Known model works | Continue with [model validation to production](../paths/model-validation-to-production.md) |
| Known model is close but not enough | Continue with [custom model conversion](05-custom-model-conversion.md) |
| Output is correct but host path is undecided | Choose [USB](../paths/usb-validation-to-uvc.md), [MIPI/RPi](../paths/mipi-rpi-product-path.md), or [SPI/MCU](../paths/spi-mcu-product-path.md) |
| Accuracy or conversion is blocked | [Contact custom model support](https://ai.arducam.com/) |
