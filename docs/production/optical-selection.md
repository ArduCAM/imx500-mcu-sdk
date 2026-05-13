# Optical Selection

Use this page when the model works in a demo but accuracy changes in the real
scene. IMX500 product success depends on model, optics, illumination, mounting,
and enclosure working together.

## What To Confirm

| Area | Questions to answer |
| --- | --- |
| FOV | Does the lens cover the required detection zone with enough pixels on target? |
| Working distance | Are near and far objects inside the useful focus range? |
| Illumination | Does the scene change across day/night, flicker, backlight, or low light? |
| Enclosure | Does the window introduce glare, blur, IR cutoff changes, or contamination risk? |
| Mounting | Can the camera position and angle be repeated in production? |
| Model assumptions | Do object scale, angle, occlusion, and background match validation data? |

## Success Checkpoints

You passed optical validation when:

- Real-scene samples match the expected model output.
- Accuracy remains acceptable across lighting and mounting variation.
- FOV and focus leave enough margin for production tolerances.
- The enclosure window does not change the model result unexpectedly.

## If It Fails

- Re-check lens FOV and working distance against the real object size.
- Capture samples from the real enclosure, not only open-air bench tests.
- Compare model output before and after the enclosure window.
- Review illumination, reflection, and mounting repeatability.
- Revisit model validation if scene data differs from the model assumptions.

## When To Contact Arducam

Contact Arducam when you need lens, FOV, illumination, mechanical, or enclosure
review before product freeze.

Optical customization support: [https://www.arducam.com/](https://www.arducam.com/)

Design-in contact route: [https://www.arducam.com/blog/contact-arducam/](https://www.arducam.com/blog/contact-arducam/)

