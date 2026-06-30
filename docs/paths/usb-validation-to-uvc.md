# Path: USB Validation To USB3 UVC Deployment

Use this path when you want to validate the IMX500 camera, model, metadata, and
preview flow from a PC before wiring an MCU or building a custom host product.

## Best For

- PC or Linux validation before firmware integration.
- Fast camera and model bring-up with a visible success signal.
- USB3 UVC plug-and-play deployment after the module is validated.
- Teams that need to separate model/application validation from MCU porting.

## Start Here

Run the first USB checkpoint from `python_bindings`:

```bash
cd python_bindings
python3 -m pip install -e . --no-build-isolation
PYTHONPATH=python python3 tools/imx500_first_run.py
```

For an optional image checkpoint, request one extracted JPEG preview:

```bash
PYTHONPATH=python python3 tools/imx500_first_run.py --jpeg-preview
```

## Success Checkpoints

You passed this path's first mission when:

- The USB bridge is detected.
- IMX500 status or probe information is read.
- `imx500_mcu_sdk.imx500_open()` and `stream_on()` complete.
- One metadata frame is saved under `first_run_outputs/`.
- Optional: one JPEG preview is saved under `first_run_outputs/`.

## Next Unlock

After USB validation passes, choose the next step:

| If you need... | Continue with... |
| --- | --- |
| PC/Linux plug-and-play deployment | [B0566 USB3 UVC deployment path](https://github.com/ArduCAM/ArducamIMX500SDK) |
| A repeatable USB quick start | [Official USB3 UVC quick-start docs](https://docs.arducam.com/AI-Camera-Solutions/Quick-Start-Guide/IMX500-USB3-UVC-Camera/#more-arducam-examples) |
| Model/application validation before hardware integration | [Model validation mission](model-validation-to-production.md)
| A low-power MCU product | [SPI metadata to MCU product path](spi-mcu-product-path.md) |

## When To Contact Arducam

Contact Arducam when:

- The USB bridge is detected but `imx500_open()` does not complete.
- Metadata frames are present but do not match the expected model output.
- JPEG preview works on PC but your target deployment needs a different USB or
  UVC product shape.
- You need a supported path from validation hardware to production supply.

For contact preparation and support routing, see
[Support Options](../production/support-options.md).

Production and design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

## Back To README

Return to the [IMX500 MCU SDK mission map](../../README.md).
