/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "example_video_common_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MIPI-CSI camera sensor common configuration
 */
#ifdef CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT
#define EXAMPLE_MIPI_CSI_SCCB_I2C_PORT                  CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT
#else
#define EXAMPLE_MIPI_CSI_SCCB_I2C_PORT                  0
#endif

#ifdef CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ
#define EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ                  CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ
#else
#define EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ                  100000
#endif

#if defined(CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR) && CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
#define EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR              1
#define EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT       CONFIG_EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT

#define EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ       CONFIG_EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ
#else
#define EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR              0
#endif /* CONFIG_EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

/**
 * @brief Example camera device path configuration
 */
#define EXAMPLE_CAM_DEV_PATH                            "/dev/video0"

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
 * @brief Prepare the camera control path only.
 *
 * This starts XCLK and creates the shared I2C bus used for camera control,
 * but does not initialize the esp_video sensor/video pipeline.
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_prepare_camera_control(void);

/**
 * @brief Prepare camera control and keep compatibility with callers that
 *        still use the old example-video initialization entry point.
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_init(void);

/**
 * @brief Prepare camera control while preserving the current sensor state.
 *
 * In this integration-test project the esp_video pipeline is not used, so
 * this function is equivalent to example_video_init().
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_init_preserving_sensor_state(void);

/**
 * @brief Get the shared I2C master bus used for camera control.
 *
 * @return I2C master bus handle, or NULL if camera control has not been prepared yet.
 */
i2c_master_bus_handle_t example_video_get_i2c_bus_handle(void);

/**
 * @brief Deinitialize the prepared camera-control resources
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
