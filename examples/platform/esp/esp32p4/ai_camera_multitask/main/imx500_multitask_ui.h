#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_lcd_types.h"

struct Imx500HudInfo {
    const char *model_name = nullptr;
    const char *task_name = nullptr;
    const char *summary_line_1 = nullptr;
    const char *summary_line_2 = nullptr;
    bool spi_metadata_enabled = true;
};

struct Imx500ModalInfo {
    bool active = false;
    const char *line_1 = nullptr;
    const char *line_2 = nullptr;
};

uint16_t imx500_rgb565_from_rgb888(uint8_t r, uint8_t g, uint8_t b);
uint16_t imx500_blend_rgb565(uint16_t base, uint8_t overlay_r, uint8_t overlay_g, uint8_t overlay_b, uint8_t alpha);
void imx500_fill_rectangle_rgb565(uint16_t *buffer, int width, int height,
                                  int x1, int y1, int x2, int y2,
                                  uint8_t r, uint8_t g, uint8_t b);
int imx500_measure_text_width(const char *text, int scale);
void imx500_draw_text_rgb565(uint16_t *buffer, int width, int height,
                             int x, int y, const char *text,
                             uint8_t r, uint8_t g, uint8_t b, int scale);
void imx500_present_status_screen(esp_lcd_panel_handle_t panel,
                                  void *const *lcd_buffers,
                                  size_t lcd_buffer_count,
                                  const std::string &title,
                                  const std::string &line1,
                                  const std::string &line2);
void imx500_render_hud(uint16_t *frame, uint32_t frame_w, uint32_t frame_h, const Imx500HudInfo &info);
void imx500_render_modal(uint16_t *frame, uint32_t frame_w, uint32_t frame_h, const Imx500ModalInfo &info);
