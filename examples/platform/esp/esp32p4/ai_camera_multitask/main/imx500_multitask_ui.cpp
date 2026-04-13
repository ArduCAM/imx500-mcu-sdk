#include "imx500_multitask_ui.h"

#include <algorithm>
#include <cstring>

#include "app_drawing_utils.h"
#include "app_lcd.h"
#include "esp_lcd_panel_ops.h"
#include "imx500_multitask_postprocess.h"

namespace {

const uint8_t *font_glyph_5x7(char c)
{
    static const uint8_t kSpace[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t kDash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t kDot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t kColon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t kPercent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};

    static const uint8_t kDigits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1E},
    };

    static const uint8_t kUppercase[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E},
        {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22},
        {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41},
        {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A},
        {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41},
        {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F},
        {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E},
        {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E},
        {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F},
        {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x7F, 0x20, 0x18, 0x20, 0x7F},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43},
    };

    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return kUppercase[c - 'A'];
    }
    if (c >= '0' && c <= '9') {
        return kDigits[c - '0'];
    }
    switch (c) {
    case '-':
        return kDash;
    case '.':
        return kDot;
    case ':':
        return kColon;
    case '%':
        return kPercent;
    case ' ':
    default:
        return kSpace;
    }
}

void rgb888_from_rgb565(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r != nullptr) {
        *r = static_cast<uint8_t>(((pixel >> 11) & 0x1Fu) << 3);
    }
    if (g != nullptr) {
        *g = static_cast<uint8_t>(((pixel >> 5) & 0x3Fu) << 2);
    }
    if (b != nullptr) {
        *b = static_cast<uint8_t>((pixel & 0x1Fu) << 3);
    }
}

} // namespace

uint16_t imx500_rgb565_from_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

uint16_t imx500_blend_rgb565(uint16_t base, uint8_t overlay_r, uint8_t overlay_g, uint8_t overlay_b, uint8_t alpha)
{
    uint8_t base_r = 0;
    uint8_t base_g = 0;
    uint8_t base_b = 0;
    rgb888_from_rgb565(base, &base_r, &base_g, &base_b);

    const uint32_t inv_alpha = 255u - alpha;
    const uint8_t out_r = static_cast<uint8_t>((static_cast<uint32_t>(base_r) * inv_alpha + static_cast<uint32_t>(overlay_r) * alpha) / 255u);
    const uint8_t out_g = static_cast<uint8_t>((static_cast<uint32_t>(base_g) * inv_alpha + static_cast<uint32_t>(overlay_g) * alpha) / 255u);
    const uint8_t out_b = static_cast<uint8_t>((static_cast<uint32_t>(base_b) * inv_alpha + static_cast<uint32_t>(overlay_b) * alpha) / 255u);
    return imx500_rgb565_from_rgb888(out_r, out_g, out_b);
}

void imx500_fill_rectangle_rgb565(uint16_t *buffer, int width, int height,
                                  int x1, int y1, int x2, int y2,
                                  uint8_t r, uint8_t g, uint8_t b)
{
    if (buffer == nullptr || width <= 0 || height <= 0) {
        return;
    }
    if (x1 > x2) {
        std::swap(x1, x2);
    }
    if (y1 > y2) {
        std::swap(y1, y2);
    }
    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(width - 1, x2);
    y2 = std::min(height - 1, y2);
    if (x1 > x2 || y1 > y2) {
        return;
    }

    const uint16_t colour = imx500_rgb565_from_rgb888(r, g, b);
    for (int y = y1; y <= y2; ++y) {
        uint16_t *row = buffer + y * width;
        for (int x = x1; x <= x2; ++x) {
            row[x] = colour;
        }
    }
}

int imx500_measure_text_width(const char *text, int scale)
{
    if (text == nullptr || scale <= 0) {
        return 0;
    }
    return static_cast<int>(std::strlen(text)) * (5 * scale + scale);
}

void imx500_draw_text_rgb565(uint16_t *buffer, int width, int height,
                             int x, int y, const char *text,
                             uint8_t r, uint8_t g, uint8_t b, int scale)
{
    if (buffer == nullptr || text == nullptr || scale <= 0) {
        return;
    }
    const uint16_t colour = imx500_rgb565_from_rgb888(r, g, b);
    int cursor_x = x;
    while (*text != '\0') {
        const uint8_t *glyph = font_glyph_5x7(*text);
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                if ((bits & (1u << row)) == 0u) {
                    continue;
                }
                for (int sx = 0; sx < scale; ++sx) {
                    for (int sy = 0; sy < scale; ++sy) {
                        const int px = cursor_x + col * scale + sx;
                        const int py = y + row * scale + sy;
                        if (px < 0 || px >= width || py < 0 || py >= height) {
                            continue;
                        }
                        buffer[py * width + px] = colour;
                    }
                }
            }
        }
        cursor_x += 6 * scale;
        ++text;
    }
}

void imx500_present_status_screen(esp_lcd_panel_handle_t panel,
                                  void *const *lcd_buffers,
                                  size_t lcd_buffer_count,
                                  const std::string &title,
                                  const std::string &line1,
                                  const std::string &line2)
{
    if (panel == nullptr || lcd_buffers == nullptr || lcd_buffer_count == 0) {
        return;
    }

    const std::string safe_title = imx500_sanitize_for_lcd(title, 28);
    const std::string safe_line_1 = imx500_sanitize_for_lcd(line1, 36);
    const std::string safe_line_2 = imx500_sanitize_for_lcd(line2, 36);

    for (size_t i = 0; i < lcd_buffer_count; ++i) {
        if (lcd_buffers[i] == nullptr) {
            continue;
        }
        uint16_t *frame = reinterpret_cast<uint16_t *>(lcd_buffers[i]);
        imx500_fill_rectangle_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 0, 0,
                                     EXAMPLE_LCD_H_RES - 1, EXAMPLE_LCD_V_RES - 1, 8, 16, 32);
        imx500_fill_rectangle_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 0, 0,
                                     EXAMPLE_LCD_H_RES - 1, 56, 0, 128, 196);
        imx500_fill_rectangle_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
                                     32, 120, EXAMPLE_LCD_H_RES - 32, 430, 12, 24, 42);
        draw_rectangle_rgb(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
                           32, 120, EXAMPLE_LCD_H_RES - 32, 430,
                           0, 0, 255, 210, 0, 3, false);

        imx500_draw_text_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 40, 16,
                                "IMX500 AI CAMERA", 255, 255, 255, 4);
        imx500_draw_text_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 80, 170,
                                safe_title.c_str(), 255, 210, 0, 5);
        imx500_draw_text_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 80, 260,
                                safe_line_1.c_str(), 255, 255, 255, 3);
        imx500_draw_text_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 80, 320,
                                safe_line_2.c_str(), 180, 220, 255, 3);
        imx500_draw_text_rgb565(frame, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 80, 380,
                                "PLEASE WAIT", 180, 220, 255, 2);
    }

    esp_lcd_panel_draw_bitmap(panel, 0, 0, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, lcd_buffers[0]);
}

void imx500_render_hud(uint16_t *frame, uint32_t frame_w, uint32_t frame_h, const Imx500HudInfo &info)
{
    if (frame == nullptr) {
        return;
    }

    imx500_fill_rectangle_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                                 0, 0, static_cast<int>(frame_w) - 1, 73, 0, 0, 0);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            12, 8, info.model_name != nullptr ? info.model_name : "NO MODEL",
                            255, 210, 0, 2);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            12, 30, info.task_name != nullptr ? info.task_name : "UNKNOWN",
                            180, 220, 255, 2);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            280, 10, info.summary_line_1 != nullptr ? info.summary_line_1 : "WAITING FOR AI RESULT",
                            255, 255, 255, 2);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            280, 34, info.summary_line_2 != nullptr ? info.summary_line_2 : "PRESS BOOT TO SWITCH MODEL",
                            160, 220, 255, 2);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            static_cast<int>(frame_w) - 250, 10,
                            info.spi_metadata_enabled ? "BOOT NEXT MODEL" : "SPI RECOVERING",
                            info.spi_metadata_enabled ? 120 : 255,
                            info.spi_metadata_enabled ? 255 : 210,
                            info.spi_metadata_enabled ? 120 : 64, 2);
}

void imx500_render_modal(uint16_t *frame, uint32_t frame_w, uint32_t frame_h, const Imx500ModalInfo &info)
{
    if (frame == nullptr || !info.active) {
        return;
    }

    const int box_w = 420;
    const int box_h = 120;
    const int x1 = std::max<int>(0, static_cast<int>(frame_w - box_w) / 2);
    const int y1 = std::max<int>(0, static_cast<int>(frame_h - box_h) / 2);
    const int x2 = std::min<int>(static_cast<int>(frame_w) - 1, x1 + box_w);
    const int y2 = std::min<int>(static_cast<int>(frame_h) - 1, y1 + box_h);

    imx500_fill_rectangle_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                                 x1, y1, x2, y2, 0, 0, 0);
    draw_rectangle_rgb(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                       x1, y1, x2, y2, 0, 0, 255, 210, 0, 3, false);

    const int title_w = imx500_measure_text_width(info.line_1 != nullptr ? info.line_1 : "", 3);
    const int detail_w = imx500_measure_text_width(info.line_2 != nullptr ? info.line_2 : "", 2);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            std::max<int>(x1 + 16, static_cast<int>((frame_w - title_w) / 2)),
                            y1 + 18, info.line_1 != nullptr ? info.line_1 : "",
                            255, 210, 0, 3);
    imx500_draw_text_rgb565(frame, static_cast<int>(frame_w), static_cast<int>(frame_h),
                            std::max<int>(x1 + 16, static_cast<int>((frame_w - detail_w) / 2)),
                            y1 + 70, info.line_2 != nullptr ? info.line_2 : "",
                            255, 255, 255, 2);
}
