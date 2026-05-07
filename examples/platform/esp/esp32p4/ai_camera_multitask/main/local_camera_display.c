/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#include <inttypes.h>

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"

#include "app_lcd.h"
#include "app_video.h"
#include "example_video_common.h"
#include "ai_camera_multitask.h"

static const char *TAG = "local_camera_display";

static esp_lcd_panel_handle_t s_display_panel;
static ppa_client_handle_t s_ppa_srm_handle;
static size_t s_data_cache_line_size;
static void *s_lcd_buffer[EXAMPLE_LCD_BUF_NUM];

static uint32_t ceil_div_u32(uint32_t numerator, uint32_t denominator)
{
    return denominator == 0 ? 0 : (numerator + denominator - 1) / denominator;
}

static uint32_t ppa_scale_numerator_for_fill(uint32_t source_size, uint32_t target_size)
{
    uint32_t numerator = ceil_div_u32(target_size * 16u, source_size);
    if (numerator == 0) {
        numerator = 1;
    }
    return numerator;
}

static uint32_t ppa_input_extent_for_exact_output(uint32_t source_size,
                                                  uint32_t target_size,
                                                  uint32_t scale_numerator)
{
    const uint32_t min_extent = ceil_div_u32(target_size * 16u, scale_numerator);
    const uint32_t max_extent = ((target_size + 1u) * 16u - 1u) / scale_numerator;
    uint32_t extent = max_extent < source_size ? max_extent : source_size;

    if (extent < min_extent) {
        extent = min_extent;
    }
    if (extent > min_extent && (extent & 1u)) {
        extent--;
    }
    return extent;
}

static void local_camera_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index,
                                         uint32_t camera_buf_hes, uint32_t camera_buf_ves,
                                         size_t camera_buf_len, void *user_data)
{
    const uint8_t display_buf_index = camera_buf_index % EXAMPLE_LCD_BUF_NUM;
    uint8_t *display_buf = (uint8_t *)s_lcd_buffer[display_buf_index];

    (void)camera_buf_len;
    (void)user_data;

    if (display_buf == NULL) {
        ESP_LOGW(TAG, "skip LCD frame because framebuffer[%u] is null", (unsigned)display_buf_index);
        return;
    }
    if (camera_buf_hes == 0 || camera_buf_ves == 0) {
        ESP_LOGW(TAG, "skip LCD frame because camera size is invalid: %" PRIu32 "x%" PRIu32,
                 camera_buf_hes, camera_buf_ves);
        return;
    }

    const uint32_t scale_numerator_x = ppa_scale_numerator_for_fill(camera_buf_hes, EXAMPLE_LCD_H_RES);
    const uint32_t scale_numerator_y = ppa_scale_numerator_for_fill(camera_buf_ves, EXAMPLE_LCD_V_RES);
    const uint32_t scale_numerator = scale_numerator_x > scale_numerator_y ? scale_numerator_x : scale_numerator_y;
    const uint32_t block_w = ppa_input_extent_for_exact_output(camera_buf_hes, EXAMPLE_LCD_H_RES, scale_numerator);
    const uint32_t block_h = ppa_input_extent_for_exact_output(camera_buf_ves, EXAMPLE_LCD_V_RES, scale_numerator);

    ppa_srm_oper_config_t srm_config = {
        .in.buffer = camera_buf,
        .in.pic_w = camera_buf_hes,
        .in.pic_h = camera_buf_ves,
        .in.block_w = block_w,
        .in.block_h = block_h,
        .in.block_offset_x = (camera_buf_hes - block_w) / 2u,
        .in.block_offset_y = (camera_buf_ves - block_h) / 2u,
        .in.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .out.buffer = display_buf,
        .out.buffer_size = ALIGN_UP(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * (APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? 2 : 3), s_data_cache_line_size),
        .out.pic_w = EXAMPLE_LCD_H_RES,
        .out.pic_h = EXAMPLE_LCD_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)scale_numerator / 16.0f,
        .scale_y = (float)scale_numerator / 16.0f,
        .rgb_swap = 0,
        .byte_swap = 0,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    if (camera_buf_hes > EXAMPLE_LCD_H_RES || camera_buf_ves > EXAMPLE_LCD_V_RES) {
        ESP_ERROR_CHECK(ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_config));
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_display_panel, 0, 0,
                                                  EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
                                                  display_buf));
    } else {
        memcpy(display_buf, camera_buf, camera_buf_len);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_display_panel, 0, 0,
                                                  camera_buf_hes, camera_buf_ves, display_buf));
    }
}

esp_err_t ai_camera_multitask_run_local_preview(void)
{
    ppa_client_config_t ppa_srm_config = {
        .oper_type = PPA_OPERATION_SRM,
    };

    ESP_LOGI(TAG, "starting local pivariety camera preview");
    ESP_RETURN_ON_ERROR(example_video_init(), TAG, "failed to initialize local video pipeline");
    ESP_RETURN_ON_ERROR(app_lcd_init(&s_display_panel), TAG, "failed to initialize LCD");
    ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_srm_config, &s_ppa_srm_handle), TAG, "failed to initialize PPA");
    ESP_RETURN_ON_ERROR(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_data_cache_line_size),
                        TAG, "failed to query cache alignment");

    int video_cam_fd = app_video_open((char *)EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    ESP_RETURN_ON_FALSE(video_cam_fd >= 0, ESP_FAIL, TAG, "video cam open failed");

#if EXAMPLE_LCD_BUF_NUM == 2
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_display_panel, 2, &s_lcd_buffer[0], &s_lcd_buffer[1]),
                        TAG, "failed to get LCD frame buffers");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_display_panel, 3, &s_lcd_buffer[0], &s_lcd_buffer[1], &s_lcd_buffer[2]),
                        TAG, "failed to get LCD frame buffers");
#endif

    ESP_LOGI(TAG, "using MMAP camera buffers for local preview");
    ESP_RETURN_ON_ERROR(app_video_set_bufs(video_cam_fd, EXAMPLE_CAM_BUF_NUM, NULL),
                        TAG, "failed to set camera buffers");
    ESP_RETURN_ON_ERROR(app_video_register_frame_operation_cb(local_camera_frame_operation),
                        TAG, "failed to register frame callback");
    ESP_RETURN_ON_ERROR(app_video_stream_task_start(video_cam_fd, 0, NULL),
                        TAG, "failed to start camera stream task");

    ESP_LOGI(TAG, "local preview running on LCD");
    return ESP_OK;
}
