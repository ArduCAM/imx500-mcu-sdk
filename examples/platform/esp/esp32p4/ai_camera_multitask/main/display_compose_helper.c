#include <stdint.h>

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_private/esp_cache_private.h"

#include "app_lcd.h"
#include "app_video.h"
#include "display_compose_helper.h"

static ppa_client_handle_t s_ppa_srm_handle;
static size_t s_data_cache_line_size;

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

esp_err_t ai_camera_lcd_compose_init(void)
{
    if (s_ppa_srm_handle == NULL) {
        ppa_client_config_t ppa_srm_config = {
            .oper_type = PPA_OPERATION_SRM,
            .max_pending_trans_num = 1,
            .data_burst_length = PPA_DATA_BURST_LENGTH_128,
        };
        ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_srm_config, &s_ppa_srm_handle), "display_compose", "failed to register PPA");
    }

    if (s_data_cache_line_size == 0) {
        ESP_RETURN_ON_ERROR(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_data_cache_line_size),
                            "display_compose", "failed to query cache alignment");
    }

    return ESP_OK;
}

esp_err_t ai_camera_lcd_compose_frame(void *display_buf,
                                      const void *camera_buf,
                                      uint32_t camera_w,
                                      uint32_t camera_h,
                                      uint32_t *display_w,
                                      uint32_t *display_h,
                                      ai_camera_lcd_compose_info_t *compose_info)
{
    ppa_srm_oper_config_t srm_config = {0};

    ESP_RETURN_ON_FALSE(display_buf != NULL && camera_buf != NULL, ESP_ERR_INVALID_ARG,
                        "display_compose", "invalid input buffers");
    ESP_RETURN_ON_FALSE(camera_w != 0 && camera_h != 0, ESP_ERR_INVALID_ARG,
                        "display_compose", "invalid camera size");
    ESP_RETURN_ON_ERROR(ai_camera_lcd_compose_init(), "display_compose", "compose init failed");

    const uint32_t scale_numerator_x = ppa_scale_numerator_for_fill(camera_w, EXAMPLE_LCD_H_RES);
    const uint32_t scale_numerator_y = ppa_scale_numerator_for_fill(camera_h, EXAMPLE_LCD_V_RES);
    const uint32_t scale_numerator = scale_numerator_x > scale_numerator_y ? scale_numerator_x : scale_numerator_y;
    const uint32_t block_w = ppa_input_extent_for_exact_output(camera_w, EXAMPLE_LCD_H_RES, scale_numerator);
    const uint32_t block_h = ppa_input_extent_for_exact_output(camera_h, EXAMPLE_LCD_V_RES, scale_numerator);

    srm_config.in.buffer = camera_buf;
    srm_config.in.pic_w = camera_w;
    srm_config.in.pic_h = camera_h;
    srm_config.in.block_w = block_w;
    srm_config.in.block_h = block_h;
    srm_config.in.block_offset_x = (camera_w - block_w) / 2u;
    srm_config.in.block_offset_y = (camera_h - block_h) / 2u;
    srm_config.in.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888;
    srm_config.in.yuv_range = PPA_COLOR_RANGE_LIMIT;
    srm_config.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;

    srm_config.out.buffer = display_buf;
    srm_config.out.buffer_size = ALIGN_UP(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES *
                                          (APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? 2 : 3),
                                          s_data_cache_line_size);
    srm_config.out.pic_w = EXAMPLE_LCD_H_RES;
    srm_config.out.pic_h = EXAMPLE_LCD_V_RES;
    srm_config.out.block_offset_x = 0;
    srm_config.out.block_offset_y = 0;
    srm_config.out.srm_cm = APP_VIDEO_FMT == APP_VIDEO_FMT_RGB565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888;
    srm_config.out.yuv_range = PPA_COLOR_RANGE_LIMIT;
    srm_config.out.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;

    srm_config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    srm_config.scale_x = (float)scale_numerator / 16.0f;
    srm_config.scale_y = (float)scale_numerator / 16.0f;
    srm_config.mirror_x = false;
    srm_config.mirror_y = false;
    srm_config.rgb_swap = false;
    srm_config.byte_swap = false;
    srm_config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    srm_config.alpha_fix_val = 0;
    srm_config.mode = PPA_TRANS_MODE_BLOCKING;
    srm_config.user_data = NULL;

    ESP_RETURN_ON_ERROR(ppa_do_scale_rotate_mirror(s_ppa_srm_handle, &srm_config),
                        "display_compose", "failed to compose camera frame");

    if (display_w) {
        *display_w = EXAMPLE_LCD_H_RES;
    }
    if (display_h) {
        *display_h = EXAMPLE_LCD_V_RES;
    }
    if (compose_info) {
        compose_info->input_full_width = camera_w;
        compose_info->input_full_height = camera_h;
        compose_info->input_offset_x = srm_config.in.block_offset_x;
        compose_info->input_offset_y = srm_config.in.block_offset_y;
        compose_info->input_width = srm_config.in.block_w;
        compose_info->input_height = srm_config.in.block_h;
        compose_info->output_width = EXAMPLE_LCD_H_RES;
        compose_info->output_height = EXAMPLE_LCD_V_RES;
    }
    return ESP_OK;
}
