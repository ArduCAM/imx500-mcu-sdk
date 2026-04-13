/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include "esp_check.h"
#include "esp_log.h"

#include "example_video_common.h"
#include "imx500_sdk_integration_test.h"

static const char *TAG = "imx500_app";

void app_main(void)
{
    ESP_LOGI(TAG, "preparing camera control for IMX500 SPI metadata integration test");
    ESP_ERROR_CHECK(example_video_prepare_camera_control());

    ESP_LOGI(TAG, "starting IMX500 higherhrnet SPI metadata integration test");
    ESP_ERROR_CHECK(imx500_sdk_integration_test_run());
}
