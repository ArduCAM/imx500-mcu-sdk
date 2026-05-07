#ifndef DISPLAY_COMPOSE_HELPER_H
#define DISPLAY_COMPOSE_HELPER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t input_full_width;
    uint32_t input_full_height;
    uint32_t input_offset_x;
    uint32_t input_offset_y;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
} ai_camera_lcd_compose_info_t;

esp_err_t ai_camera_lcd_compose_init(void);
esp_err_t ai_camera_lcd_compose_frame(void *display_buf,
                                      const void *camera_buf,
                                      uint32_t camera_w,
                                      uint32_t camera_h,
                                      uint32_t *display_w,
                                      uint32_t *display_h,
                                      ai_camera_lcd_compose_info_t *compose_info);

#ifdef __cplusplus
}
#endif

#endif
