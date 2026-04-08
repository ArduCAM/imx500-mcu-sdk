/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "example_video_common.h"
#include "imx500_sdk_integration_test.h"

static const char *TAG = "imx500_app";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "preparing camera control for the fixed IMX500 integration flow");
    ESP_ERROR_CHECK(example_video_prepare_camera_control());

    ESP_LOGI(TAG, "starting IMX500 SDK integration flow");
    ESP_ERROR_CHECK(imx500_sdk_integration_test_run());
}
