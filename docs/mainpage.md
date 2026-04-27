# IMX500 MCU SDK API Overview

The IMX500 MCU SDK exposes a compact set of C/C++ interfaces for bringing up the module, starting inference streams, parsing metadata and output tensors, injecting test input, and tuning sensor behavior.

This page is the API landing page for the SDK. Build and publishing instructions remain in `README.md`; the sections below focus on what each public interface is used for.

## Public Header Files

- `ArducamIMX500SDK.h`
  High-level SDK interfaces for module control, metadata parsing, data injection, and sensor configuration.
- `ai_driver.h`
  Platform callback registration interfaces used to bind your `I2C`, `SPI`, delay, and optional log implementation.

## Interface Categories

### Dequant

Use this category to parse raw output metadata and bind tensor payloads before reading quantization parameters or running host-side dequant and post-processing:

- @ref parse_output_tensor_data_with_metadata "parse_output_tensor_data_with_metadata(...)"

This category is the SDK handoff point between raw SPI metadata and any host-side tensor dequantization or result decoding pipeline.

### ROI

Use this category for crop and coordinate-mapping related helpers:

- @ref bbox_coordinate_x_scale_map "bbox_coordinate_x_scale_map(...)"
- @ref bbox_coordinate_y_scale_map "bbox_coordinate_y_scale_map(...)"
- @ref dnn_crop_xyxy_absolute "dnn_crop_xyxy_absolute(...)"

This group is used when an application needs to translate bounding-box coordinates or set a crop region on the device side.

### ISP

Use this category for exposure and white-balance tuning:

- @ref imx500_get_default_ae_config "imx500_get_default_ae_config(...)"
- @ref imx500_set_ae_config "imx500_set_ae_config(...)"
- @ref imx500_apply_ae_config "imx500_apply_ae_config()"
- @ref imx500_get_default_white_balance_config "imx500_get_default_white_balance_config(...)"
- @ref imx500_set_white_balance_config "imx500_set_white_balance_config(...)"
- @ref imx500_apply_white_balance_config "imx500_apply_white_balance_config()"

This is the category to use when you need to tune image quality through the SDK's AE and white-balance controls.

### Data Injection

- @ref do_data_injection_stream "do_data_injection_stream(...)"
- @ref do_data_injection "do_data_injection(...)"
- @ref stop_data_injection "stop_data_injection()"
- @ref _preprocess_nn_input_data "_preprocess_nn_input_data(...)"
- @ref _convert_injected_data "_convert_injected_data(...)"

This group is especially useful for validation pipelines where input frames are pushed from the MCU host side instead of being captured live from the image path.

### IMX500 Control

Use this category for module lifecycle control, model loading, metadata transport, and runtime state queries:

- @ref register_i2c_driver "register_i2c_driver(...)"
- @ref register_spi_driver "register_spi_driver(...)"
- @ref register_printf "register_printf(...)"
- @ref get_fw_ver "get_fw_ver(...)"
- @ref get_pid "get_pid(...)"
- @ref probe_imx500_module "probe_imx500_module(...)"
- @ref open "open(...)"
- @ref load_imx500_fw "load_imx500_fw(...)"
- @ref stream_on "stream_on()"
- @ref switch_spi_data_forward_mode "switch_spi_data_forward_mode(...)"
- @ref get_metadata_size "get_metadata_size()"
- @ref read_metadata "read_metadata(...)"
- @ref unpack_imx500_output_header "unpack_imx500_output_header(...)"
- @ref get_spi_flash_status "get_spi_flash_status(...)"
- @ref write_model_to_cam_flash "write_model_to_cam_flash(...)"
- @ref write_nn_info_to_cam_flash "write_nn_info_to_cam_flash(...)"
- @ref load_nn_info_to_cam_memory "load_nn_info_to_cam_memory(...)"
- @ref load_nn_info_to_sdk_cache "load_nn_info_to_sdk_cache(...)"
- @ref dump_network_info_list "dump_network_info_list()"
- @ref sensor_i2c_write_16_8 "sensor_i2c_write_16_8(...)"
- @ref sensor_i2c_read_16_8 "sensor_i2c_read_16_8(...)"
- @ref sensor_i2c_write_16_16 "sensor_i2c_write_16_16(...)"
- @ref sensor_i2c_read_16_16 "sensor_i2c_read_16_16(...)"
- @ref sensor_i2c_write_16_32 "sensor_i2c_write_16_32(...)"
- @ref sensor_i2c_read_16_32 "sensor_i2c_read_16_32(...)"

This is the core operational category of the SDK and the main entry point for bring-up, streaming, metadata consumption, and low-level sensor-side control.

## Key Data Structures

The following structures are the most important ones to understand when integrating the SDK:

- @ref IMX500OutputHeader
  Raw header decoded from IMX500 metadata output.
- @ref IMX500ParsedMetadata
  Parsed metadata view that includes header state, offsets, JPEG payload information, and parsed networks.
- @ref IMX500ParsedNetwork
  Describes one parsed network and its input and output tensor lists.
- @ref IMX500ParsedTensor
  Describes one tensor including dimensions, format, quantization, and bound payload pointer.
- @ref spi_flash_status_t
  Reports the current state of a flash programming operation.
- @ref imx500_ae_config_t
  Auto-exposure configuration structure.
- @ref imx500_white_balance_config_t
  White-balance configuration structure.

## Typical Call Sequence

Most integrations follow this order:

1. Register platform callbacks with @ref register_i2c_driver "register_i2c_driver(...)" and @ref register_spi_driver "register_spi_driver(...)".
2. Optionally bind logging with @ref register_printf "register_printf(...)".
3. Probe the hardware with @ref probe_imx500_module "probe_imx500_module(...)" if your application needs an early presence check.
4. Initialize the module with @ref open "open(...)".
5. Start runtime output with @ref stream_on "stream_on()".
6. Read metadata with @ref read_metadata "read_metadata(...)".
7. Parse output metadata and bind tensor payloads for dequant or downstream decoding with @ref parse_output_tensor_data_with_metadata "parse_output_tensor_data_with_metadata(...)".
