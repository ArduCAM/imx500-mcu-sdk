# IMX500 MCU SDK 集成手册

语言版本：中文 | [English](../eng/integration-guide.md) | [日本語](../jpn/integration-guide.md)

本文档承接根目录 `README.md` 中原有的工程集成内容，面向已经准备把
`imx500-mcu-sdk` 接入目标 MCU 工程的开发者。根 README 负责快速入口、Demo
路径和产品定位；本文档负责 CMake 集成、平台适配、运行流程和常见问题。

## SDK 定位

`imx500-mcu-sdk` 是一个用于从 MCU 平台控制 Arducam IMX500 模组的轻量级 SDK。
它提供可移植的驱动接口、C/C++ SDK API，以及平台参考集成代码。

仓库设计目标是逐步支持多个 MCU 平台。平台相关说明会维护在 `examples/`
目录下。

## 核心能力

- 通过 `I2C` / `SPI` 与 IMX500 模组通信
- 提供统一 C/C++ SDK API，入口见 `ArducamIMX500SDK.h`
- 支持网络固件加载和视频流控制
- 支持 metadata 读取和解析
- 支持 SPI data forwarding mode 切换
- 使用 platform adapter pattern 加速新 MCU 平台 bring-up

## 产品定位

[B0642](https://www.arducam.com/arducam-imx500-ai-camera-module-for-esp32-p4-and-other-mcu-soc.html)
定位为将 `IMX500` 能力带到 MCU 平台的桥接方案。

适用场景包括：

- 需要 `IMX500` 端侧推理能力的新 MCU 产品设计
- 从 SoC 方案迁移到 MCU 方案的既有项目
- 优先考虑兼容性、希望使用 `SPI` 获取 metadata 的集成方案

与 Raspberry Pi AI Camera 的路径差异：

| Feature | Raspberry Pi AI Camera | B0642 |
| --- | --- | --- |
| MIPI output | Image + metadata (`RGB888` input tensor + output tensor) | Image only |
| Metadata path | Over `MIPI` | Over `SPI` (`JPEG` input tensor + output tensor, or output tensor only) |

## 仓库结构

| Path | Purpose |
| --- | --- |
| `ArducamIMX500SDK.h/.cc` | Public SDK API and core implementation |
| `ai_driver.h/.c` | Low-level driver abstraction and registration |
| `imx500_mcu_sdk.cmake` | Collects SDK source files and compile-time configuration |
| `imx500_firmware.cmake` | Collects generated firmware C++ sources |
| `imx500_firmware_cpp/imx500_firmware/` | Generated firmware/network-info blobs |
| `examples/` | Platform reference projects |
| `third_party/flatbuffers/` | FlatBuffers dependency used by network-info parsing |

## 环境要求

- CMake 3.13 或更高版本
- 目标 MCU 对应的 C/C++ toolchain
- Git，用于初始化 submodule

不同平台还需要各自的 SDK 或工具链，例如 ESP32-P4 示例需要 ESP-IDF，Pico 2
示例需要 Raspberry Pi Pico SDK。具体版本以对应 example README 为准。

## 初始化 Submodule

```bash
git submodule update --init --recursive
```

如果构建时报 FlatBuffers 头文件缺失，通常是 submodule 没有初始化完成。

## CMake 集成

在平台工程的 CMake 项目中包含 SDK 和 firmware 源文件集合：

```cmake
include(path/to/imx500_mcu_sdk.cmake)
include(path/to/imx500_firmware.cmake)
```

将以下 source group 添加到你的 target：

- `${IMX500_MCU_SDK_SRC_FILES}`
- `${IMX500_FIRMWARE_CPP_FILES}`

对 target 应用 SDK 的编译期配置：

```cmake
imx500_mcu_sdk_apply_config(your_target)
```

典型 include directory 包括：

- SDK root
- `imx500_firmware_cpp/`
- `third_party/flatbuffers/include`

## 选择 MIPI 输出分辨率

SDK 默认使用 `1024x600` MIPI 输出。也可以通过 CMake 选择以下支持的分辨率：

- `1024x600`
- `1600x1200`
- `1280x720`
- `640x480`

示例：

```bash
cmake -S . -B build -DIMX500_MCU_SDK_SENSOR_MIPI_RESOLUTION=1280x720
```

该配置会由 `imx500_mcu_sdk.cmake` 转换为对应的 sensor MIPI command、width 和
height 编译定义。

## 实现平台 Adapter

新平台需要提供平台相关的 `I2C`、`SPI`、delay，以及可选 log 输出函数，然后注册到
SDK。

需要关注的注册接口：

- `register_i2c_driver(...)`
- `register_spi_driver(...)`
- `register_printf(...)`

参考实现可从以下路径开始阅读：

- `examples/platform/esp/esp32p4/ai_camera_multitask/main/peripherals_adapter.c`
- `examples/platform/rpi/pico2/peripherals_adapter.c`
- `examples/platform/rpi/pico_w/peripherals_adapter.c`

适配时请确认：

- `I2C` 地址、读写时序和错误返回值符合平台实现
- `SPI` 的 TX/RX 方向和片选行为正确
- 传输 buffer 的生命周期满足 DMA 或中断驱动要求
- delay 函数不会破坏当前 RTOS 或 bare-metal 调度模型
- SDK log 输出不会阻塞关键实时路径

## 典型运行流程

大多数平台按以下顺序工作：

1. 注册平台回调：`register_i2c_driver(...)` 和 `register_spi_driver(...)`
2. 可选：通过 `register_printf(...)` 将 SDK log 接入平台 logger
3. 可选：调用 `probe_imx500_module(...)` 做早期硬件存在性检查
4. 调用 `open(...)` 加载 firmware/network info 并配置数据格式
5. 调用 `stream_on()` 启动 runtime output
6. 调用 `read_metadata(...)` 读取 SPI metadata frame
7. 调用 `parse_metadata(...)` 解析 metadata，并绑定 output tensor payload
8. 在应用层执行后处理，将 tensor 转换为 product event

示意：

```text
register_i2c_driver / register_spi_driver
    -> probe_imx500_module
    -> open
    -> stream_on
    -> read_metadata
    -> parse_metadata
    -> postprocess
    -> application event
```

## Metadata 到应用事件

SDK 的边界通常停在 raw metadata 和 parsed tensor descriptor。产品应用还需要根据模型
类型执行后处理。

```text
raw SPI metadata
    -> IMX500ParsedMetadata
    -> IMX500ParsedNetwork
    -> IMX500ParsedTensor
    -> post-processing
    -> event or UI overlay
```

常见应用事件包括：

- `person_count`
- `object_detected`
- `zone_occupied`
- `classification_label`
- `pose_keypoints`
- `segmentation_mask`

## API 参考

公开 API 声明位于 `ArducamIMX500SDK.h`。

在线 API 文档：

[https://arducam.github.io/imx500-mcu-sdk/](https://arducam.github.io/imx500-mcu-sdk/)

API landing page 位于 `docs/mainpage.md`，其中按以下类别组织接口：

- Dequant
- ROI
- ISP
- Data Injection
- IMX500 Control

## 平台示例入口

| Platform | Entry |
| --- | --- |
| ESP32-P4 | `examples/platform/esp/esp32p4/README.md` |
| Pico 2 wiring | `examples/platform/rpi/pico2/README.md` |
| Pico 2 serial stream | `examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md` |
| Pico 2 production test | `examples/platform/rpi/pico2/production_test/README.md` |
| Pico W person detect ROI MVP | `examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md` |

## 排障

- 如果构建时报 FlatBuffers 头文件缺失，执行 `git submodule update --init --recursive`。
- 如果相机无法检测，优先检查 `GND`、`3V3`、`I2C SDA`、`I2C SCL` 和 adapter 回调。
- 如果视频有输出但 metadata 读取失败，检查 `SPI_CS`、`SPI_SCK`、`SPI_TX`、`SPI_RX`。
- 注意 `SPI_TX` / `SPI_RX` 是从相机侧命名的，MCU `TX` 应连接相机 `RX`，MCU `RX` 应连接相机 `TX`。
- 如果 `parse_metadata(...)` 失败，确认 metadata format、network-info、payload size 和 buffer size 是否匹配。
- 如果修改了 MIPI 分辨率，确认 CMake 使用的是 `imx500_mcu_sdk.cmake` 支持的值。

## 量产前检查

在进入产品设计阶段前，建议确认：

- 目标 MCU 的 RAM、flash、SPI 带宽和 MIPI 视频路径
- 镜头、视场角、光照条件和结构安装方式
- 模型版本、network-info 版本和 metadata format
- firmware/network-info 加载策略
- factory test 流程和成功/失败日志
- 相机检测失败、SPI 读取失败、metadata 解析失败时的恢复策略
- 商业支持、光学定制、模型适配和批量采购需求
