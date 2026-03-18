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

// network_info parser types migrated from main project
#define SC_DNN_MAX_NETWORK_ID_DEC (999999)
#define SC_DNN_MAX_NETWORK_ID (0x999999)
#define MAX_DNN_HEADER_SIZE (4096)
#define MAX_NUM_OF_NETWORKS (3)
#define MAX_OUTPUT_TENSOR_NUM (30)

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

typedef enum {
	SPI_METADATA_OUTPUT_TENSOR = 0,
	SPI_METADATA_INPUT_TENSOR,
	SPI_METADATA_JPEG_INPUT_TENSOR,
	SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR,
	SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
	SPI_METADATA_NONE
} spi_data_format_t;

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
} spi_flash_op_result_t;

typedef struct {
	uint32_t status;
	uint32_t result;
	uint32_t bytes_done;
	uint32_t bytes_total;
} spi_flash_status_t;

#ifdef __cplusplus
#include "ApParams.h"
extern "C" {
#endif

void unpack_imx500_output_header(const uint8_t* data, IMX500OutputHeader* header);
bool parse_ap_params(const uint8_t* data, size_t data_len, DetectionResult* detection_result);
uint32_t bbox_coordinate_x_scale_map(float x, uint32_t s_w, uint32_t t_w);
uint32_t bbox_coordinate_y_scale_map(float y, uint32_t s_h, uint32_t t_h);
bool switch_spi_data_forward_mode(spi_data_forwarding_mode_t m);
bool get_spi_flash_status(spi_flash_status_t *status);
bool spi_slave_write_model_to_flash(const uint8_t *model, uint32_t model_size);
bool spi_slave_write_nn_info_to_flash(const uint8_t *nn_info, uint32_t nn_info_size);
int load_imx500_fw(const uint8_t *fw, uint32_t size, uint32_t fw_type);
void stream_on(void);
bool open(const uint8_t *nn_fw, uint32_t nn_fw_size, const uint8_t* nn_info, uint32_t nn_info_size, mipi_data_format_t mipi_format, spi_data_format_t spi_format);
uint32_t get_metadata_size(void);
int32_t read_metadata(uint8_t *rx_buf, uint32_t buf_size);
typedef uint32_t (*data_provider_t)(uint8_t *buf, uint32_t max_len, uint32_t offset);
void do_data_injection_stream(data_provider_t provider, uint32_t total_size, bool first_time);
void do_data_injection(const uint8_t *data, uint32_t size, bool first_time);
void stop_data_injection(void);
int _preprocess_nn_input_data(uint8_t *src, uint32_t src_size);
int _convert_injected_data(const uint8_t *img,
                           uint32_t img_width, uint32_t img_height, uint32_t img_channels,
                           uint8_t *dst, uint32_t dst_size,
                           uint32_t input_height, uint32_t input_width, uint32_t channel_num,
                           const uint8_t transpose_order[3], uint32_t align_base);
extern sc_dnn_nw_info_t network_info[MAX_NUM_OF_NETWORKS];
int set_nw_info_from_flash_buffer(const uint8_t *cfg, size_t cfg_len);
void dump_network_info_list(void);
void get_fw_ver(uint32_t* v);
void get_pid(uint32_t* v);
int sensor_i2c_write_16_8(uint16_t reg_addr, uint8_t data);
int sensor_i2c_read_16_8(uint16_t reg_addr, uint8_t *data);
int sensor_i2c_write_16_16(uint16_t reg_addr, uint16_t data);
int sensor_i2c_read_16_16(uint16_t reg_addr, uint16_t *data);
int sensor_i2c_write_16_32(uint16_t reg_addr, uint32_t data);
int sensor_i2c_read_16_32(uint16_t reg_addr, uint32_t *data);

#ifdef __cplusplus
}
#endif

#endif  // ARDUCAM_IMX500_SDK_H_
