/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include "linux/videodev2.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "example_video_common_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MIPI-CSI camera sensor common configuration
 */
#define EXAMPLE_MIPI_CSI_SCCB_I2C_PORT                  CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT

#define EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ                  CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ

#if CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
#define EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR              1
#define EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT       CONFIG_EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT

#define EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ       CONFIG_EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ
#endif /* CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

/**
 * @brief Example camera device path configuration
 */
#define EXAMPLE_CAM_DEV_PATH                            ESP_VIDEO_MIPI_CSI_DEVICE_NAME

/**
 * @brief Example encoder handle
 */
typedef void *example_encoder_handle_t;

/**
 * @brief Example encoder configuration
 */
typedef struct example_encoder_config {
    uint32_t width;             /**< Image width */
    uint32_t height;            /**< Image height */
    uint32_t pixel_format;      /**< Input image pixel format in V4L2 format */
    uint8_t quality;            /**< Image quality */
} example_encoder_config_t;

/**
 * @brief Initialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_init(void);

/**
 * @brief Deinitialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_deinit(void);

/**
 * @brief Initialize the encoder
 *
 * @param config Encoder configuration
 * @param ret_handle Encoder handle
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_init(example_encoder_config_t *config, example_encoder_handle_t *ret_handle);

/**
 * @brief Get the encoder output buffer
 *
 * @param handle Encoder handle
 * @param buf Output buffer
 * @param size Output buffer size
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_alloc_output_buffer(example_encoder_handle_t handle, uint8_t **buf, uint32_t *size);

/**
 * @brief Free the encoder output buffer
 *
 * @param handle Encoder handle
 * @param buf Output buffer
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_free_output_buffer(example_encoder_handle_t handle, uint8_t *buf);

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
esp_err_t example_encoder_process(example_encoder_handle_t handle, uint8_t *src_buf, uint32_t src_size, uint8_t *dst_buf, uint32_t dst_size, uint32_t *dst_size_out);

/**
 * @brief Set the JPEG quality
 *
 * @param handle Encoder handle
 * @param quality JPEG quality
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_set_jpeg_quality(example_encoder_handle_t handle, uint8_t quality);

/**
 * @brief Deinitialize the encoder
 *
 * @param handle Encoder handle
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_encoder_deinit(example_encoder_handle_t handle);

#ifdef __cplusplus
}
#endif
