# Mission 06: Train And Deploy A Custom Model

Use this guide when a model-zoo network is not enough and you want to train your
own model, convert it for IMX500, package it, and deploy it to an Arducam B0642
IMX500 AI camera module.

This page uses `YOLOv8n` as the concrete example, but the same checkpoints apply
to other model families that are supported by the IMX500 conversion flow.

## Goal

Produce a matched model package that can be programmed into the camera module
and consumed by this SDK:

```text
training data
    -> trained model
    -> IMX500 conversion / quantization
    -> packaged .rpk
    -> .fpk + network_info.txt
    -> USB flash to B0642
    -> MCU metadata transport
    -> custom post-processing
```

You passed this mission when the camera runs the custom model, metadata frames
are readable, and your host or MCU code turns the output tensors into the
expected application events.

## Best For

- Teams that need a custom object-detection model instead of a bundled model.
- Users training `YOLOv8n` or a similar supported network for B0642.
- Projects that need to validate conversion, packaging, flashing, and
  post-processing before product integration.

## Start With

Before training or conversion, record these model assumptions:

| Item | Why it matters |
| --- | --- |
| Model task | The runtime post-processing must match detection, classification, pose, segmentation, or your custom output. |
| Input size and color format | The converter and application preprocessing must agree. |
| Labels/classes | The renderer, event logic, and validation reports need the same label order. |
| Output tensor layout | The MCU SDK transports metadata; your application still owns tensor decoding. |
| Quantization/calibration images | Representative samples reduce accuracy loss after conversion. |
| Product route | USB, MIPI/Linux, and SPI/MCU paths consume the same model output differently. |

## Train The Model

Train the model with your own dataset using the upstream Ultralytics YOLOv8
workflow:

[https://docs.ultralytics.com/models/yolov8#yolov8-usage-examples](https://docs.ultralytics.com/models/yolov8#yolov8-usage-examples)

Keep these artifacts with the training result:

- The exported model file required by the IMX500 conversion flow.
- The exact input resolution and preprocessing settings.
- The class label file.
- A small validation image/video set that represents the real product scene.

## Quantize And Convert For IMX500

Follow the Sony IMX500 integration notes from Ultralytics:

[https://docs.ultralytics.com/integrations/sony-imx500#benchmarks](https://docs.ultralytics.com/integrations/sony-imx500#benchmarks)

### JPEG Preview Workaround Before Export

If inference results are correct but the JPEG preview generated from the input
tensor is corrupted or scrambled, apply this temporary workaround before
exporting the model through Ultralytics:

1. Open `ultralytics/utils/export/imx.py` in the Python environment used for
   model export.
2. Find the `imxconv-pt` command and remove the
   `--no-input-persistency` argument from its argument list.

Change:

```python
[str(imxconv), "-i", str(onnx_model), "-o", str(output_dir), "--no-input-persistency", "--overwrite-output"]
```

to:

```python
[str(imxconv), "-i", str(onnx_model), "-o", str(output_dir), "--overwrite-output"]
```

See the
[Ultralytics IMX500 export script at the referenced revision](https://github.com/ultralytics/ultralytics/blob/fa9a0a0d962f53164c7b2422239ce63fb34cd691/ultralytics/utils/export/imx.py#L364)
for the original command. Removing this option keeps the input tensor
persistence path enabled, which may restore JPEG generation on affected IMX500
firmware and model-package combinations.

Re-export the model after making the change, then repeat conversion and
packaging with the new output. Verify both the inference result and JPEG
preview. Treat this as a symptom-specific workaround; if the JPEG output is not
corrupted or scrambled, keep the upstream export settings unchanged.

During conversion, check:

- Unsupported operators or layers in the converter log.
- Input shape and preprocessing mismatches.
- Quantization/calibration warnings.
- Output tensor count, dimensions, and element size.

The conversion flow should produce a packer output such as `packerOut.zip`.

## Package The Model

You can package the converted model locally on a Raspberry Pi, or use the
Arducam online conversion service.

### Local Packaging On Raspberry Pi

Install the IMX500 packaging tools:

```bash
sudo apt update
sudo apt install imx500-all imx500-tools
```

Package the converter output:

```bash
imx500-package -i path/to/packerOut.zip -o path/to/package_out
```

The package step produces an `.rpk` file. Convert it into the files used by this
SDK:

```bash
python3 tools/rpk2fpk.py path/to/package_out/model.rpk \
  --output-dir path/to/custom_model \
  --overwrite
```

The output directory should contain:

```text
path/to/custom_model/model.fpk
path/to/custom_model/network_info.txt
```

Always keep the `.fpk` file and `network_info.txt` from the same conversion and
packaging run.

### Online Packaging

Use the Arducam AI conversion service when you prefer a hosted workflow or need
conversion support:

[https://ai.arducam.com/](https://ai.arducam.com/)

Service tutorial:

[https://www.arducam.com/arducam-ai-model-converter-tutorial](https://www.arducam.com/arducam-ai-model-converter-tutorial)

## Flash The Model To B0642

Use the Python USB bridge flasher from the SDK root. Before connecting the
module to the host PC, hold the module `MODE` button, then plug in the host-side
USB cable if your module requires bridge/control mode.

```bash
cd python_bindings
python3 -m pip install -e . --no-build-isolation
PYTHONPATH=python python3 tools/imx500_usb_flash.py --status
PYTHONPATH=python python3 tools/imx500_usb_flash.py \
  --model path/to/custom_model/model.fpk \
  --network-info path/to/custom_model/network_info.txt
```

Run the first hardware check:

```bash
PYTHONPATH=python python3 tools/imx500_first_run.py
```

You should see the USB bridge respond, the SDK `imx500_open()` call succeed, and at
least one metadata frame saved.

For more flashing details, see [tools/README.md](../../tools/README.md).

## Validate With The MCU Examples

The Pico 2 serial stream example is a good transport check because the firmware
forwards metadata frames without depending on one specific model task:

[examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md](../../examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md)

For a custom YOLO model, expect to add your own host-side parser or renderer:

- Add a task configuration that points to your custom model directory.
- Load the matching `network_info.txt`.
- Decode the output tensors according to your exported model layout.
- Apply your confidence threshold, non-maximum suppression, labels, and
  application event logic.

The bundled host receiver includes parsers/renderers for the bundled reference
tasks. It does not automatically know the tensor layout or post-processing for
an arbitrary custom model.

## Success Checkpoints

- The trained model reaches acceptable accuracy on representative samples.
- Conversion completes without unsupported operators.
- Packaging produces a matched `.fpk` and `network_info.txt`.
- `imx500_usb_flash.py --status` can communicate with the module.
- The custom model can be flashed to B0642.
- `imx500_first_run.py` receives at least one metadata frame.
- Your post-processing converts tensors into correct product-level events.

## If It Fails

Check:

- Unsupported layers, operators, or model export settings.
- Quantization images that do not represent the deployment scene.
- Input size, color order, normalization, or preprocessing mismatch.
- `.fpk` and `network_info.txt` from different package runs.
- Tensor order, output scale, label mapping, confidence threshold, or NMS logic.
- MCU transport payload limits if the custom output tensors are larger than the
  bundled examples.

## Next Unlock

| Result | Next |
| --- | --- |
| Custom model runs on hardware | Continue with [Model Validation To Production](../paths/model-validation-to-production.md). |
| You only need AI events on an MCU | Continue with [SPI Metadata To MCU Product](../paths/spi-mcu-product-path.md). |
| You need Linux preview, services, or packaging | Continue with [MIPI / Raspberry Pi / CM5 Product](../paths/mipi-rpi-product-path.md). |
| You need PC/Linux plug-and-play deployment | Continue with [USB3 UVC Deployment](../paths/usb-validation-to-uvc.md). |
| Conversion or accuracy is blocked | Contact [Arducam custom model support](https://ai.arducam.com/). |
