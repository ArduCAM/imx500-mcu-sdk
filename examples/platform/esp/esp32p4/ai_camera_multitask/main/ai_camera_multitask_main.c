/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "example_video_common.h"
#include "ai_camera_multitask.h"

static const char *TAG = "ai_camera_multitask";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

#if AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK
    ESP_LOGI(TAG, "preparing camera control for ai_camera_multitask");
    ESP_ERROR_CHECK(example_video_prepare_camera_control());

    ESP_LOGI(TAG, "starting ai_camera_multitask");
    ESP_ERROR_CHECK(ai_camera_multitask_run());
#else
    ESP_LOGI(TAG, "starting local pivariety camera preview");
    ESP_ERROR_CHECK(ai_camera_multitask_run_local_preview());
#endif
}
