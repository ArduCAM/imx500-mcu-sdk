#ifndef DISPLAY_COMPOSE_HELPER_H
#define DISPLAY_COMPOSE_HELPER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ai_camera_lcd_compose_init(void);
esp_err_t ai_camera_lcd_compose_frame(void *display_buf,
                                      const void *camera_buf,
                                      uint32_t camera_w,
                                      uint32_t camera_h,
                                      uint32_t *display_w,
                                      uint32_t *display_h);

#ifdef __cplusplus
}
#endif

#endif
