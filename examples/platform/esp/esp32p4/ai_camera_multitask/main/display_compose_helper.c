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
                                      uint32_t *display_h)
{
    ppa_srm_oper_config_t srm_config = {0};

    ESP_RETURN_ON_FALSE(display_buf != NULL && camera_buf != NULL, ESP_ERR_INVALID_ARG,
                        "display_compose", "invalid input buffers");
    ESP_RETURN_ON_ERROR(ai_camera_lcd_compose_init(), "display_compose", "compose init failed");

    srm_config.in.buffer = camera_buf;
    srm_config.in.pic_w = camera_w;
    srm_config.in.pic_h = camera_h;
    srm_config.in.block_w = (camera_w > EXAMPLE_LCD_H_RES) ? EXAMPLE_LCD_H_RES : camera_w;
    srm_config.in.block_h = (camera_h > EXAMPLE_LCD_V_RES) ? EXAMPLE_LCD_V_RES : camera_h;
    srm_config.in.block_offset_x = (camera_w > EXAMPLE_LCD_H_RES) ? (camera_w - EXAMPLE_LCD_H_RES) / 2 : 0;
    srm_config.in.block_offset_y = (camera_h > EXAMPLE_LCD_V_RES) ? (camera_h - EXAMPLE_LCD_V_RES) / 2 : 0;
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
    srm_config.scale_x = 1.0f;
    srm_config.scale_y = 1.0f;
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
        *display_w = srm_config.in.block_w;
    }
    if (display_h) {
        *display_h = srm_config.in.block_h;
    }
    return ESP_OK;
}
