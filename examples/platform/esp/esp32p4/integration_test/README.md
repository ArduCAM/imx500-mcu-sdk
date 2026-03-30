# Simple Video Server for ESP32-P4

This repository now uses the `simple_video_server` example as the root ESP-IDF project.

## What Changed

- The root `main/` component now builds the `simple_video_server` application entry.
- Required components such as `esp_video`, `mdns`, and `esp_wifi_remote` are downloaded automatically through the ESP-IDF component manager.
- `esp_cam_sensor` is kept as a local override so the unmerged `sensors/pivariety` driver can be used immediately.
- The web frontend assets now live under the root `frontend/gzipped`.

## Local Override Strategy

The project uses this dependency pattern:

- Remote managed components for the standard upstream dependencies
- Local `override_path` for `esp_cam_sensor`

This keeps the project close to the upstream `simple_video_server` structure while still allowing the local `pivariety` driver to participate in the build.

## Default Target

The default configuration is prepared for:

- `esp32p4`
- ESP32-P4 Function EV Board V1.5
- MIPI-CSI sensor path
- `pivariety` sensor
- `RAW10 1920x1080 30fps`

## Build

```bash
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
idf.py flash monitor
```

## Menuconfig Notes

Check these menus when adapting to your hardware:

- `Example Connection Configuration`
- `Example Video Initialization Configuration`
- `Component config -> Espressif Camera Sensors Configurations`

If your board wiring or `pivariety` mode differs, adjust the camera interface and sensor format there.
