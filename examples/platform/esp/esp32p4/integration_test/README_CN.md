# ESP32-P4 Simple Video Server

当前根工程已经完全切换为 `simple_video_server` 形态，原来的 ISP tuning 相关入口和组件都已清理掉。

## 当前依赖策略

- `esp_video`、`mdns`、`esp_wifi_remote` 等通用依赖通过 ESP-IDF Component Manager 自动下载
- `esp_cam_sensor` 使用本地 `override_path`
- 本地 `esp_cam_sensor` 中只保留并启用 `sensors/pivariety`

这样既能保持和上游 `simple_video_server` 的结构接近，也能继续使用尚未合并到上游的本地 `pivariety` 驱动。

## 默认配置

根目录 `sdkconfig.defaults` 默认设置为：

- 目标芯片：`esp32p4`
- 开发板：`ESP32-P4 Function EV Board V1.5`
- 相机接口：`MIPI-CSI`
- 传感器：`pivariety`
- 默认格式：`RAW10 1920x1080 30fps`

如果你的硬件连线或模组版本不同，可以在 `menuconfig` 中调整。

## 构建方法

```bash
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
idf.py flash monitor
```

## 重点配置位置

- `Example Connection Configuration`
- `Example Video Initialization Configuration`
- `Component config -> Espressif Camera Sensors Configurations`

## 目录说明

- 根目录 `main/` 负责构建 `simple_video_server` 应用入口
- 根目录 `frontend/gzipped/` 保存网页静态资源
- `esp_cam_sensor/` 是本地覆写组件，内部仅保留 `pivariety` 驱动
