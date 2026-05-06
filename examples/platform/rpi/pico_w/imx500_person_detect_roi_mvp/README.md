# IMX500 Person Detect ROI MVP

Raspberry Pi Pico W + Arducam IMX500 person detection demo.

This example streams the latest JPEG frame over HTTP port 80, overlays ROI and person detection boxes in the browser, and drives GP0/GP1 high when a person is detected.

## Features

- HTTP web UI on port 80.
- Dynamic JPEG refresh through `/frame.jpg`.
- Runtime status through `/status.json`.
- Runtime configuration through `/config`.
- Person detection boxes drawn over the displayed JPEG.
- ROI polygon drawn over the displayed JPEG.
- Confidence threshold configurable from the page.
- ROI point configuration kept in the page. Default ROI is the full image.
- GP0 and GP1 output high when `person_count > 0`, low otherwise.
- UDP logic has been removed.
- Serial logs are quiet by default for long-running stability.

## Hardware

- Raspberry Pi Pico W / compatible RP2040 Pico W target.
- Arducam IMX500 module wired as expected by this SDK example.
- GP0 and GP1 are used as detection output pins.

Output behavior:

| Pin | State |
| --- | --- |
| GP0 | High when at least one person is detected |
| GP1 | High when at least one person is detected |
| GP0/GP1 | Low when no person is detected |

## Wi-Fi Config

Create a `.env` file in this directory:

```env
WIFI_SSID=your_wifi_ssid
WIFI_PASSWORD=your_wifi_password
```

The default Wi-Fi country is `CN`. Override it at configure time if needed:

```bash
cmake .. -DWIFI_COUNTRY=US
```

## Build

```bash
mkdir build
cd build
cmake ..
```

## Runtime

Open the USB serial console. After Wi-Fi connects, the firmware prints the page URL:

```text
HTTP JPEG server listening on http://<board-ip>/
```

Open that URL in a browser on the same network.

The page is designed for landscape use:

- Left: JPEG frame with ROI and detection overlays.
- Right: frame status, JPEG size, person count, GP0/GP1 state, confidence, and ROI config.
- Apply button: updates confidence threshold and ROI points.

## Web Endpoints

| Endpoint | Purpose |
| --- | --- |
| `/` or `/index.html` | Web UI |
| `/frame.jpg` | Latest JPEG frame |
| `/status.json` | Latest metadata, detections, ROI, and threshold |
| `/config?...` | Apply runtime config |

Example config request:

```text
/config?conf=0.50&x0=0&y0=0&x1=1&y1=0&x2=1&y2=1&x3=0&y3=1
```

Coordinates are relative to the displayed image:

- `0.0` means left/top edge.
- `1.0` means right/bottom edge.
- Default ROI points are the full image: `(0,0)`, `(1,0)`, `(1,1)`, `(0,1)`.
