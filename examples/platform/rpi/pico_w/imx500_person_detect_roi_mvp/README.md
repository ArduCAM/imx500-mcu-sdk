# Mission: Build A Pico W Person-Detection ROI Demo

Raspberry Pi Pico W + Arducam IMX500 person detection demo.

This example streams the latest JPEG frame over HTTP port 80, overlays ROI and
person detection boxes in the browser, and drives GP0/GP1 to the configured
warning-active level when a person is detected.

## Goal

Turn IMX500 metadata into a product-like event loop:

- browser preview through `/frame.jpg`
- person detections exposed through `/status.json`
- configurable ROI and confidence threshold through `/config`
- GP0/GP1 warning outputs driven by `person_count > 0`

## Hardware

- Raspberry Pi Pico W / compatible RP2040 Pico W target.
- Arducam IMX500 module wired as expected by this SDK example.
- GP0 and GP1 connected to the downstream warning input, relay, LED, or test fixture.
- Wi-Fi network reachable by the Pico W and browser.

## Run

Create a `.env` file in this directory:

```env
WIFI_SSID=your_wifi_ssid
WIFI_PASSWORD=your_wifi_password
```

Build the firmware:

```bash
cd examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp
mkdir build
cd build
cmake ..
cmake --build .
```

Flash `imx500_person_detect_roi_mvp.uf2` to Pico W.

Open the USB serial console. After Wi-Fi connects, the firmware prints:

```text
HTTP JPEG server listening on http://<board-ip>/
```

Open that URL in a browser on the same network.

## Expected Feedback

You should see:

- Serial output showing IMX500 module information.
- `IMX500 stream_on complete`.
- `metadata worker started on core1`.
- Wi-Fi connected and an HTTP URL printed.
- Browser preview with JPEG frame, ROI polygon, person boxes, and status panel.
- GP0/GP1 switch to the configured warning-active level when `person_count > 0`.

## You Passed This Mission When

- The browser UI loads from the Pico W.
- `/frame.jpg` refreshes with the latest JPEG frame.
- `/status.json` reports metadata, person count, ROI, threshold, and GPIO state.
- Changing confidence or ROI through the page updates runtime behavior.
- GP0/GP1 idle and warning levels match the configured active level.

## If It Fails

- If Wi-Fi does not connect, check `.env`, `WIFI_COUNTRY`, SSID, password, and network reachability.
- If the IMX500 does not open, check wiring, power, and whether the model is already deployed in module Flash.
- If metadata parsing fails, confirm the module is running the expected person-detection model and metadata format.
- If the browser loads but no image appears, check that `IMX500_SPI_METADATA_FORMAT` includes JPEG metadata.
- If GP0/GP1 appear inverted, rebuild with the expected active level.

## Runtime Configuration

The default Wi-Fi country is `CN`. Override it at configure time if needed:

```bash
cmake .. -DWIFI_COUNTRY=US
```

Output active level:

```bash
cmake .. -DPERSON_DETECT_GPIO_ACTIVE_LEVEL=HIGH
cmake .. -DPERSON_DETECT_GPIO_ACTIVE_LEVEL=LOW
```

By default, GP0/GP1 are active-high: startup and idle are low, warning is high.
With active-low outputs, startup and idle are high, and warning is low. `1` and
`0` are also accepted.

## Web Endpoints

| Endpoint | Purpose |
| --- | --- |
| `/` or `/index.html` | Web UI |
| `/frame.jpg` | Latest JPEG frame |
| `/status.json` | Latest metadata, detections, GPIO output state, ROI, and threshold |
| `/config?...` | Apply runtime config |

Example config request:

```text
/config?conf=0.50&x0=0&y0=0&x1=1&y1=0&x2=1&y2=1&x3=0&y3=1
```

Coordinates are relative to the displayed image:

- `0.0` means left/top edge.
- `1.0` means right/bottom edge.
- Default ROI points are the full image: `(0,0)`, `(1,0)`, `(1,1)`, `(0,1)`.

## Next Unlock

- Turn `person_count > 0` into your product event logic.
- Continue with the [SPI metadata to MCU product path](../../../../../docs/paths/spi-mcu-product-path.md).
- Validate or replace the model with the [model validation mission](../../../../../docs/paths/model-validation-to-production.md).
- Move toward production with the [design-in checklist](../../../../../docs/production/design-in-checklist.md).
