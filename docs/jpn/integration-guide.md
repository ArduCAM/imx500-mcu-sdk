# IMX500 MCU SDK インテグレーションガイド

言語: [中文](../zho/integration-guide.md) | [English](../eng/integration-guide.md) | 日本語

このドキュメントは、リポジトリルートの `README.md` に含まれていたエンジニアリング
向けの統合内容を移したものです。ルート README はデモ、プラットフォーム別の入口、
製品ポジショニングを示すためのページです。このガイドでは、CMake 統合、プラット
フォームアダプタ、実行フロー、トラブルシューティングを扱います。

## SDK の位置づけ

`imx500-mcu-sdk` は、MCU プラットフォームから Arducam IMX500 モジュールを制御する
ための軽量 SDK です。ポータブルなドライバインターフェース、C/C++ SDK API、参照用
統合コードを提供します。

このリポジトリは、今後さらに多くの MCU プラットフォームをサポートできるように設計
されています。プラットフォーム固有の説明は `examples/` 以下で管理されます。

## 主な機能

- IMX500 モジュールとの `I2C` / `SPI` 通信
- `ArducamIMX500SDK.h` を入口とする統一 C/C++ SDK API
- ネットワークファームウェアのロードとストリーム制御
- metadata の読み出しと解析
- SPI data forwarding mode の切り替え
- 新しい MCU の bring-up を早める platform adapter pattern

## 製品ポジショニング

[B0642](https://www.arducam.com/arducam-imx500-ai-camera-module-for-esp32-p4-and-other-mcu-soc.html)
は、`IMX500` の機能を MCU プラットフォームへ持ち込むためのブリッジソリューション
です。

対象となる用途:

- `IMX500` の推論機能を必要とする新しい MCU ベース製品
- SoC ベースの既存プロジェクトから MCU ベースのシステムへ移行する設計
- `MIPI` 経由の metadata ではなく、互換性を優先して `SPI` 経由の metadata を使う統合

Raspberry Pi AI Camera とのパスの違い:

| Feature | Raspberry Pi AI Camera | B0642 |
| --- | --- | --- |
| MIPI output | Image + metadata (`RGB888` input tensor + output tensor) | Image only |
| Metadata path | Over `MIPI` | Over `SPI` (`JPEG` input tensor + output tensor, or output tensor only) |

## リポジトリ構成

| Path | Purpose |
| --- | --- |
| `ArducamIMX500SDK.h/.cc` | Public SDK API and core implementation |
| `ai_driver.h/.c` | Low-level driver abstraction and registration |
| `imx500_mcu_sdk.cmake` | Collects SDK source files and compile-time configuration |
| `imx500_firmware.cmake` | Collects generated firmware C++ sources |
| `imx500_firmware_cpp/imx500_firmware/` | Generated firmware/network-info blobs |
| `examples/` | Platform reference projects |
| `third_party/flatbuffers/` | FlatBuffers dependency used by network-info parsing |

## 必要環境

- CMake 3.13 以降
- ターゲット MCU 向けの C/C++ toolchain
- submodule 初期化に使用する Git

各プラットフォームでは、それぞれの SDK または toolchain も必要です。たとえば
ESP32-P4 の例では ESP-IDF、Pico 2 の例では Raspberry Pi Pico SDK が必要です。
toolchain のバージョンは各 example README を基準にしてください。

## Submodule の初期化

```bash
git submodule update --init --recursive
```

ビルド時に FlatBuffers のヘッダが見つからない場合、通常は submodule の初期化が完了
していません。

## CMake 統合

プラットフォーム側の CMake プロジェクトで SDK と firmware のソース集合を読み込みます。

```cmake
include(path/to/imx500_mcu_sdk.cmake)
include(path/to/imx500_firmware.cmake)
```

次の source group を target に追加します。

- `${IMX500_MCU_SDK_SRC_FILES}`
- `${IMX500_FIRMWARE_CPP_FILES}`

SDK のコンパイル時設定を target に適用します。

```cmake
imx500_mcu_sdk_apply_config(your_target)
```

典型的な include directory:

- SDK root
- `imx500_firmware_cpp/`
- `third_party/flatbuffers/include`

## MIPI 出力解像度の選択

SDK のデフォルト MIPI 出力は `1024x600` です。CMake で次の対応解像度を選択できます。

- `1024x600`
- `1600x1200`
- `1280x720`
- `640x480`

例:

```bash
cmake -S . -B build -DIMX500_MCU_SDK_SENSOR_MIPI_RESOLUTION=1280x720
```

`imx500_mcu_sdk.cmake` は、この設定を対応する sensor MIPI command、width、height の
コンパイル定義へ変換します。

## Platform Adapter の実装

新しいプラットフォームでは、プラットフォーム固有の `I2C`、`SPI`、delay、任意の log
関数を用意し、SDK に登録します。

登録 API:

- `register_i2c_driver(...)`
- `register_spi_driver(...)`
- `register_printf(...)`

参照実装:

- `examples/platform/esp/esp32p4/ai_camera_multitask/main/peripherals_adapter.c`
- `examples/platform/rpi/pico2/peripherals_adapter.c`
- `examples/platform/rpi/pico_w/peripherals_adapter.c`

adapter bring-up 時の確認項目:

- `I2C` アドレス、読み書きタイミング、エラー戻り値
- `SPI` の TX/RX 方向と chip select の動作
- DMA または割り込み駆動実装での転送 buffer の寿命
- RTOS または bare-metal scheduler 下での delay 動作
- SDK log 出力が重要なリアルタイム処理をブロックしないこと

## 典型的な実行フロー

多くのプラットフォームは次の順序で動作します。

1. `register_i2c_driver(...)` と `register_spi_driver(...)` でプラットフォーム callback を登録します。
2. 必要に応じて `register_printf(...)` で SDK log をプラットフォーム logger へ接続します。
3. 必要に応じて `probe_imx500_module(...)` を呼び、早期のハードウェア存在確認を行います。
4. `open(...)` を呼び、firmware/network info をロードしてデータ形式を設定します。
5. `stream_on()` を呼び、runtime output を開始します。
6. `read_metadata(...)` を呼び、SPI metadata frame を読み出します。
7. `parse_metadata(...)` を呼び、metadata を解析して output tensor payload を関連付けます。
8. アプリケーション側の後処理で tensor を product event に変換します。

フロー概要:

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

## Metadata からアプリケーションイベントへ

SDK の境界は、多くの場合 raw metadata と parsed tensor descriptor までです。製品
アプリケーションでは、モデルに応じた後処理が必要です。

```text
raw SPI metadata
    -> IMX500ParsedMetadata
    -> IMX500ParsedNetwork
    -> IMX500ParsedTensor
    -> post-processing
    -> event or UI overlay
```

一般的なアプリケーションイベント:

- `person_count`
- `object_detected`
- `zone_occupied`
- `classification_label`
- `pose_keypoints`
- `segmentation_mask`

## API リファレンス

公開 API の宣言は `ArducamIMX500SDK.h` にあります。

オンライン API ドキュメント:

[https://arducam.github.io/imx500-mcu-sdk/](https://arducam.github.io/imx500-mcu-sdk/)

API landing page は `docs/mainpage.md` で、インターフェースは次のカテゴリに分類されます。

- Dequant
- ROI
- ISP
- Data Injection
- IMX500 Control

## プラットフォーム例の入口

| Platform | Entry |
| --- | --- |
| ESP32-P4 | `examples/platform/esp/esp32p4/README.md` |
| Pico 2 wiring | `examples/platform/rpi/pico2/README.md` |
| Pico 2 serial stream | `examples/platform/rpi/pico2/camera_serial_stream_multitask/README.md` |
| Pico 2 production test | `examples/platform/rpi/pico2/production_test/README.md` |
| Pico W person detect ROI MVP | `examples/platform/rpi/pico_w/imx500_person_detect_roi_mvp/README.md` |

## トラブルシューティング

- FlatBuffers ヘッダが見つからない場合は、`git submodule update --init --recursive` を実行してください。
- カメラが検出されない場合は、まず `GND`、`3V3`、`I2C SDA`、`I2C SCL`、adapter callback を確認してください。
- 映像は出るが metadata 読み出しに失敗する場合は、`SPI_CS`、`SPI_SCK`、`SPI_TX`、`SPI_RX` を確認してください。
- `SPI_TX` / `SPI_RX` はカメラ側から見た名称です。MCU `TX` はカメラ `RX` へ、MCU `RX` はカメラ `TX` へ接続します。
- `parse_metadata(...)` が失敗する場合は、metadata format、network-info、payload size、buffer size を確認してください。
- MIPI 解像度を変更した場合は、CMake が `imx500_mcu_sdk.cmake` で対応している値を使っていることを確認してください。

## 量産前チェックリスト

製品設計へ進む前に、次の項目を確認してください。

- ターゲット MCU の RAM、flash、SPI bandwidth、MIPI video path
- レンズ、画角、照明条件、機構取り付け
- モデルバージョン、network-info バージョン、metadata format
- firmware/network-info のロード戦略
- factory test フローと成功/失敗ログ
- カメラ検出失敗、SPI 読み出し失敗、metadata 解析失敗時の復旧動作
- 商用サポート、光学カスタマイズ、モデル適配、量産購入要件
