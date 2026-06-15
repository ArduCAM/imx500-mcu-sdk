/**
 * @file ArducamIMX500SDK.h
 * @brief Public API for integrating IMX500 modules on MCU platforms.
 */

#ifndef ARDUCAM_IMX500_SDK_H_
#define ARDUCAM_IMX500_SDK_H_

#include "ai_regs.h"
#include "common_regs.h"
#include "ai_driver.h"
#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#define VALID_DATA_OFFSET           0
#define IMX500_HEADER_LEN           12
#define MAX_DETECT_ITEM_NUM         10
#define IMX500_FW_TYPE_LOADER 0
#define IMX500_FW_TYPE_MAIN 1
#define IMX500_FW_TYPE_NETWORK_WEIGHTS 2


/** @brief Decoded 12-byte metadata header emitted by the IMX500 output stream. */
typedef struct {
    uint8_t valid_flag;
    uint8_t frame_count;
    uint16_t max_length_of_line;
    uint16_t size_of_ap_parameter;
    uint16_t network_ordinal;
    uint8_t indicator;
} IMX500OutputHeader;

typedef struct {
    float scale;
    int zero_point;
} QuantParam;

#define SC_DNN_MAX_NETWORK_ID_DEC (999999)
#define SC_DNN_MAX_NETWORK_ID (0x999999)
#define MAX_DNN_HEADER_SIZE (4096)
#define MAX_NUM_OF_NETWORKS (3)
#define MAX_OUTPUT_TENSOR_NUM (30)

#define IMX500_MAX_NETWORKS           MAX_NUM_OF_NETWORKS
#define IMX500_MAX_TENSOR_DIMS        8
#define IMX500_MAX_INPUT_TENSORS      8
#define IMX500_MAX_OUTPUT_TENSORS     MAX_OUTPUT_TENSOR_NUM

typedef struct {
    uint16_t id;
    uint16_t size;
    uint16_t serialization_index;
    uint16_t padding;
} IMX500TensorDimension;

/** @brief Parsed tensor descriptor plus bound tensor payload pointer. */
typedef struct {
    uint32_t id;
    char name[48];
    uint8_t format;
    uint8_t bits_per_element;
    uint8_t dimension_count;
    int32_t zero_point;
    float scale;
    uint32_t element_count;
    uint32_t data_bytes;
    uint32_t aligned_data_bytes;
    const uint8_t *data;
    IMX500TensorDimension dimensions[IMX500_MAX_TENSOR_DIMS];
} IMX500ParsedTensor;

/** @brief Parsed network description including input and output tensors. */
typedef struct {
    uint32_t id;
    char name[48];
    char type[24];
    uint8_t input_tensor_count;
    uint8_t output_tensor_count;
    IMX500ParsedTensor input_tensors[IMX500_MAX_INPUT_TENSORS];
    IMX500ParsedTensor output_tensors[IMX500_MAX_OUTPUT_TENSORS];
} IMX500ParsedNetwork;

/** @brief Parsed metadata payload with JPEG and output tensor offsets. */
typedef struct {
    bool has_primary_header;
    bool has_output_header;
    IMX500OutputHeader primary_header;
    IMX500OutputHeader output_header;
    uint32_t ap_param_offset;
    uint32_t ap_param_size;
    uint32_t ap_param_end_offset;
    uint32_t jpeg_size;
    uint32_t jpeg_block_offset;
    uint32_t jpeg_block_end_offset;
    uint32_t jpeg_data_offset;
    uint32_t jpeg_data_len;
    const uint8_t *jpeg_data;
    uint32_t output_header_offset;
    uint32_t output_payload_offset;
    uint32_t output_payload_length;
    uint8_t network_count;
    uint8_t selected_network_index;
    IMX500ParsedNetwork networks[IMX500_MAX_NETWORKS];
} IMX500ParsedMetadata;

/** @brief Detection bounding box in absolute image coordinates. */
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int   class_id;
} BBox;

typedef struct {
    BBox* bboxs;
    uint16_t valid_num;
} DetectionResult;

typedef enum {
    DNN_INPUT_FORMAT_RGB = 0,
    DNN_INPUT_FORMAT_BGR,
    DNN_INPUT_FORMAT_Y,
    DNN_INPUT_FORMAT_BAYER_RGB,
} dnn_input_format_t;

typedef enum {
    BAYER_CH_R = 0,
    BAYER_CH_GR,
    BAYER_CH_GB,
    BAYER_CH_B,
    BAYER_CH_MAX
} E_BAYER_CH;

typedef struct {
    uint16_t add;
    uint8_t shift;
    uint16_t clipMax;
    uint16_t clipMin;
} sc_input_tensor_rgb_norm_info_t;

typedef struct {
    uint32_t tensorSize;
    uint32_t dimSize;
    uint8_t padding;
    uint8_t bytePerElement;
} sc_output_tensor_size_info_t;

typedef struct {
    uint16_t dnnHeaderSize;
    uint16_t inputTensorWidth;
    uint16_t inputTensorHeight;
    uint32_t inputTensorWidthStride;
    uint32_t inputTensorHeightStride;
    uint32_t inputTensorSize;
    uint32_t inputTensorFormat;

    uint16_t NormK00;
    uint16_t NormK01;
    uint16_t NormK02;
    uint16_t NormK03;
    uint16_t NormK10;
    uint16_t NormK11;
    uint16_t NormK12;
    uint16_t NormK13;
    uint16_t NormK20;
    uint16_t NormK21;
    uint16_t NormK22;
    uint16_t NormK23;

    uint32_t yClip;
    uint32_t cbClip;
    uint32_t crClip;

    uint8_t yGgain;
    uint8_t ySht;
    uint16_t yAdd;

    sc_input_tensor_rgb_norm_info_t rgbNorm[BAYER_CH_MAX];

    uint8_t outputTensorNum;
    uint32_t outputTensorDimSize[MAX_OUTPUT_TENSOR_NUM];
    uint8_t outputTensorPadding[MAX_OUTPUT_TENSOR_NUM];
    uint8_t outputTensorBytesPerElement[MAX_OUTPUT_TENSOR_NUM];
    sc_output_tensor_size_info_t *p_outputTensorSizeInfo;
} sc_dnn_nw_info_t;

/** @brief SPI bridge forwarding path selected inside the module. */
typedef enum {
	SPI_DATA_FORWARDING_NONE = 0,
	SPI_SLAVE_FROM_IMX500_MSPI,
	SPI_MASTER_FROM_IMX500_MSPI,
	SPI_SLAVE_FROM_IMX500_SSPI,
	SPI_MASTER_FROM_IMX500_SSPI,
	SPI_SLAVE_TO_IMX500_SSPI,
	SPI_SLAVE_WRITE_MODEL_TO_FLASH,
	SPI_SLAVE_WRITE_NN_INFO_TO_FLASH,
	SPI_LOAD_NN_INFO_TO_MEMORY,
	SPI_FORWORDING_MODE_SWITCHING
} spi_data_forwarding_mode_t;

/** @brief SPI metadata layout returned during inference streaming. */
typedef enum {
	SPI_METADATA_OUTPUT_TENSOR = 0,
	SPI_METADATA_INPUT_TENSOR,
	SPI_METADATA_JPEG_INPUT_TENSOR,
	SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
	SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
	SPI_METADATA_NONE
} spi_data_format_t;

/** @brief MIPI data layout selected for the module video output. */
typedef enum {
	MIPI_DATA_IMAGE = 0,
	MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
	MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
	MIPI_DATA_NONE
} mipi_data_format_t;


typedef enum {
	MODULE_WAIT_BOOT = 0,
	MODULE_SELF_BOOT,
	MODULE_I2C_LOAD_NN_BOOT,
	MODULE_RPI_PRIVATE_PROTOCOL_LOAD_NN_BOOT,
	MODULE_SPI_LOAD_NN_BOOT,
} module_boot_mode_t;

typedef enum {
	SPI_FLASH_OP_IDLE = 0,
	SPI_FLASH_OP_WAIT_HEADER = 1,
	SPI_FLASH_OP_RECEIVING = 2,
	SPI_FLASH_OP_PARSING = 3,
	SPI_FLASH_OP_SUCCESS = 4,
	SPI_FLASH_OP_FAILED = 5,
} spi_flash_op_status_t;

typedef enum {
	SPI_FLASH_RESULT_NONE = 0,
	SPI_FLASH_RESULT_OK = 1,
	SPI_FLASH_RESULT_TIMEOUT = 2,
	SPI_FLASH_RESULT_BAD_HEADER = 3,
	SPI_FLASH_RESULT_BAD_SIZE = 4,
	SPI_FLASH_RESULT_WRITE_FAIL = 5,
	SPI_FLASH_RESULT_CRC_MISMATCH = 6,
	SPI_FLASH_RESULT_PARSE_FAIL = 7,
	SPI_FLASH_RESULT_FLASH_BLOB_MISSING = 8,
	SPI_FLASH_RESULT_BUSY = 9,
	SPI_FLASH_RESULT_BAD_OPERATION = 10,
	SPI_FLASH_RESULT_NOT_SUPPORTED = 11,
	SPI_FLASH_RESULT_NO_MEMORY = 12,
	SPI_FLASH_RESULT_IMX500_DOWNLOAD_FAIL = 13,
} spi_flash_op_result_t;

/** @brief Summary of a model or network-info flash write request. */
typedef struct {
	uint32_t status;
	uint32_t result;
	uint32_t bytes_done;
	uint32_t bytes_total;
} spi_flash_status_t;

typedef struct {
    uint16_t top;
    uint16_t left;
    uint16_t height;
    uint16_t width;
} imx500_roi_t;

/** @brief Crop rectangle in absolute sensor coordinates, using [xmin, xmax) and [ymin, ymax). */
typedef struct {
    uint32_t xmin;
    uint32_t ymin;
    uint32_t xmax;
    uint32_t ymax;
} imx500_crop_rect_t;

/** @brief Auto-exposure configuration to be applied to the sensor. */
typedef struct {
    uint16_t max_exposure_time_100us;
    uint8_t max_gain_db;
    uint8_t brightness;
    uint8_t speed;
    bool has_roi;
    imx500_roi_t roi;
} imx500_ae_config_t;

typedef enum {
    IMX500_WB_MODE_AUTO = 0,
    IMX500_WB_MODE_ONE_PUSH = 1,
    IMX500_WB_MODE_PRESET = 2,
} imx500_white_balance_mode_t;

typedef enum {
    IMX500_WB_PRESET_3200 = 0,
    IMX500_WB_PRESET_4300 = 1,
    IMX500_WB_PRESET_5600 = 2,
    IMX500_WB_PRESET_6500 = 3,
} imx500_white_balance_preset_t;

/** @brief White balance configuration to be applied to the sensor. */
typedef struct {
    imx500_white_balance_mode_t mode;
    imx500_white_balance_preset_t preset;
    bool has_roi;
    imx500_roi_t roi;
} imx500_white_balance_config_t;

#ifdef __cplusplus
#include "ApParams.h"
extern "C" {
#endif

/**
 * @name Dequant APIs
 * Helpers that parse tensor metadata and bind output payloads so host code can
 * inspect quantization parameters and run dequant or post-processing.
 * @{
 */

/**
 * @brief Parse one raw SPI metadata buffer using the selected SPI metadata layout.
 * @param data Raw metadata buffer from @ref read_metadata.
 * @param data_len Number of valid bytes in @p data.
 * @param spi_format SPI metadata layout passed to @ref open.
 * @param parsed_metadata Output structure filled with parsed offsets and tensors.
 * @return `true` on success, `false` if the payload format is invalid.
 */
bool parse_metadata(const uint8_t *data,
                    uint32_t data_len,
                    spi_data_format_t spi_format,
                    IMX500ParsedMetadata *parsed_metadata);

/** @} */

/**
 * @name ROI Helper APIs
 * Helpers for crop control and coordinate mapping.
 * @{
 */

/** @brief Scale an X coordinate from source width to target width. */
uint32_t bbox_coordinate_x_scale_map(float x, uint32_t s_w, uint32_t t_w);

/** @brief Scale a Y coordinate from source height to target height. */
uint32_t bbox_coordinate_y_scale_map(float y, uint32_t s_h, uint32_t t_h);

/**
 * @brief Calculate a centered crop rectangle that matches a target output aspect ratio.
 *
 * The returned rectangle is expressed in absolute source/sensor coordinates and
 * uses left-closed, right-open coordinates: [xmin, xmax), [ymin, ymax).
 * Dimensions and offsets are rounded down to even values when possible so the
 * rectangle is suitable for Bayer/binning image paths.
 *
 * Example: source 4056x3040 and target 1024x600 returns
 * xmin=0, ymin=332, xmax=4056, ymax=2708.
 *
 * @return `0` on success, negative on invalid arguments.
 */
int imx500_calculate_center_crop_xyxy(uint32_t source_width,
                                      uint32_t source_height,
                                      uint32_t target_width,
                                      uint32_t target_height,
                                      imx500_crop_rect_t *crop);

/**
 * @brief Apply a crop rectangle in absolute sensor coordinates.
 * @return `0` on success, negative on invalid arguments or command failure.
 */
int dnn_crop_xyxy_absolute(uint32_t xmin, uint32_t ymin, uint32_t xmax, uint32_t ymax);

/**
 * @brief Apply the DNN input-tensor mapping to the current IMX500 12MP active area.
 *
 * This keeps the full IMX500 active area before crop (4056x3040) as the source
 * coordinate system, calculates a centered crop matching @p input_tensor_width x
 * @p input_tensor_height, then applies it through @ref dnn_crop_xyxy_absolute.
 *
 * @return `0` on success, negative on invalid arguments or command failure.
 */
int apply_dnn_input_tensor_mapping(uint32_t width, uint32_t height);

/** @} */

/**
 * @name ISP and Sensor Tuning APIs
 * Helpers for auto-exposure and white-balance configuration.
 * @{
 */

/** @brief Fill an AE config structure with SDK defaults. */
void imx500_get_default_ae_config(imx500_ae_config_t *config);

/** @brief Fill a white balance config structure with SDK defaults. */
void imx500_get_default_white_balance_config(imx500_white_balance_config_t *config);

/** @brief Stage a new auto-exposure configuration in the sensor control block. */
int imx500_set_ae_config(const imx500_ae_config_t *config);

/** @brief Apply the staged auto-exposure configuration to the sensor. */
int imx500_apply_ae_config(void);

/** @brief Stage a new white balance configuration in the sensor control block. */
int imx500_set_white_balance_config(const imx500_white_balance_config_t *config);

/** @brief Apply the staged white balance configuration to the sensor. */
int imx500_apply_white_balance_config(void);

/** @} */

/**
 * @name Data Injection and Host Preprocessing APIs
 * Helpers for host-driven input injection and input tensor preprocessing.
 * @{
 */

/** @brief Callback used by @ref do_data_injection_stream to stream image bytes lazily. */
typedef uint32_t (*data_provider_t)(uint8_t *buf, uint32_t max_len, uint32_t offset);

/**
 * @brief Inject input data through a pull-based provider callback.
 * @param provider Callback that fills the next chunk.
 * @param total_size Total input size in bytes.
 * @param first_time Set to `true` before the first injected frame.
 */
void do_data_injection_stream(data_provider_t provider, uint32_t total_size, bool first_time);

/**
 * @brief Inject one complete input buffer directly.
 * @param data Pointer to source bytes.
 * @param size Number of bytes to inject.
 * @param first_time Set to `true` before the first injected frame.
 */
void do_data_injection(const uint8_t *data, uint32_t size, bool first_time);

/** @brief Exit data injection mode. */
void stop_data_injection(void);

/** @brief Apply cached network-info normalization rules to an input tensor buffer. */
int _preprocess_nn_input_data(uint8_t *src, uint32_t src_size);

/**
 * @brief Resize, align, and transpose raw image bytes into injection layout.
 * @return Number of bytes produced, or negative on conversion failure.
 */
int _convert_injected_data(const uint8_t *img,
                           uint32_t img_width, uint32_t img_height, uint32_t img_channels,
                           uint8_t *dst, uint32_t dst_size,
                           uint32_t input_height, uint32_t input_width, uint32_t channel_num,
                           const uint8_t transpose_order[3], uint32_t align_base);

/** @} */

/**
 * @name IMX500 Control, Metadata, and Asset-Management APIs
 * Helpers for module lifecycle control, metadata transport, and asset loading.
 * @{
 */

/** @brief Read the module firmware version register. */
void get_fw_ver(uint32_t* v);

/** @brief Read the module device ID register. */
void get_pid(uint32_t* v);

/**
 * @brief Read the sensor device ID used by imx500_dump_basic_info().
 * @param out Destination buffer for the formatted ID string.
 * @param out_size Size of @p out in bytes. Must be at least 36.
 * @return 0 on success, negative on command or argument failure.
 *
 * The formatted ID matches imx500_dump_basic_info():
 * XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX
 */
int get_sensor_device_id(char *out, size_t out_size);

/**
 * @brief Probe the module and read device ID plus boot status.
 * @return `true` if both registers were read successfully.
 */
bool probe_imx500_module(uint32_t *device_id, uint32_t *boot_status);

/**
 * @brief Reset the IMX500 module and wait until loader/main firmware is ready.
 * @return `true` if reset completed and boot status returned to 1.
 *
 * This is the reset-only part shared by @ref open. It does not load a model
 * from SPI or flash.
 */
bool reset_imx500_module(void);

/**
 * @brief Initialize the module, load model/network info, and configure stream formats.
 * @param nn_fw Network weights blob. Pass `NULL` for flash boot.
 * @param nn_fw_size Size of @p nn_fw in bytes.
 * @param nn_info Network-info blob associated with the loaded model.
 * @param nn_info_size Size of @p nn_info in bytes.
 * @param mipi_format Requested MIPI output format.
 * @param spi_format Requested SPI metadata format.
 * @param fps Target frame rate.
 * @return `true` if initialization completed successfully.
 */
bool open(const uint8_t *nn_fw, uint32_t nn_fw_size, const uint8_t* nn_info, uint32_t nn_info_size, mipi_data_format_t mipi_format, spi_data_format_t spi_format, uint32_t fps);

/**
 * @brief Transfer one firmware blob to the module.
 * @param fw Pointer to the firmware image.
 * @param size Firmware size in bytes.
 * @param fw_type One of `IMX500_FW_TYPE_*`.
 * @return `0` on success, negative on failure.
 */
int load_imx500_fw(const uint8_t *fw, uint32_t size, uint32_t fw_type);

/** @brief Start inference/video streaming after @ref open succeeds. */
void stream_on(void);

/**
 * @brief Switch the module SPI bridge to a different data forwarding mode.
 * @param m Requested SPI forwarding mode.
 * @return `true` if the switch succeeded.
 */
bool switch_spi_data_forward_mode(spi_data_forwarding_mode_t m);

/** @brief Read the size of the next metadata payload exposed by the module. */
uint32_t get_metadata_size(void);

/**
 * @brief Read one metadata frame over SPI.
 * @param rx_buf Destination buffer.
 * @param buf_size Capacity of @p rx_buf in bytes.
 * @return Number of bytes written to @p rx_buf, or `0` on failure.
 */
int32_t read_metadata(uint8_t *rx_buf, uint32_t buf_size);

/** @brief Decode a raw IMX500 metadata header into a typed structure. */
void unpack_imx500_output_header(const uint8_t* data, IMX500OutputHeader* header);

/**
 * @brief Read the current flash write progress from module firmware.
 * @param status Output status structure.
 * @return `true` on success.
 */
bool get_spi_flash_status(spi_flash_status_t *status);

/**
 * @brief Abort any active I2C payload import session and wait until it returns to idle.
 * @return `true` if the module acknowledged the abort and the payload state is idle.
 */
bool abort_i2c_payload_operation(void);

/**
 * @brief Stream a model blob to module flash over SPI.
 * @param model Pointer to the model payload.
 * @param model_size Model size in bytes.
 * @return `true` if the transfer and module-side validation succeeded.
 */
bool write_model_to_cam_flash(const uint8_t *model, uint32_t model_size);

/**
 * @brief Stream a network-info blob to module flash over SPI.
 * @param nn_info Pointer to the network-info payload.
 * @param nn_info_size Network-info size in bytes.
 * @return `true` if the transfer and module-side validation succeeded.
 */
bool write_nn_info_to_cam_flash(const uint8_t *nn_info, uint32_t nn_info_size);

/**
 * @brief Load a network-info blob directly into module memory.
 * @param nn_info Pointer to the network-info payload.
 * @param nn_info_size Network-info size in bytes.
 * @return `true` if the module accepted the blob.
 */
bool load_nn_info_to_cam_memory(const uint8_t *nn_info, uint32_t nn_info_size);
bool load_nn_info_to_cam_memory_i2c(const uint8_t *nn_info, uint32_t nn_info_size);
bool load_model_to_cam_memory_i2c(const uint8_t *model, uint32_t model_size);

/** @brief Cached network descriptors parsed from the current network-info blob. */
extern sc_dnn_nw_info_t network_info[MAX_NUM_OF_NETWORKS];

/** @brief Parse and cache a network-info blob in the SDK host-side cache. */
int load_nn_info_to_sdk_cache(const uint8_t *cfg, size_t cfg_len);

/** @brief Print the cached network list through the registered logger. */
void dump_network_info_list(void);

/** @brief Write one 8-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_write_16_8(uint16_t reg_addr, uint8_t data);

/** @brief Read one 8-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_read_16_8(uint16_t reg_addr, uint8_t *data);

/** @brief Write one 16-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_write_16_16(uint16_t reg_addr, uint16_t data);

/** @brief Read one 16-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_read_16_16(uint16_t reg_addr, uint16_t *data);

/** @brief Write one 32-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_write_16_32(uint16_t reg_addr, uint32_t data);

/** @brief Read one 32-bit sensor register addressed by a 16-bit register address. */
int sensor_i2c_read_16_32(uint16_t reg_addr, uint32_t *data);

/** @} */

#ifdef __cplusplus
}
#endif

#endif  // ARDUCAM_IMX500_SDK_H_
