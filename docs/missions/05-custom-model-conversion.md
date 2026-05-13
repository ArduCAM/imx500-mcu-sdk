# Mission 05: Custom Model Conversion

Use this mission when model zoo validation is not enough and you need to bring a
custom model into the IMX500 workflow.

## Goal

Convert a custom model into an IMX500-compatible package, validate that the
package is accepted, and confirm that metadata can be parsed into your product
logic.

Model conversion docs: [https://www.arducam.com/arducam-ai-model-converter-tutorial](https://www.arducam.com/arducam-ai-model-converter-tutorial)

Custom model support: [https://ai.arducam.com/](https://ai.arducam.com/)

## Start With

Before conversion, confirm:

- The model task is supported by the IMX500 workflow.
- Input size, preprocessing, output tensors, and post-processing are documented.
- Quantization and model size constraints are understood.
- You have representative validation images or videos.
- You know which product path will consume the result: USB, MIPI/Linux, or
  SPI/MCU.

## Run

Follow the official [model conversion guide](https://www.arducam.com/arducam-ai-model-converter-tutorial). 

After conversion, validate the package on hardware:

```bash
cd python_bindings
PYTHONPATH=python python3 tools/imx500_usb_flash.py \
  --model path/to/custom_model.fpk \
  --network-info path/to/network_info.txt

PYTHONPATH=python python3 tools/imx500_first_run.py
```

Then validate the parsed output against real application samples.

## Expected Feedback

You should see:

- The converted package is accepted by the conversion flow.
- The model and `network_info` can be flashed or loaded.
- `imx500_first_run.py` receives metadata from the device.
- Parsed output matches the expected tensor layout.
- Real-scene samples produce usable application events.

## You Passed This Mission When

- The custom model runs on real IMX500 hardware.
- Metadata frames match the expected network output.
- Post-processing converts tensor output into application-level events.
- Accuracy, latency, lighting, and FOV are acceptable for the target scene.

## If It Fails

Check:

- Unsupported layers, operators, or model task assumptions.
- Quantization, input shape, preprocessing, and normalization.
- `network_info` mismatch with the model package.
- Tensor order, output scale, label mapping, and post-processing code.
- Scene mismatch: lighting, lens, object scale, occlusion, or motion.

## Next Unlock

| Result | Next |
| --- | --- |
| Custom model works | Continue with [model validation to production](../paths/model-validation-to-production.md) |
| Hardware path is PC/Linux | Continue with [USB3 UVC deployment](../paths/usb-validation-to-uvc.md) |
| Product needs Linux services or packaging | Continue with [MIPI / Raspberry Pi / CM5 product path](../paths/mipi-rpi-product-path.md) |
| Product only needs AI events | Continue with [SPI metadata to MCU product path](../paths/spi-mcu-product-path.md) |
| Conversion or accuracy is blocked | [Contact custom model support](https://ai.arducam.com/) |

