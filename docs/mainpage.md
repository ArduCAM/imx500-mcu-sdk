# IMX500 MCU SDK API Overview

The IMX500 MCU SDK exposes C/C++ interfaces for binding platform drivers,
starting the module, selecting MIPI and SPI output, reading inference metadata,
managing model assets, and tuning the sensor.

This page follows the declarations currently exposed by the public headers.
Platform quick starts are maintained in `README.md`; integration details live
under `docs/`.

## Public Header Files

- `ArducamIMX500SDK.h` contains the module, stream, metadata, model-management,
  data-injection, ROI, and sensor-tuning interfaces.
- `ai_driver.h` contains the platform callback types and registration functions
  required by the SDK.

Names beginning with an underscore and the SDK's global driver/cache variables
are implementation details. They are intentionally excluded from the generated
public API reference.

## Interface Categories

### Platform Binding

Register these callbacks before probing or opening the module:

- @ref register_i2c_driver "register_i2c_driver(...)"
- @ref register_spi_driver "register_spi_driver(...)"
- @ref register_printf "register_printf(...)"

The I2C driver also supplies the millisecond and microsecond delay callbacks
used by module operations. The logger is optional.

### Module Lifecycle and Streaming

- @ref get_fw_ver "get_fw_ver(...)"
- @ref get_pid "get_pid(...)"
- @ref get_sensor_device_id "get_sensor_device_id(...)"
- @ref probe_imx500_module "probe_imx500_module(...)"
- @ref reset_imx500_module "reset_imx500_module()"
- @ref imx500_open "imx500_open(...)"
- @ref load_imx500_fw "load_imx500_fw(...)"
- @ref stream_on "stream_on()"
- @ref switch_spi_data_forward_mode "switch_spi_data_forward_mode(...)"

@ref imx500_open selects one of two model-loading paths. A non-null model with
a non-zero size uses direct SPI loading and requires the matching network-info
blob. A null or empty model requests model and network-info loading from module
flash.

### Metadata and Tensor Parsing

- @ref get_metadata_size "get_metadata_size()"
- @ref read_metadata "read_metadata(...)"
- @ref unpack_imx500_output_header "unpack_imx500_output_header(...)"
- @ref parse_metadata "parse_metadata(...)"

Call @ref get_metadata_size before allocating or selecting a receive buffer.
After @ref read_metadata returns a non-zero byte count, pass that exact count
and the same @ref spi_data_format_t selected by @ref imx500_open to
@ref parse_metadata.

### Model and Network Assets

SPI asset operations:

- @ref get_spi_flash_status "get_spi_flash_status(...)"
- @ref write_model_to_cam_flash "write_model_to_cam_flash(...)"
- @ref write_nn_info_to_cam_flash "write_nn_info_to_cam_flash(...)"
- @ref load_nn_info_to_cam_memory "load_nn_info_to_cam_memory(...)"

I2C payload asset operations:

- @ref abort_i2c_payload_operation "abort_i2c_payload_operation()"
- @ref write_model_to_cam_flash_i2c "write_model_to_cam_flash_i2c(...)"
- @ref write_nn_info_to_cam_flash_i2c "write_nn_info_to_cam_flash_i2c(...)"
- @ref load_model_to_cam_memory_i2c "load_model_to_cam_memory_i2c(...)"
- @ref load_nn_info_to_cam_memory_i2c "load_nn_info_to_cam_memory_i2c(...)"

Host-side network-info cache:

- @ref load_nn_info_to_sdk_cache "load_nn_info_to_sdk_cache(...)"
- @ref dump_network_info_list "dump_network_info_list()"

Model and network-info blobs must be a matching pair. The transport-specific
functions above do not replace the platform callback registration step.

### Data Injection

- @ref do_data_injection_stream "do_data_injection_stream(...)"
- @ref do_data_injection "do_data_injection(...)"
- @ref stop_data_injection "stop_data_injection()"

Use the streaming variant when the complete input cannot be held in one host
buffer. Set `first_time` for the first frame in an injection session and call
@ref stop_data_injection when the session ends.

### ROI and Coordinate Mapping

- @ref bbox_coordinate_x_scale_map "bbox_coordinate_x_scale_map(...)"
- @ref bbox_coordinate_y_scale_map "bbox_coordinate_y_scale_map(...)"
- @ref imx500_calculate_center_crop_xyxy "imx500_calculate_center_crop_xyxy(...)"
- @ref dnn_crop_xyxy_absolute "dnn_crop_xyxy_absolute(...)"
- @ref apply_dnn_input_tensor_mapping "apply_dnn_input_tensor_mapping(...)"

Crop rectangles use absolute sensor coordinates and the half-open form
`[xmin, xmax)`, `[ymin, ymax)`.

### ISP and Sensor Tuning

- @ref imx500_get_default_ae_config "imx500_get_default_ae_config(...)"
- @ref imx500_set_ae_config "imx500_set_ae_config(...)"
- @ref imx500_apply_ae_config "imx500_apply_ae_config()"
- @ref imx500_get_default_white_balance_config "imx500_get_default_white_balance_config(...)"
- @ref imx500_set_white_balance_config "imx500_set_white_balance_config(...)"
- @ref imx500_apply_white_balance_config "imx500_apply_white_balance_config()"

The setters stage a configuration. Call the corresponding apply function to
send the staged settings to the sensor.

### Low-Level Sensor Register Access

- @ref sensor_i2c_write_16_8 "sensor_i2c_write_16_8(...)"
- @ref sensor_i2c_read_16_8 "sensor_i2c_read_16_8(...)"
- @ref sensor_i2c_write_16_16 "sensor_i2c_write_16_16(...)"
- @ref sensor_i2c_read_16_16 "sensor_i2c_read_16_16(...)"
- @ref sensor_i2c_write_16_32 "sensor_i2c_write_16_32(...)"
- @ref sensor_i2c_read_16_32 "sensor_i2c_read_16_32(...)"

These functions bypass the higher-level configuration helpers. Use them only
when the target register and value width are known.

## Supported Stream Formats

All four values in @ref mipi_data_format_t are handled by the current
@ref imx500_open implementation.

The current @ref imx500_open implementation configures these SPI modes:

- @ref SPI_METADATA_OUTPUT_TENSOR
- @ref SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR
- @ref SPI_METADATA_NONE

The remaining @ref spi_data_format_t values are reserved by the interface but
are not currently enabled by @ref imx500_open. Select a configured mode before
calling @ref read_metadata or @ref parse_metadata.

## Key Data Types

- @ref i2c_driver and @ref spi_driver bind the host transport and delay hooks.
- @ref mipi_data_format_t and @ref spi_data_format_t select stream payloads.
- @ref IMX500OutputHeader describes the 12-byte IMX500 metadata header.
- @ref IMX500ParsedMetadata, @ref IMX500ParsedNetwork, and
  @ref IMX500ParsedTensor expose parsed metadata and bound tensor payloads.
- @ref spi_flash_status_t reports asset-transfer progress and completion.
- @ref imx500_crop_rect_t represents a half-open sensor crop rectangle.
- @ref imx500_ae_config_t and @ref imx500_white_balance_config_t hold staged
  ISP settings.

## Typical Inference Sequence

1. Register platform callbacks with @ref register_i2c_driver and
   @ref register_spi_driver.
2. Optionally register logging with @ref register_printf.
3. Optionally verify connectivity with @ref probe_imx500_module.
4. Initialize the model and stream formats with @ref imx500_open.
5. Start output with @ref stream_on.
6. Poll @ref get_metadata_size until a non-zero frame size is available.
7. Read that frame with @ref read_metadata.
8. Decode it with @ref parse_metadata using the selected SPI format.

`stream_on()` reports command failures through the registered logger because
its current C API return type is `void`. Treat a subsequent metadata timeout as
a stream-start failure and include the SDK log in diagnostics.
