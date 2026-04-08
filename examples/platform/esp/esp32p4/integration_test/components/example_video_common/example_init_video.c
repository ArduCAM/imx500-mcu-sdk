/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <inttypes.h>

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "example_video_common.h"
#include "esp_cam_sensor_xclk.h"

static esp_video_init_csi_config_t s_csi_config = {
    .sccb_config = {
        .init_sccb = false,
        .i2c_handle = NULL,
        .freq = EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
    },
    .reset_pin = EXAMPLE_MIPI_CSI_CAM_SENSOR_RESET_PIN,
    .pwdn_pin  = EXAMPLE_MIPI_CSI_CAM_SENSOR_PWDN_PIN,
#if CONFIG_EXAMPLE_MIPI_CSI_VIDEO_DEVICE_DONT_INIT_LDO
    .dont_init_ldo = true,
#endif /* CONFIG_EXAMPLE_MIPI_CSI_VIDEO_DEVICE_DONT_INIT_LDO */
};

static const esp_cam_sensor_isp_info_t s_imx500_external_isp_info = {
    .isp_v1_info = {
        .version = 1,
        .pclk = 837000000,
        .hts = 12518,
        .vts = 2248,
        .exp_def = 1121,
        .gain_def = 500,
        .tline_ns = 14955,
        .bayer_type = ESP_CAM_SENSOR_BAYER_RGGB,
    },
};

static const esp_cam_sensor_format_t s_imx500_external_format = {
    .name = "IMX500_MIPI_2lane_RAW10_1024x600_10fps_external",
    .format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .port = ESP_CAM_SENSOR_MIPI_CSI,
    .xclk = 0,
    .width = 1024,
    .height = 600,
    .regs = NULL,
    .regs_size = 0,
    .fps = 10,
    .isp_info = &s_imx500_external_isp_info,
    .mipi_info = {
        .mipi_clk = 640000000,
        .lane_num = 2,
        .line_sync_en = false,
    },
    .reserved = NULL,
};

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
static esp_video_init_cam_motor_config_t s_cam_motor_config = {
    .sccb_config = {
        .init_sccb = false,
        .i2c_handle = NULL,
        .freq      = EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ,
    },
    .reset_pin = EXAMPLE_MIPI_CSI_CAM_MOTOR_RESET_PIN,
    .pwdn_pin  = EXAMPLE_MIPI_CSI_CAM_MOTOR_PWDN_PIN,
    .signal_pin = EXAMPLE_MIPI_CSI_CAM_MOTOR_SIGNAL_PIN,
};
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

#if defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0
static esp_cam_sensor_xclk_handle_t s_xclk_handle;
#endif /* defined(EXAMPLE_MIPI_CSI_XCLK_PIN) && EXAMPLE_MIPI_CSI_XCLK_PIN > 0 */

static i2c_master_bus_handle_t s_camera_i2c_bus_handle;
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
static i2c_master_bus_handle_t s_motor_i2c_bus_handle;
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */
static bool s_is_control_ready = false;
static bool s_is_init = false;
static const char *TAG = "example_init_video";

static esp_err_t example_video_init_i2c_bus(i2c_port_num_t port, gpio_num_t scl_pin,
                                            gpio_num_t sda_pin, uint32_t freq_hz,
                                            i2c_master_bus_handle_t *ret_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = port,
        .scl_io_num = scl_pin,
        .sda_io_num = sda_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t handle = NULL;

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &handle), TAG, "failed to create i2c master bus");

    if (ret_handle != NULL) {
        *ret_handle = handle;
    }

    ESP_LOGI(TAG, "created shared I2C master bus on port=%d scl=%d sda=%d freq=%" PRIu32,
             port, scl_pin, sda_pin, freq_hz);
    return ESP_OK;
}

static void example_video_apply_i2c_handles(void)
{
    s_csi_config.sccb_config.i2c_handle = s_camera_i2c_bus_handle;
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
    s_cam_motor_config.sccb_config.i2c_handle = s_motor_i2c_bus_handle;
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */
}

static void example_video_clear_i2c_handles(void)
{
    s_csi_config.sccb_config.i2c_handle = NULL;
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
    s_cam_motor_config.sccb_config.i2c_handle = NULL;
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */
}

i2c_master_bus_handle_t example_video_get_i2c_bus_handle(void)
{
    return s_camera_i2c_bus_handle;
}

/**
 * @brief Prepare the camera control path only
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_prepare_camera_control(void)
{
    esp_err_t ret;

    if (s_is_control_ready) {
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

    ESP_GOTO_ON_ERROR(example_video_init_i2c_bus(EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
                                                 EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN,
                                                 EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN,
                                                 EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
                                                 &s_camera_i2c_bus_handle),
                      failed_2, TAG, "failed to init camera i2c bus");

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
    if (EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT == EXAMPLE_MIPI_CSI_SCCB_I2C_PORT &&
            EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SCL_PIN == EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN &&
            EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SDA_PIN == EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN) {
        s_motor_i2c_bus_handle = s_camera_i2c_bus_handle;
    } else {
        ESP_GOTO_ON_ERROR(example_video_init_i2c_bus(EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_PORT,
                                                     EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SCL_PIN,
                                                     EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_SDA_PIN,
                                                     EXAMPLE_MIPI_CSI_CAM_MOTOR_SCCB_I2C_FREQ,
                                                     &s_motor_i2c_bus_handle),
                          failed_3, TAG, "failed to init motor i2c bus");
    }
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

    example_video_apply_i2c_handles();
    s_is_control_ready = true;

    return ESP_OK;

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
failed_3:
    if (s_motor_i2c_bus_handle != NULL && s_motor_i2c_bus_handle != s_camera_i2c_bus_handle) {
        i2c_del_master_bus(s_motor_i2c_bus_handle);
        s_motor_i2c_bus_handle = NULL;
    }
#endif
    if (s_camera_i2c_bus_handle != NULL) {
        i2c_del_master_bus(s_camera_i2c_bus_handle);
        s_camera_i2c_bus_handle = NULL;
    }
    example_video_clear_i2c_handles();
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
 * @brief Initialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
static esp_err_t example_video_init_internal(bool preserve_sensor_state)
{
    esp_video_init_csi_config_t csi_config = s_csi_config;
    esp_video_init_config_t cam_config = {
        .csi = &csi_config,
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
        .cam_motor = &s_cam_motor_config,
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */
    };

    if (s_is_init) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(example_video_prepare_camera_control(), TAG, "failed to prepare camera control");
    if (preserve_sensor_state) {
        csi_config.preserve_sensor_state = true;
        csi_config.external_format = &s_imx500_external_format;
        ESP_LOGI(TAG, "initializing video pipeline with preserved sensor state, external format=%s",
                 s_imx500_external_format.name);
    }
    ESP_RETURN_ON_ERROR(esp_video_init(&cam_config), TAG, "failed to initialize video");

    s_is_init = true;
    return ESP_OK;
}

esp_err_t example_video_init(void)
{
    return example_video_init_internal(false);
}

esp_err_t example_video_init_preserving_sensor_state(void)
{
    return example_video_init_internal(true);
}

/**
 * @brief Deinitialize the video system
 *
 * @return ESP_OK on success or other value on failure
 */
esp_err_t example_video_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (!s_is_init && !s_is_control_ready) {
        return ESP_OK;
    }

    if (s_is_init) {
        ESP_RETURN_ON_ERROR(esp_video_deinit(), TAG, "failed to deinitialize video");
        s_is_init = false;
    }

#if EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR
    if (s_motor_i2c_bus_handle != NULL && s_motor_i2c_bus_handle != s_camera_i2c_bus_handle) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_motor_i2c_bus_handle), TAG, "failed to free motor i2c bus");
    }
    s_motor_i2c_bus_handle = NULL;
#endif /* EXAMPLE_ENABLE_MIPI_CSI_CAM_MOTOR */

    if (s_camera_i2c_bus_handle != NULL) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_camera_i2c_bus_handle), TAG, "failed to free camera i2c bus");
        s_camera_i2c_bus_handle = NULL;
    }
    example_video_clear_i2c_handles();

#if EXAMPLE_MIPI_CSI_XCLK_PIN > 0
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_stop(s_xclk_handle), TAG, "failed to stop xclk");
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_free(s_xclk_handle), TAG, "failed to free xclk");
    s_xclk_handle = NULL;
#endif

    s_is_control_ready = false;
    return ret;
}
