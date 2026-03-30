/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_log.h"
#include "esp_check.h"
#include "example_video_common.h"
#include "esp_cam_sensor_xclk.h"

static const esp_video_init_csi_config_t s_csi_config = {
    .sccb_config = {
        .init_sccb = true,
        .i2c_config = {
            .port      = EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
            .scl_pin   = EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN,
            .sda_pin   = EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN,
        },
        .freq = EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
    },
    .reset_pin = EXAMPLE_MIPI_CSI_CAM_SENSOR_RESET_PIN,
    .pwdn_pin  = EXAMPLE_MIPI_CSI_CAM_SENSOR_PWDN_PIN,
#if CONFIG_EXAMPLE_MIPI_CSI_VIDEO_DEVICE_DONT_INIT_LDO
    .dont_init_ldo = true,
#endif /* CONFIG_EXAMPLE_MIPI_CSI_VIDEO_DEVICE_DONT_INIT_LDO */
};

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
static const esp_video_init_cam_motor_config_t s_cam_motor_config = {
    .sccb_config = {
        .init_sccb = true,
        .i2c_config = {
            .port      = EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT,
            .scl_pin   = EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SCL_PIN,
            .sda_pin   = EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SDA_PIN,
        },
        .freq      = EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ,
    },
    .reset_pin = EXAMPLE_MIPI_CSI_CAM_MOTOR_RESET_PIN,
    .pwdn_pin  = EXAMPLE_MIPI_CSI_CAM_MOTOR_PWDN_PIN,
    .signal_pin = EXAMPLE_MIPI_CSI_CAM_MOTOR_SIGNAL_PIN,
};
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

static const esp_video_init_config_t s_cam_config = {
    .csi      = &s_csi_config,
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
    .cam_motor = &s_cam_motor_config,
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */
};

#if defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0
static esp_cam_sensor_xclk_handle_t s_xclk_handle;
#endif /* defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0 */

static bool s_is_init = false;
static const char *TAG = "example_init_video";

/**
 * @brief Initialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_init(void)
{
    esp_err_t ret;

    if (s_is_init) {
        return ESP_OK;
    }

#if defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0
    esp_cam_sensor_xclk_config_t cam_xclk_config = {
        .esp_clock_router_cfg = {
            .xclk_pin = EXAMPLE_MIPI_CSI_XCLK_PIN,
            .xclk_freq_hz = EXAMPLE_MIPI_CSI_XCLK_FREQ,
        }
    };

    ESP_LOGI(TAG, "MIPI-CSI xclk pin=%d, freq=%d", EXAMPLE_MIPI_CSI_XCLK_PIN, EXAMPLE_MIPI_CSI_XCLK_FREQ);

    ESP_GOTO_ON_ERROR(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &s_xclk_handle), failed_0, TAG, "failed to allocate xclk");
    ESP_GOTO_ON_ERROR(esp_cam_sensor_xclk_start(s_xclk_handle, &cam_xclk_config), failed_1, TAG, "failed to start xclk");
#endif /* defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0 */

    ESP_LOGI(TAG, "MIPI-CSI camera sensor I2C port=%d, scl_pin=%d, sda_pin=%d, freq=%d",
             EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
             EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN,
             EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN,
             EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ);

    ESP_GOTO_ON_ERROR(esp_video_init(&s_cam_config), failed_2, TAG, "failed to initialize video");

    s_is_init = true;

    return ESP_OK;


failed_2:
#if EXAMPLE_MIPI_CSI_XCLK_PIN > 0
    esp_cam_sensor_xclk_stop(s_xclk_handle);
failed_1:
    esp_cam_sensor_xclk_free(s_xclk_handle);
    s_xclk_handle = NULL;
failed_0:
#endif
    return ret;
}

/**
 * @brief Deinitialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (!s_is_init) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_video_deinit(), TAG, "failed to deinitialize video");

#if EXAMPLE_MIPI_CSI_XCLK_PIN > 0
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_stop(s_xclk_handle), TAG, "failed to stop xclk");
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_free(s_xclk_handle), TAG, "failed to free xclk");
    s_xclk_handle = NULL;
#endif

    s_is_init = false;

    return ret;
}
