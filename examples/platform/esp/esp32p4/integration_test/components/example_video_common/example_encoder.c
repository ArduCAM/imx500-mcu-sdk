/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/jpeg_encode.h"
#include "example_video_common.h"

#if __has_include("linux/videodev2.h")
#include "linux/videodev2.h"
#else
#define EXAMPLE_V4L2_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define V4L2_PIX_FMT_GREY EXAMPLE_V4L2_FOURCC('G', 'R', 'E', 'Y')
#define V4L2_PIX_FMT_RGB565 EXAMPLE_V4L2_FOURCC('R', 'G', 'B', 'P')
#define V4L2_PIX_FMT_RGB24 EXAMPLE_V4L2_FOURCC('R', 'G', 'B', '3')
#define V4L2_PIX_FMT_UYVY EXAMPLE_V4L2_FOURCC('U', 'Y', 'V', 'Y')
#define V4L2_PIX_FMT_YUV420 EXAMPLE_V4L2_FOURCC('Y', 'U', '1', '2')
#define V4L2_PIX_FMT_YUV444 EXAMPLE_V4L2_FOURCC('Y', '4', '4', '4')
#define V4L2_PIX_FMT_SBGGR8 EXAMPLE_V4L2_FOURCC('B', 'A', '8', '1')
#endif

typedef struct example_encoder {
    jpeg_encode_cfg_t jpeg_enc_config;
    uint32_t jpeg_out_buf_size;
} example_encoder_t;

static const char *TAG = "example_encoder";

/**
 * @brief JPEG hardware encoder handle to provide a single instance for all video streams
 */
static jpeg_encoder_handle_t s_jpeg_hw_handle;
static uint32_t s_jpeg_hw_ref_count;

/**
 * @brief Initialize the encoder
 *
 * @param config Encoder configuration
 * @param ret_handle Encoder handle
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_init(example_encoder_config_t *config, example_encoder_handle_t *ret_handle)
{
    if (!config || !ret_handle) {
        ESP_LOGE(TAG, "invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    example_encoder_t *encoder;
    uint32_t jpeg_enc_input_src_size;
    jpeg_encode_cfg_t jpeg_enc_config = {0};
    jpeg_encoder_handle_t jpeg_handle = NULL;

    jpeg_enc_config.height = config->height;
    jpeg_enc_config.width = config->width;
    jpeg_enc_config.image_quality = config->quality;

    switch (config->pixel_format) {
    case V4L2_PIX_FMT_SBGGR8: // Treat raw8 as grayscale, for testing only.
    case V4L2_PIX_FMT_GREY:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_GRAY;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_GRAY;
        jpeg_enc_input_src_size = config->width * config->height;
        break;
    case V4L2_PIX_FMT_RGB565:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
        jpeg_enc_input_src_size = config->width * config->height * 2;
        break;
    case V4L2_PIX_FMT_RGB24:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_RGB888;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV444;
        jpeg_enc_input_src_size = config->width * config->height * 3;
        break;
    case V4L2_PIX_FMT_UYVY:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
        jpeg_enc_input_src_size = config->width * config->height * 2;
        break;
#if CONFIG_ESP32P4_REV_MIN_FULL >= 300
    case V4L2_PIX_FMT_YUV420:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_YUV420;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV420;
        jpeg_enc_input_src_size = config->width * config->height * 3 / 2;
        break;
    case V4L2_PIX_FMT_YUV444:
        jpeg_enc_config.src_type = JPEG_ENCODE_IN_FORMAT_YUV444;
        jpeg_enc_config.sub_sample = JPEG_DOWN_SAMPLING_YUV444;
        jpeg_enc_input_src_size = config->width * config->height * 3;
        break;
#endif
    default:
        ESP_LOGE(TAG, "Unsupported format");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!s_jpeg_hw_handle) {
        jpeg_encode_engine_cfg_t encode_eng_cfg = {
            .timeout_ms = 5000,
        };
        ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&encode_eng_cfg, &jpeg_handle), TAG, "failed to create jpeg encoder engine");
    }

    encoder = (example_encoder_t *)calloc(1, sizeof(example_encoder_t));
    ESP_GOTO_ON_FALSE(encoder, ESP_ERR_NO_MEM, fail0, TAG, "failed to alloc example encoder");

    encoder->jpeg_enc_config = jpeg_enc_config;
    if (!s_jpeg_hw_ref_count) {
        s_jpeg_hw_ref_count++;
        s_jpeg_hw_handle = jpeg_handle;
    }

    encoder->jpeg_out_buf_size = jpeg_enc_input_src_size * 3 / 4;

    *ret_handle = encoder;

    return ESP_OK;

fail0:
    jpeg_del_encoder_engine(jpeg_handle);
    return ret;
}

/**
 * @brief Get the encoder output buffer
 *
 * @param handle Encoder handle
 * @param buf Output buffer
 * @param size Output buffer size
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_alloc_output_buffer(example_encoder_handle_t handle, uint8_t **buf, uint32_t *size)
{
    if (!s_jpeg_hw_ref_count) {
        ESP_LOGE(TAG, "jpeg hardware encoder is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *jpeg_out_buf;
    example_encoder_t *encoder = (example_encoder_t *)handle;
    if (!encoder || !buf || !size) {
        ESP_LOGE(TAG, "invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    size_t jpeg_out_buf_size;
    jpeg_encode_memory_alloc_cfg_t jpeg_enc_output_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    // Note that a larger JPEG_ENC_QUALITY means better image quality, so you need to increase the allocated buffer size
    jpeg_out_buf = (uint8_t *)jpeg_alloc_encoder_mem(encoder->jpeg_out_buf_size, &jpeg_enc_output_mem_cfg, &jpeg_out_buf_size);
    ESP_RETURN_ON_FALSE(jpeg_out_buf, ESP_ERR_NO_MEM, TAG, "failed to alloc jpeg output buf");

    *buf = jpeg_out_buf;
    *size = jpeg_out_buf_size;

    return ESP_OK;
}

/**
 * @brief Free the encoder output buffer
 *
 * @param handle Encoder handle
 * @param buf Output buffer
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_free_output_buffer(example_encoder_handle_t handle, uint8_t *buf)
{
    if (!s_jpeg_hw_ref_count) {
        ESP_LOGE(TAG, "jpeg hardware encoder is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!buf || !handle) {
        ESP_LOGE(TAG, "invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    free(buf);
    return ESP_OK;
}

/**
 * @brief Process the encoder
 *
 * @param handle Encoder handle
 * @param src_buf Source buffer
 * @param src_size Source buffer size
 * @param dst_buf Destination buffer
 * @param dst_size Destination buffer size
 * @param dst_size_out Output destination buffer size
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_process(example_encoder_handle_t handle, uint8_t *src_buf, uint32_t src_size,
                                  uint8_t *dst_buf, uint32_t dst_size, uint32_t *dst_size_out)
{
    if (!s_jpeg_hw_ref_count) {
        ESP_LOGE(TAG, "jpeg hardware encoder is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!handle || !src_buf || !src_size || !dst_buf || !dst_size || !dst_size_out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    example_encoder_t *encoder = (example_encoder_t *)handle;

    ret = jpeg_encoder_process(s_jpeg_hw_handle, &encoder->jpeg_enc_config, src_buf, src_size, dst_buf, dst_size, dst_size_out);

    return ret;
}

/**
 * @brief Set the JPEG quality
 *
 * @param handle Encoder handle
 * @param quality JPEG quality
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_set_jpeg_quality(example_encoder_handle_t handle, uint8_t quality)
{
    example_encoder_t *encoder = (example_encoder_t *)handle;
    if (!encoder) {
        ESP_LOGE(TAG, "example encoder is not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    encoder->jpeg_enc_config.image_quality = quality;
    return ESP_OK;
}

/**
 * @brief Deinitialize the encoder
 *
 * @param handle Encoder handle
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_deinit(example_encoder_handle_t handle)
{
    example_encoder_t *encoder = (example_encoder_t *)handle;
    if (!encoder) {
        ESP_LOGE(TAG, "example encoder is not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_jpeg_hw_ref_count) {
        s_jpeg_hw_ref_count--;
        if (!s_jpeg_hw_ref_count) {
            jpeg_del_encoder_engine(s_jpeg_hw_handle);
            s_jpeg_hw_handle = NULL;
        }
    } else {
        ESP_LOGW(TAG, "jpeg hardware encoder ref count already 0, possible double deinit");
    }
    free(encoder);

    return ESP_OK;
}
