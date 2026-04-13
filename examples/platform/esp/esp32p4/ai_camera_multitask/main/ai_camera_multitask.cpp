#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>
#include <vector>
#include <sys/ioctl.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "linux/videodev2.h"
#include "driver/jpeg_decode.h"
#include "sdkconfig.h"

#include "ArducamIMX500SDK.h"
#include "app_drawing_utils.h"
#include "app_lcd.h"
#include "app_video.h"
#include "example_video_common.h"
#include "higherhrnet_postprocess.h"
#include "imx500_multitask_model_registry.h"
#include "imx500_multitask_postprocess.h"
#include "imx500_multitask_ui.h"
#include "ai_camera_multitask.h"
#include "peripherals_adapter.h"

static const char *TAG = "ai_camera_multitask";

namespace {

constexpr uint32_t kMaxFrameBufferSize = 384 * 1024;
constexpr uint16_t kSpiThumbMaxWidth = 192;
constexpr uint16_t kSpiThumbMaxHeight = 144;
constexpr uint32_t kSpiTaskStackSize = 10 * 1024;
constexpr uint32_t kBootButtonTaskStackSize = 4 * 1024;
constexpr uint32_t kMetadataDisableThreshold = 3;
constexpr uint32_t kAiResultExpiryUs = 1000000;
constexpr uint32_t kThumbDecodeMaxWidth = 384;
constexpr uint32_t kThumbDecodeMaxHeight = 288;
constexpr uint32_t kThumbDecodeBufferBytes = kThumbDecodeMaxWidth * kThumbDecodeMaxHeight * 2;
constexpr size_t kPsramFrameBufferAlignment = 64;
constexpr uint32_t kJpegAlignment = 1024;
constexpr uint32_t kJpegMinBlockBytes = 4 * 1024;
constexpr uint32_t kOverlayTextLen = 96;
constexpr uint32_t kHudHeight = 74;
constexpr uint32_t kBootButtonPollMs = 20;
constexpr uint32_t kBootButtonDebounceMs = 80;
constexpr uint32_t kModelSwitchNoticeMs = 1200;
constexpr gpio_num_t kBootButtonGpio = GPIO_NUM_35;
constexpr char kNvsNamespace[] = "imx500";
constexpr char kNvsModelIndexKey[] = "model_idx";
constexpr uint8_t kSegmentationAlpha = 110;
constexpr uint32_t kSkeleton[][2] = {
    {0, 1}, {0, 2}, {1, 3}, {2, 4},
    {5, 6}, {5, 11}, {11, 12}, {12, 6},
    {5, 7}, {7, 9}, {6, 8}, {8, 10},
    {11, 13}, {13, 15}, {12, 14}, {14, 16},
};
constexpr uint8_t kSegmentationColours[][3] = {
    {0, 0, 0},
    {128, 0, 0},
    {0, 128, 0},
    {128, 128, 0},
    {0, 0, 128},
    {128, 0, 128},
    {0, 128, 128},
    {128, 128, 128},
    {64, 0, 0},
    {192, 0, 0},
    {64, 128, 0},
    {192, 128, 0},
    {64, 0, 128},
    {192, 0, 128},
    {64, 128, 128},
    {192, 128, 128},
    {0, 64, 0},
    {128, 64, 0},
    {0, 192, 0},
    {128, 192, 0},
    {0, 64, 128},
};

uint8_t *g_frame_buf = nullptr;
uint32_t g_frame_buf_capacity = 0;
IMX500ParsedMetadata *g_parsed_metadata = nullptr;
TaskHandle_t g_spi_task_handle = nullptr;
TaskHandle_t g_boot_button_task_handle = nullptr;
SemaphoreHandle_t g_overlay_mutex = nullptr;
int g_video_fd = -1;
esp_lcd_panel_handle_t g_display_panel = nullptr;
void *g_lcd_buffers[EXAMPLE_LCD_BUF_NUM] = {};
void *g_camera_buffers[EXAMPLE_LCD_BUF_NUM] = {};
uint8_t g_lcd_next_compose_index = 0;
int g_lcd_last_presented_index = -1;
volatile uint32_t g_lcd_refresh_count = 0;
bool g_lcd_refresh_callback_attempted = false;
bool g_lcd_refresh_callback_registered = false;
uint32_t g_lcd_buffer_busy_until[EXAMPLE_LCD_BUF_NUM] = {};

uint8_t *g_spi_thumb_buffer = nullptr;
uint8_t *g_spi_thumb_staging = nullptr;
uint8_t *g_spi_thumb_snapshot = nullptr;
uint8_t *g_spi_jpeg_input_buffer = nullptr;
size_t g_spi_jpeg_input_buffer_size = 0;
uint8_t *g_spi_jpeg_full_buffer = nullptr;
size_t g_spi_jpeg_full_buffer_size = 0;
jpeg_decoder_handle_t g_jpeg_decoder = nullptr;
HigherhrnetResult *g_overlay_pose_snapshot = nullptr;
HigherhrnetResult *g_spi_hrnet_result = nullptr;
uint8_t *g_segmentation_mask = nullptr;
uint8_t *g_segmentation_mask_snapshot = nullptr;

const Imx500EmbeddedModelDescriptor *g_active_model = nullptr;
size_t g_active_model_index = 0;
size_t g_model_count = 0;
std::vector<std::string> g_active_labels;
bool g_switching_model = false;

struct SharedOverlayState {
    bool thumbnail_ready = false;
    uint16_t thumb_width = 0;
    uint16_t thumb_height = 0;
    uint64_t thumb_updated_us = 0;
    bool ai_overlay_enabled = true;
    bool spi_metadata_enabled = true;
    uint32_t zero_header_failures = 0;
    Imx500ClassificationOverlayState classification = {};
    Imx500DetectionOverlayState detection = {};
    HigherhrnetResult hrnet = {};
    Imx500SegmentationOverlayState segmentation = {};
    uint64_t result_updated_us = 0;
    bool modal_active = false;
    char summary_line_1[kOverlayTextLen] = {};
    char summary_line_2[kOverlayTextLen] = {};
    char modal_line_1[kOverlayTextLen] = {};
    char modal_line_2[kOverlayTextLen] = {};
};

struct LocalOverlaySnapshot {
    bool thumbnail_ready = false;
    uint16_t thumb_width = 0;
    uint16_t thumb_height = 0;
    uint64_t result_updated_us = 0;
    bool ai_overlay_enabled = true;
    bool spi_metadata_enabled = true;
    bool modal_active = false;
    Imx500ClassificationOverlayState classification = {};
    Imx500DetectionOverlayState detection = {};
    HigherhrnetResult hrnet = {};
    Imx500SegmentationOverlayState segmentation = {};
    char summary_line_1[kOverlayTextLen] = {};
    char summary_line_2[kOverlayTextLen] = {};
    char modal_line_1[kOverlayTextLen] = {};
    char modal_line_2[kOverlayTextLen] = {};
};

SharedOverlayState g_overlay_state = {};
LocalOverlaySnapshot g_display_overlay_snapshot = {};
volatile bool g_display_first_frame_seen = false;

static uint64_t now_us()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

static bool IRAM_ATTR lcd_refresh_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    g_lcd_refresh_count = g_lcd_refresh_count + 1u;
    return false;
}

static uint32_t align_up_u32(uint32_t value, uint32_t base)
{
    return ((value + base - 1) / base) * base;
}

static uint32_t calculate_jpeg_aligned_size(uint32_t jpeg_size)
{
    return std::max(kJpegMinBlockBytes, align_up_u32(jpeg_size + 3, kJpegAlignment));
}

static bool compute_spi_thumbnail_size(uint32_t src_width, uint32_t src_height,
                                       uint16_t *scaled_width, uint16_t *scaled_height)
{
    if (src_width == 0 || src_height == 0 || scaled_width == nullptr || scaled_height == nullptr) {
        return false;
    }

    uint32_t fit_width = src_width;
    uint32_t fit_height = src_height;
    if (fit_width > kSpiThumbMaxWidth || fit_height > kSpiThumbMaxHeight) {
        const uint64_t width_limited_height = (static_cast<uint64_t>(src_height) * kSpiThumbMaxWidth) / src_width;
        if (width_limited_height > 0 && width_limited_height <= kSpiThumbMaxHeight) {
            fit_width = kSpiThumbMaxWidth;
            fit_height = static_cast<uint32_t>(width_limited_height);
        } else {
            fit_height = kSpiThumbMaxHeight;
            fit_width = static_cast<uint32_t>((static_cast<uint64_t>(src_width) * kSpiThumbMaxHeight) / src_height);
        }
    }

    *scaled_width = static_cast<uint16_t>(std::max<uint32_t>(1, fit_width));
    *scaled_height = static_cast<uint16_t>(std::max<uint32_t>(1, fit_height));
    return true;
}

static inline uint16_t swap_rgb565_bytes(uint16_t pixel)
{
    return static_cast<uint16_t>((pixel << 8) | (pixel >> 8));
}

static void log_metadata_first_12_bytes(const uint8_t *data, uint32_t len, const char *prefix)
{
    char bytes[3 * 12 + 1] = {};
    uint32_t count = std::min<uint32_t>(12, len);
    char *p = bytes;
    for (uint32_t i = 0; i < count; ++i) {
        int written = snprintf(p, sizeof(bytes) - static_cast<size_t>(p - bytes), "%02X%s",
                               data[i], (i + 1 < count) ? " " : "");
        if (written <= 0) {
            break;
        }
        p += written;
    }
    ESP_LOGI(TAG, "%s first12=[%s]", prefix, bytes);
}

static void log_parsed_metadata_basic_info(const IMX500ParsedMetadata &parsed, uint32_t payload_size)
{
    const IMX500ParsedNetwork &network = parsed.networks[parsed.selected_network_index];
    ESP_LOGI(TAG,
             "metadata basic: payload=%" PRIu32 " primary(valid=%u frame=%u ap=%u ord=%u ind=%u) jpeg=%" PRIu32
             " output_off=%" PRIu32 " output_len=%" PRIu32 " networks=%u selected=%u outputs=%u",
             payload_size,
             parsed.primary_header.valid_flag,
             parsed.primary_header.frame_count,
             parsed.primary_header.size_of_ap_parameter,
             parsed.primary_header.network_ordinal,
             parsed.primary_header.indicator,
             parsed.jpeg_data_len,
             parsed.output_payload_offset,
             parsed.output_payload_length,
             parsed.network_count,
             parsed.selected_network_index,
             network.output_tensor_count);
}

static bool is_zero_header_payload(const uint8_t *data, uint32_t len)
{
    if (!data || len < 9) {
        return false;
    }
    for (uint32_t i = 0; i < 9; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

static void copy_text(char *dst, size_t dst_size, const std::string &src)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    std::snprintf(dst, dst_size, "%s", src.c_str());
}

static std::string lcd_model_name()
{
    if (g_active_model == nullptr) {
        return "NO MODEL";
    }
    return imx500_sanitize_for_lcd(g_active_model->display_name, 28);
}

static std::string lcd_task_name()
{
    if (g_active_model == nullptr) {
        return "UNKNOWN";
    }
    return imx500_sanitize_for_lcd(g_active_model->task_name, 20);
}

static void clear_overlay_results_locked()
{
    g_overlay_state.classification = {};
    g_overlay_state.detection = {};
    memset(&g_overlay_state.hrnet, 0, sizeof(g_overlay_state.hrnet));
    g_overlay_state.segmentation = {};
    g_overlay_state.result_updated_us = 0;
}

static void set_overlay_summary_locked(const std::string &line1, const std::string &line2)
{
    copy_text(g_overlay_state.summary_line_1, sizeof(g_overlay_state.summary_line_1), imx500_sanitize_for_lcd(line1, 46));
    copy_text(g_overlay_state.summary_line_2, sizeof(g_overlay_state.summary_line_2), imx500_sanitize_for_lcd(line2, 46));
}

static void set_overlay_modal_locked(bool active, const std::string &line1, const std::string &line2)
{
    g_overlay_state.modal_active = active;
    copy_text(g_overlay_state.modal_line_1, sizeof(g_overlay_state.modal_line_1), imx500_sanitize_for_lcd(line1, 32));
    copy_text(g_overlay_state.modal_line_2, sizeof(g_overlay_state.modal_line_2), imx500_sanitize_for_lcd(line2, 32));
}

static void clear_overlay_state_locked()
{
    g_overlay_state.thumbnail_ready = false;
    g_overlay_state.thumb_width = 0;
    g_overlay_state.thumb_height = 0;
    g_overlay_state.thumb_updated_us = 0;
    g_overlay_state.zero_header_failures = 0;
    g_overlay_state.ai_overlay_enabled = true;
    g_overlay_state.spi_metadata_enabled = true;
    g_overlay_state.modal_active = false;
    g_overlay_state.summary_line_1[0] = '\0';
    g_overlay_state.summary_line_2[0] = '\0';
    g_overlay_state.modal_line_1[0] = '\0';
    g_overlay_state.modal_line_2[0] = '\0';
    clear_overlay_results_locked();
}

static bool ensure_lcd_ready()
{
    if (g_display_panel == nullptr) {
        if (app_lcd_init(&g_display_panel) != ESP_OK) {
            ESP_LOGE(TAG, "failed to init LCD");
            return false;
        }
    }
    if (!g_lcd_refresh_callback_attempted) {
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
        callbacks.on_refresh_done = lcd_refresh_done_cb;
        const esp_err_t err = esp_lcd_dpi_panel_register_event_callbacks(g_display_panel, &callbacks, nullptr);
        g_lcd_refresh_callback_registered = (err == ESP_OK);
        g_lcd_refresh_callback_attempted = true;
        if (!g_lcd_refresh_callback_registered) {
            ESP_LOGW(TAG, "failed to register LCD refresh callback, using framebuffer ring fallback: %s", esp_err_to_name(err));
        }
    }
    bool need_lcd_buffers = false;
    for (uint8_t i = 0; i < EXAMPLE_LCD_BUF_NUM; ++i) {
        need_lcd_buffers = need_lcd_buffers || (g_lcd_buffers[i] == nullptr);
    }
    if (need_lcd_buffers) {
#if EXAMPLE_LCD_BUF_NUM >= 3
        if (esp_lcd_dpi_panel_get_frame_buffer(g_display_panel, 3, &g_lcd_buffers[0], &g_lcd_buffers[1],
                                               &g_lcd_buffers[2]) != ESP_OK) {
#else
        if (esp_lcd_dpi_panel_get_frame_buffer(g_display_panel, 2, &g_lcd_buffers[0], &g_lcd_buffers[1]) != ESP_OK) {
#endif
            ESP_LOGE(TAG, "failed to get LCD frame buffers");
            return false;
        }
        for (uint8_t i = 0; i < EXAMPLE_LCD_BUF_NUM; ++i) {
            if (g_lcd_buffers[i] == nullptr) {
                ESP_LOGE(TAG, "LCD frame buffer %u is null", static_cast<unsigned>(i));
                return false;
            }
        }
        if (g_lcd_last_presented_index < 0) {
            g_lcd_last_presented_index = 0;
            g_lcd_next_compose_index = 1 % EXAMPLE_LCD_BUF_NUM;
        }
    }
    return true;
}

static bool lcd_buffer_ready(uint8_t index)
{
    return index < EXAMPLE_LCD_BUF_NUM && g_lcd_buffers[index] != nullptr;
}

static bool lcd_buffer_available_for_compose(uint8_t index)
{
    if (!lcd_buffer_ready(index)) {
        return false;
    }
    if (!g_lcd_refresh_callback_registered) {
        return static_cast<int>(index) != g_lcd_last_presented_index;
    }
    return static_cast<int32_t>(g_lcd_refresh_count - g_lcd_buffer_busy_until[index]) >= 0;
}

static uint8_t select_lcd_compose_buffer_index(uint8_t camera_buf_index)
{
    for (uint8_t attempt = 0; attempt < EXAMPLE_LCD_BUF_NUM; ++attempt) {
        const uint8_t candidate = g_lcd_next_compose_index;
        g_lcd_next_compose_index = (g_lcd_next_compose_index + 1u) % EXAMPLE_LCD_BUF_NUM;
        if (lcd_buffer_available_for_compose(candidate)) {
            return candidate;
        }
    }
    for (uint8_t i = 0; i < EXAMPLE_LCD_BUF_NUM; ++i) {
        if (lcd_buffer_ready(i) && static_cast<int>(i) != g_lcd_last_presented_index) {
            return i;
        }
    }
    if (lcd_buffer_ready(camera_buf_index)) {
        return camera_buf_index;
    }
    return 0;
}

static bool load_selected_model_index(size_t *index_out)
{
    if (index_out == nullptr) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        *index_out = imx500_get_default_embedded_model_index();
        return false;
    }

    uint8_t stored = 0;
    const esp_err_t err = nvs_get_u8(handle, kNvsModelIndexKey, &stored);
    nvs_close(handle);
    if (err != ESP_OK) {
        *index_out = imx500_get_default_embedded_model_index();
        return false;
    }

    *index_out = stored;
    return true;
}

static bool save_selected_model_index(size_t index)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return false;
    }
    esp_err_t err = nvs_set_u8(handle, kNvsModelIndexKey, static_cast<uint8_t>(index));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save model index err=%d", static_cast<int>(err));
        return false;
    }
    return true;
}

static void select_active_model()
{
    imx500_get_embedded_models(&g_model_count);
    size_t selected_index = imx500_get_default_embedded_model_index();
    load_selected_model_index(&selected_index);
    if (selected_index >= g_model_count) {
        selected_index = imx500_get_default_embedded_model_index();
    }

    g_active_model_index = selected_index;
    g_active_model = imx500_get_embedded_model(selected_index);
    g_active_labels = (g_active_model != nullptr) ? imx500_parse_embedded_labels(g_active_model->labels) : std::vector<std::string>{};
}

static bool allocate_parsed_metadata_buffer()
{
    uint32_t reported_size = get_metadata_size();
    uint32_t desired_size = kMaxFrameBufferSize;

    if (!g_frame_buf || g_frame_buf_capacity < desired_size) {
        if (g_frame_buf) {
            free(g_frame_buf);
            g_frame_buf = nullptr;
            g_frame_buf_capacity = 0;
        }

        g_frame_buf = static_cast<uint8_t *>(heap_caps_calloc(1, desired_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (!g_frame_buf) {
            g_frame_buf = static_cast<uint8_t *>(calloc(1, desired_size));
        }
        if (!g_frame_buf) {
            ESP_LOGE(TAG, "failed to allocate metadata frame buffer");
            return false;
        }
        g_frame_buf_capacity = desired_size;
    }

    if (!g_parsed_metadata) {
        g_parsed_metadata = static_cast<IMX500ParsedMetadata *>(heap_caps_calloc(1, sizeof(IMX500ParsedMetadata), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (!g_parsed_metadata) {
            g_parsed_metadata = static_cast<IMX500ParsedMetadata *>(calloc(1, sizeof(IMX500ParsedMetadata)));
        }
        if (!g_parsed_metadata) {
            ESP_LOGE(TAG, "failed to allocate parsed metadata buffer");
            return false;
        }
    }
    ESP_LOGI(TAG, "allocated parsed metadata buffer: parsed=%u frame_buf=%u",
             static_cast<unsigned>(sizeof(IMX500ParsedMetadata)),
             static_cast<unsigned>(g_frame_buf_capacity));
    ESP_LOGI(TAG, "metadata size planning: reported=%u actual_alloc=%u",
             static_cast<unsigned>(reported_size),
             static_cast<unsigned>(g_frame_buf_capacity));
    return true;
}

static bool allocate_spi_thumbnail_buffers()
{
    if (!g_overlay_mutex) {
        g_overlay_mutex = xSemaphoreCreateMutex();
    }
    if (!g_overlay_mutex) {
        ESP_LOGE(TAG, "failed to create overlay mutex");
        return false;
    }

    if (!g_spi_thumb_buffer) {
        g_spi_thumb_buffer = static_cast<uint8_t *>(heap_caps_malloc(kSpiThumbMaxWidth * kSpiThumbMaxHeight * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_spi_thumb_staging) {
        g_spi_thumb_staging = static_cast<uint8_t *>(heap_caps_malloc(kSpiThumbMaxWidth * kSpiThumbMaxHeight * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_spi_thumb_snapshot) {
        g_spi_thumb_snapshot = static_cast<uint8_t *>(heap_caps_malloc(kSpiThumbMaxWidth * kSpiThumbMaxHeight * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_segmentation_mask) {
        g_segmentation_mask = static_cast<uint8_t *>(heap_caps_calloc(1, kSpiThumbMaxWidth * kSpiThumbMaxHeight, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_segmentation_mask_snapshot) {
        g_segmentation_mask_snapshot = static_cast<uint8_t *>(heap_caps_calloc(1, kSpiThumbMaxWidth * kSpiThumbMaxHeight, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_spi_jpeg_full_buffer) {
        jpeg_decode_memory_alloc_cfg_t out_mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        g_spi_jpeg_full_buffer = static_cast<uint8_t *>(
            jpeg_alloc_decoder_mem(kThumbDecodeBufferBytes, &out_mem_cfg, &g_spi_jpeg_full_buffer_size));
    }
    if (!g_spi_thumb_buffer || !g_spi_thumb_staging || !g_spi_thumb_snapshot || !g_spi_jpeg_full_buffer ||
        !g_segmentation_mask || !g_segmentation_mask_snapshot) {
        ESP_LOGE(TAG, "failed to allocate SPI thumbnail buffers");
        return false;
    }

    if (!g_overlay_pose_snapshot) {
        g_overlay_pose_snapshot = static_cast<HigherhrnetResult *>(heap_caps_calloc(1, sizeof(HigherhrnetResult), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_overlay_pose_snapshot) {
        g_overlay_pose_snapshot = static_cast<HigherhrnetResult *>(calloc(1, sizeof(HigherhrnetResult)));
    }
    if (!g_spi_hrnet_result) {
        g_spi_hrnet_result = static_cast<HigherhrnetResult *>(heap_caps_calloc(1, sizeof(HigherhrnetResult), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_spi_hrnet_result) {
        g_spi_hrnet_result = static_cast<HigherhrnetResult *>(calloc(1, sizeof(HigherhrnetResult)));
    }
    if (!g_overlay_pose_snapshot || !g_spi_hrnet_result) {
        ESP_LOGE(TAG, "failed to allocate HRNet pose buffers");
        return false;
    }

    if (!g_jpeg_decoder) {
        jpeg_decode_engine_cfg_t cfg = {
            .intr_priority = 0,
            .timeout_ms = 2000,
        };
        if (jpeg_new_decoder_engine(&cfg, &g_jpeg_decoder) != ESP_OK) {
            ESP_LOGE(TAG, "failed to create JPEG decoder");
            return false;
        }
    }

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        clear_overlay_state_locked();
        set_overlay_summary_locked("WAITING FOR AI RESULT", "PRESS BOOT TO SWITCH MODEL");
        xSemaphoreGive(g_overlay_mutex);
    }

    ESP_LOGI(TAG, "allocated SPI thumbnail buffers: %ux%u decode=%u alloc=%u hrnet=%u seg=%u",
             kSpiThumbMaxWidth, kSpiThumbMaxHeight, kThumbDecodeBufferBytes,
             static_cast<unsigned>(g_spi_jpeg_full_buffer_size),
             static_cast<unsigned>(sizeof(HigherhrnetResult)),
             static_cast<unsigned>(kSpiThumbMaxWidth * kSpiThumbMaxHeight));
    return true;
}

static bool ensure_spi_jpeg_input_buffer(uint32_t jpeg_len)
{
    if (g_spi_jpeg_input_buffer && g_spi_jpeg_input_buffer_size >= jpeg_len) {
        return true;
    }

    if (g_spi_jpeg_input_buffer) {
        free(g_spi_jpeg_input_buffer);
        g_spi_jpeg_input_buffer = nullptr;
        g_spi_jpeg_input_buffer_size = 0;
    }

    jpeg_decode_memory_alloc_cfg_t in_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    g_spi_jpeg_input_buffer = static_cast<uint8_t *>(
        jpeg_alloc_decoder_mem(jpeg_len, &in_mem_cfg, &g_spi_jpeg_input_buffer_size));
    if (!g_spi_jpeg_input_buffer) {
        ESP_LOGE(TAG, "failed to allocate SPI JPEG input buffer len=%" PRIu32, jpeg_len);
        return false;
    }
    return true;
}

static bool find_jpeg_span(const uint8_t *data, uint32_t len, const uint8_t **jpeg_ptr, uint32_t *jpeg_len)
{
    if (!data || len < 4 || !jpeg_ptr || !jpeg_len) {
        return false;
    }

    uint32_t soi = UINT32_MAX;
    uint32_t eoi = UINT32_MAX;
    for (uint32_t i = 0; i + 1 < len; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8) {
            soi = i;
            break;
        }
    }
    if (soi == UINT32_MAX) {
        return false;
    }
    for (uint32_t i = len - 2; i > soi; --i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            eoi = i + 2;
            break;
        }
    }
    if (eoi == UINT32_MAX || eoi <= soi) {
        return false;
    }

    *jpeg_ptr = data + soi;
    *jpeg_len = eoi - soi;
    return true;
}

static bool trim_jpeg_block_span(const uint8_t *data, uint32_t block_len, uint32_t claimed_jpeg_len,
                                 const uint8_t **jpeg_ptr, uint32_t *jpeg_len)
{
    if (!data || !jpeg_ptr || !jpeg_len || block_len < 4) {
        return false;
    }

    uint32_t candidate_len = block_len;
    if (claimed_jpeg_len > 0) {
        candidate_len = std::min(block_len, calculate_jpeg_aligned_size(claimed_jpeg_len));
    }
    return find_jpeg_span(data, candidate_len, jpeg_ptr, jpeg_len);
}

static bool decode_spi_jpeg_thumbnail_span(const uint8_t *jpeg_data, uint32_t jpeg_len)
{
    static bool rgb565_swap_logged = false;
    if (!jpeg_data || jpeg_len == 0 || !g_jpeg_decoder) {
        return false;
    }

    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(jpeg_data, jpeg_len, &info) != ESP_OK) {
        ESP_LOGW(TAG, "failed to query SPI JPEG info len=%" PRIu32, jpeg_len);
        return false;
    }

    uint16_t scaled_width = 0;
    uint16_t scaled_height = 0;
    if (!compute_spi_thumbnail_size(info.width, info.height, &scaled_width, &scaled_height)) {
        ESP_LOGW(TAG, "invalid SPI JPEG thumbnail size raw=%" PRIu32 "x%" PRIu32, info.width, info.height);
        return false;
    }

    const uint64_t decoded_bytes_needed = static_cast<uint64_t>(info.width) * info.height * 2u;
    if (decoded_bytes_needed > g_spi_jpeg_full_buffer_size) {
        ESP_LOGW(TAG, "SPI JPEG decode buffer too small: raw=%" PRIu32 "x%" PRIu32 " need=%" PRIu64 " have=%u",
                 info.width, info.height, decoded_bytes_needed,
                 static_cast<unsigned>(g_spi_jpeg_full_buffer_size));
        return false;
    }

    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    if (!ensure_spi_jpeg_input_buffer(jpeg_len)) {
        return false;
    }
    memcpy(g_spi_jpeg_input_buffer, jpeg_data, jpeg_len);

    uint32_t out_size = 0;
    if (jpeg_decoder_process(g_jpeg_decoder, &decode_cfg, g_spi_jpeg_input_buffer, jpeg_len,
                             g_spi_jpeg_full_buffer, static_cast<uint32_t>(g_spi_jpeg_full_buffer_size), &out_size) != ESP_OK) {
        ESP_LOGW(TAG, "failed to decode SPI JPEG len=%" PRIu32, jpeg_len);
        return false;
    }

    const uint16_t *src = reinterpret_cast<const uint16_t *>(g_spi_jpeg_full_buffer);
    uint16_t *dst = reinterpret_cast<uint16_t *>(g_spi_thumb_staging);
    for (uint16_t y = 0; y < scaled_height; ++y) {
        uint32_t src_y = static_cast<uint32_t>(y) * info.height / scaled_height;
        for (uint16_t x = 0; x < scaled_width; ++x) {
            uint32_t src_x = static_cast<uint32_t>(x) * info.width / scaled_width;
            uint16_t pixel = src[src_y * info.width + src_x];
            pixel = swap_rgb565_bytes(pixel);
            if (!rgb565_swap_logged) {
                ESP_LOGI(TAG, "applying RGB565 byte swap to SPI JPEG thumbnail before LCD overlay");
                rgb565_swap_logged = true;
            }
            dst[y * scaled_width + x] = pixel;
        }
    }

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(g_spi_thumb_buffer, g_spi_thumb_staging, scaled_width * scaled_height * 2);
        g_overlay_state.thumbnail_ready = true;
        g_overlay_state.thumb_width = scaled_width;
        g_overlay_state.thumb_height = scaled_height;
        g_overlay_state.thumb_updated_us = now_us();
        xSemaphoreGive(g_overlay_mutex);
    }
    return true;
}

static bool update_spi_thumbnail_from_metadata(const IMX500ParsedMetadata &parsed)
{
    if (parsed.jpeg_data && parsed.jpeg_data_len > 0 && decode_spi_jpeg_thumbnail_span(parsed.jpeg_data, parsed.jpeg_data_len)) {
        return true;
    }

    if (parsed.jpeg_block_offset < parsed.jpeg_block_end_offset &&
        parsed.jpeg_block_end_offset <= g_frame_buf_capacity) {
        const uint8_t *jpeg_ptr = nullptr;
        uint32_t jpeg_len = 0;
        const uint8_t *jpeg_block = g_frame_buf + parsed.jpeg_block_offset;
        uint32_t jpeg_block_len = parsed.jpeg_block_end_offset - parsed.jpeg_block_offset;
        if (trim_jpeg_block_span(jpeg_block, jpeg_block_len, parsed.jpeg_data_len, &jpeg_ptr, &jpeg_len)) {
            ESP_LOGI(TAG, "retrying SPI JPEG decode with fallback span: off=%" PRIu32 " len=%" PRIu32,
                     static_cast<uint32_t>(jpeg_ptr - g_frame_buf), jpeg_len);
            return decode_spi_jpeg_thumbnail_span(jpeg_ptr, jpeg_len);
        }
    }
    return false;
}

static float clamp_coord(float value, float max_value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int thumbnail_origin_x(uint32_t frame_w, const LocalOverlaySnapshot &snapshot)
{
    (void)frame_w;
    (void)snapshot;
    return 0;
}

static int thumbnail_origin_y(uint32_t frame_h, const LocalOverlaySnapshot &snapshot)
{
    if (frame_h <= snapshot.thumb_height) {
        return 0;
    }
    return static_cast<int>(frame_h - snapshot.thumb_height);
}

static void snapshot_overlay_state(LocalOverlaySnapshot *snapshot)
{
    if (!snapshot || !g_overlay_mutex) {
        return;
    }

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snapshot->thumbnail_ready = g_overlay_state.thumbnail_ready;
        snapshot->thumb_width = g_overlay_state.thumb_width;
        snapshot->thumb_height = g_overlay_state.thumb_height;
        snapshot->result_updated_us = g_overlay_state.result_updated_us;
        snapshot->ai_overlay_enabled = g_overlay_state.ai_overlay_enabled;
        snapshot->spi_metadata_enabled = g_overlay_state.spi_metadata_enabled;
        snapshot->modal_active = g_overlay_state.modal_active;
        snapshot->classification = g_overlay_state.classification;
        snapshot->detection = g_overlay_state.detection;
        snapshot->segmentation = g_overlay_state.segmentation;
        memcpy(&snapshot->hrnet, &g_overlay_state.hrnet, sizeof(snapshot->hrnet));
        memcpy(snapshot->summary_line_1, g_overlay_state.summary_line_1, sizeof(snapshot->summary_line_1));
        memcpy(snapshot->summary_line_2, g_overlay_state.summary_line_2, sizeof(snapshot->summary_line_2));
        memcpy(snapshot->modal_line_1, g_overlay_state.modal_line_1, sizeof(snapshot->modal_line_1));
        memcpy(snapshot->modal_line_2, g_overlay_state.modal_line_2, sizeof(snapshot->modal_line_2));
        if (snapshot->thumbnail_ready && g_spi_thumb_buffer && g_spi_thumb_snapshot) {
            memcpy(g_spi_thumb_snapshot, g_spi_thumb_buffer,
                   static_cast<size_t>(snapshot->thumb_width) * snapshot->thumb_height * 2u);
        }
        if (snapshot->segmentation.ready && g_segmentation_mask && g_segmentation_mask_snapshot) {
            memcpy(g_segmentation_mask_snapshot, g_segmentation_mask,
                   static_cast<size_t>(snapshot->segmentation.width) * snapshot->segmentation.height);
        }
        xSemaphoreGive(g_overlay_mutex);
    }
}

static void render_classification_preview(uint16_t *frame, uint32_t frame_w, uint32_t frame_h,
                                          const LocalOverlaySnapshot &snapshot)
{
    const int origin_x = thumbnail_origin_x(frame_w, snapshot);
    const int origin_y = thumbnail_origin_y(frame_h, snapshot);
    const int panel_height = static_cast<int>(snapshot.classification.count) * 18 + 10;
    const int panel_top = std::max(origin_y, origin_y + static_cast<int>(snapshot.thumb_height) - panel_height);
    imx500_fill_rectangle_rgb565(frame, frame_w, frame_h, origin_x, panel_top,
                                 origin_x + static_cast<int>(snapshot.thumb_width) - 1,
                                 origin_y + static_cast<int>(snapshot.thumb_height) - 1, 0, 0, 0);

    int y = panel_top + 4;
    for (uint32_t i = 0; i < snapshot.classification.count; ++i) {
        char line[64] = {};
        const std::string label = imx500_label_for_class_id(g_active_labels, snapshot.classification.class_ids[i]);
        std::snprintf(line, sizeof(line), "TOP%u %s %.0f%%",
                      static_cast<unsigned>(i + 1), label.c_str(),
                      imx500_score_to_percent(snapshot.classification.scores[i]));
        imx500_draw_text_rgb565(frame, frame_w, frame_h, origin_x + 4, y, line, 255, 255, 255, 2);
        y += 18;
    }
}

static void render_detection_preview(uint16_t *frame, uint32_t frame_w, uint32_t frame_h,
                                     const LocalOverlaySnapshot &snapshot)
{
    const int origin_x = thumbnail_origin_x(frame_w, snapshot);
    const int origin_y = thumbnail_origin_y(frame_h, snapshot);
    const uint16_t src_w = std::max<uint16_t>(1, snapshot.detection.source_width);
    const uint16_t src_h = std::max<uint16_t>(1, snapshot.detection.source_height);
    for (uint32_t i = 0; i < snapshot.detection.count; ++i) {
        const Imx500DetectionOverlayItem &item = snapshot.detection.items[i];
        const int x1 = origin_x + static_cast<int>(item.x1 * snapshot.thumb_width / src_w);
        const int y1 = origin_y + static_cast<int>(item.y1 * snapshot.thumb_height / src_h);
        const int x2 = origin_x + static_cast<int>(item.x2 * snapshot.thumb_width / src_w);
        const int y2 = origin_y + static_cast<int>(item.y2 * snapshot.thumb_height / src_h);
        draw_rectangle_rgb(frame, frame_w, frame_h, x1, y1, x2, y2, 0, 0, 255, 210, 0, 2, false);

        char caption[64] = {};
        const std::string label = imx500_label_for_class_id(g_active_labels, item.class_id);
        std::snprintf(caption, sizeof(caption), "%s %.0f%%", label.c_str(), imx500_score_to_percent(item.score));
        const int text_y = std::max(origin_y, y1 - 16);
        imx500_fill_rectangle_rgb565(frame, frame_w, frame_h, x1, text_y,
                                     std::min<int>(x1 + imx500_measure_text_width(caption, 1) + 6,
                                                   origin_x + static_cast<int>(snapshot.thumb_width) - 1),
                                     std::min<int>(text_y + 14,
                                                   origin_y + static_cast<int>(snapshot.thumb_height) - 1), 0, 0, 0);
        imx500_draw_text_rgb565(frame, frame_w, frame_h, x1 + 2, text_y + 2, caption, 255, 255, 255, 1);
    }
}

static void render_segmentation_preview(uint16_t *frame, uint32_t frame_w, uint32_t frame_h,
                                        const LocalOverlaySnapshot &snapshot)
{
    if (!snapshot.segmentation.ready || !g_segmentation_mask_snapshot) {
        return;
    }
    const int origin_x = thumbnail_origin_x(frame_w, snapshot);
    const int origin_y = thumbnail_origin_y(frame_h, snapshot);
    for (uint32_t y = 0; y < snapshot.segmentation.height; ++y) {
        for (uint32_t x = 0; x < snapshot.segmentation.width; ++x) {
            const uint8_t class_id = g_segmentation_mask_snapshot[y * snapshot.segmentation.width + x];
            if (class_id == 0) {
                continue;
            }
            const uint8_t *colour = kSegmentationColours[class_id % (sizeof(kSegmentationColours) / sizeof(kSegmentationColours[0]))];
            uint16_t &pixel = frame[(origin_y + static_cast<int>(y)) * frame_w + (origin_x + static_cast<int>(x))];
            pixel = imx500_blend_rgb565(pixel, colour[0], colour[1], colour[2], kSegmentationAlpha);
        }
    }
}

static void render_pose_preview(uint16_t *frame, uint32_t frame_w, uint32_t frame_h,
                                const LocalOverlaySnapshot &snapshot)
{
    const int origin_x = thumbnail_origin_x(frame_w, snapshot);
    const int origin_y = thumbnail_origin_y(frame_h, snapshot);
    float max_x = 0.0f;
    float max_y = 0.0f;
    for (uint32_t i = 0; i < snapshot.hrnet.pose_count; ++i) {
        for (uint32_t j = 0; j < kHigherhrnetJointCount; ++j) {
            if (snapshot.hrnet.poses[i].keypoints[j].score > 0.05f) {
                max_x = std::max(max_x, snapshot.hrnet.poses[i].keypoints[j].x);
                max_y = std::max(max_y, snapshot.hrnet.poses[i].keypoints[j].y);
            }
        }
    }

    const float src_w = (max_x <= snapshot.thumb_width * 1.5f && max_y <= snapshot.thumb_height * 1.5f)
                            ? static_cast<float>(snapshot.thumb_width)
                            : 384.0f;
    const float src_h = (max_x <= snapshot.thumb_width * 1.5f && max_y <= snapshot.thumb_height * 1.5f)
                            ? static_cast<float>(snapshot.thumb_height)
                            : 288.0f;

    for (uint32_t i = 0; i < snapshot.hrnet.pose_count; ++i) {
        const HigherhrnetPose &pose = snapshot.hrnet.poses[i];
        int x1 = origin_x + static_cast<int>(clamp_coord(pose.x1 * snapshot.thumb_width / src_w, static_cast<float>(snapshot.thumb_width - 1)));
        int y1 = origin_y + static_cast<int>(clamp_coord(pose.y1 * snapshot.thumb_height / src_h, static_cast<float>(snapshot.thumb_height - 1)));
        int x2 = origin_x + static_cast<int>(clamp_coord(pose.x2 * snapshot.thumb_width / src_w, static_cast<float>(snapshot.thumb_width - 1)));
        int y2 = origin_y + static_cast<int>(clamp_coord(pose.y2 * snapshot.thumb_height / src_h, static_cast<float>(snapshot.thumb_height - 1)));
        draw_rectangle_rgb(frame, frame_w, frame_h, x1, y1, x2, y2, 0, 0, 0, 255, 0, 2, false);

        for (const auto &joint : kSkeleton) {
            const HigherhrnetKeypoint &a = pose.keypoints[joint[0]];
            const HigherhrnetKeypoint &b = pose.keypoints[joint[1]];
            if (a.score > 0.05f && b.score > 0.05f) {
                int ax = origin_x + static_cast<int>(clamp_coord(a.x * snapshot.thumb_width / src_w, static_cast<float>(snapshot.thumb_width - 1)));
                int ay = origin_y + static_cast<int>(clamp_coord(a.y * snapshot.thumb_height / src_h, static_cast<float>(snapshot.thumb_height - 1)));
                int bx = origin_x + static_cast<int>(clamp_coord(b.x * snapshot.thumb_width / src_w, static_cast<float>(snapshot.thumb_width - 1)));
                int by = origin_y + static_cast<int>(clamp_coord(b.y * snapshot.thumb_height / src_h, static_cast<float>(snapshot.thumb_height - 1)));
                draw_line_rgb(frame, frame_w, frame_h, ax, ay, bx, by, 0, 0, 255, 255, 255, 2, false);
            }
        }

        for (uint32_t j = 0; j < kHigherhrnetJointCount; ++j) {
            const HigherhrnetKeypoint &keypoint = pose.keypoints[j];
            if (keypoint.score <= 0.05f) {
                continue;
            }
            int x = origin_x + static_cast<int>(clamp_coord(keypoint.x * snapshot.thumb_width / src_w, static_cast<float>(snapshot.thumb_width - 1)));
            int y = origin_y + static_cast<int>(clamp_coord(keypoint.y * snapshot.thumb_height / src_h, static_cast<float>(snapshot.thumb_height - 1)));
            draw_point_rgb(frame, frame_w, frame_h, x, y, 0, 0, 255, 0, 0, 3, false);
        }
    }
}

static void blit_spi_thumbnail_with_ai_overlay(uint16_t *frame, uint32_t frame_w, uint32_t frame_h,
                                               const LocalOverlaySnapshot &snapshot)
{
    if (!frame || !g_overlay_mutex) {
        return;
    }
    if (!snapshot.thumbnail_ready || snapshot.thumb_width == 0 || snapshot.thumb_height == 0) {
        return;
    }

    const int origin_x = thumbnail_origin_x(frame_w, snapshot);
    const int origin_y = thumbnail_origin_y(frame_h, snapshot);
    for (uint16_t y = 0; y < snapshot.thumb_height && (origin_y + static_cast<int>(y)) < static_cast<int>(frame_h); ++y) {
        memcpy(frame + (origin_y + static_cast<int>(y)) * frame_w + origin_x,
               g_spi_thumb_snapshot + y * snapshot.thumb_width * 2,
               snapshot.thumb_width * 2);
    }

    if (!snapshot.ai_overlay_enabled || now_us() - snapshot.result_updated_us > kAiResultExpiryUs) {
        return;
    }

    switch (g_active_model ? g_active_model->task_type : Imx500ModelTaskType::kPoseEstimation) {
    case Imx500ModelTaskType::kClassification:
        render_classification_preview(frame, frame_w, frame_h, snapshot);
        break;
    case Imx500ModelTaskType::kObjectDetection:
        render_detection_preview(frame, frame_w, frame_h, snapshot);
        break;
    case Imx500ModelTaskType::kPoseEstimation:
        render_pose_preview(frame, frame_w, frame_h, snapshot);
        break;
    case Imx500ModelTaskType::kSegmentation:
        render_segmentation_preview(frame, frame_w, frame_h, snapshot);
        break;
    }
}

static void mipi_display_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes,
                                         uint32_t camera_buf_ves, size_t camera_buf_len, void *user_data)
{
    static bool callback_logged = false;
    static bool compose_mode_logged = false;
    static uint64_t first_frame_us = 0;
    static uint32_t frame_count = 0;
    (void)user_data;

    if (!callback_logged) {
        ESP_LOGI(TAG, "display callback started: fd=%d camera=%" PRIu32 "x%" PRIu32 " len=%u stack_hwm=%u",
                 g_video_fd, camera_buf_hes, camera_buf_ves, static_cast<unsigned>(camera_buf_len),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)));
        ESP_LOGI(TAG, "MIPI main image resolution: %" PRIu32 "x%" PRIu32, camera_buf_hes, camera_buf_ves);
        callback_logged = true;
        g_display_first_frame_seen = true;
        first_frame_us = now_us();
    }

    const uint8_t display_buf_index = select_lcd_compose_buffer_index(camera_buf_index);
    uint8_t *display_buf = camera_buf;
    if (lcd_buffer_ready(display_buf_index)) {
        display_buf = reinterpret_cast<uint8_t *>(g_lcd_buffers[display_buf_index]);
        memcpy(display_buf, camera_buf, camera_buf_len);
        if (!compose_mode_logged) {
            ESP_LOGI(TAG, "display composition uses LCD framebuffer ring: cam=%u compose=%u buffers=%u; overlays are completed off-screen before scanout",
                     static_cast<unsigned>(camera_buf_index), static_cast<unsigned>(display_buf_index),
                     static_cast<unsigned>(EXAMPLE_LCD_BUF_NUM));
            compose_mode_logged = true;
        }
    }

    snapshot_overlay_state(&g_display_overlay_snapshot);
    const LocalOverlaySnapshot &snapshot = g_display_overlay_snapshot;

    uint16_t *frame = reinterpret_cast<uint16_t *>(display_buf);
    blit_spi_thumbnail_with_ai_overlay(frame, camera_buf_hes, camera_buf_ves, snapshot);
    const std::string model_name = lcd_model_name();
    const std::string task_name = lcd_task_name();
    Imx500HudInfo hud = {
        .model_name = model_name.c_str(),
        .task_name = task_name.c_str(),
        .summary_line_1 = snapshot.summary_line_1[0] ? snapshot.summary_line_1 : "WAITING FOR AI RESULT",
        .summary_line_2 = snapshot.summary_line_2[0] ? snapshot.summary_line_2 : "PRESS BOOT TO SWITCH MODEL",
        .spi_metadata_enabled = snapshot.spi_metadata_enabled,
    };
    imx500_render_hud(frame, camera_buf_hes, camera_buf_ves, hud);
    Imx500ModalInfo modal = {
        .active = snapshot.modal_active,
        .line_1 = snapshot.modal_line_1,
        .line_2 = snapshot.modal_line_2,
    };
    imx500_render_modal(frame, camera_buf_hes, camera_buf_ves, modal);
    if (display_buf != camera_buf && g_lcd_refresh_callback_registered) {
        g_lcd_buffer_busy_until[display_buf_index] = g_lcd_refresh_count + 2u;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_display_panel, 0, 0, camera_buf_hes, camera_buf_ves, display_buf));
    if (display_buf != camera_buf) {
        g_lcd_last_presented_index = display_buf_index;
    }

    uint64_t now = now_us();
    float fps = 0.0f;
    if (frame_count > 0 && now > first_frame_us) {
        fps = static_cast<float>(frame_count) * 1000000.0f / static_cast<float>(now - first_frame_us);
    }
    if (frame_count < 5 || (frame_count % 60) == 0) {
        ESP_LOGI(TAG, "display frame=%" PRIu32 " cam_buf=%u lcd_buf=%u size=%" PRIu32 "x%" PRIu32 " bytes=%u fps=%.2f",
                 frame_count, static_cast<unsigned>(camera_buf_index), static_cast<unsigned>(display_buf_index),
                 camera_buf_hes, camera_buf_ves, static_cast<unsigned>(camera_buf_len), fps);
    }
    frame_count++;
}

static esp_err_t init_mipi_display_path()
{
    struct v4l2_format format = {};

    ESP_LOGI(TAG, "initializing MIPI display path without reconfiguring the sensor");
    ESP_RETURN_ON_FALSE(ensure_lcd_ready(), ESP_FAIL, TAG, "failed to prepare LCD");
    ESP_RETURN_ON_ERROR(example_video_init_preserving_sensor_state(), TAG, "failed to initialize video pipeline");

    g_video_fd = app_video_open((char *)EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    ESP_RETURN_ON_FALSE(g_video_fd >= 0, ESP_FAIL, TAG, "video open failed");

    for (uint32_t i = 0; i < EXAMPLE_CAM_BUF_NUM; ++i) {
        if (!g_camera_buffers[i]) {
            g_camera_buffers[i] = heap_caps_aligned_calloc(kPsramFrameBufferAlignment, 1, app_video_get_buf_size(), MALLOC_CAP_SPIRAM);
        }
        ESP_RETURN_ON_FALSE(g_camera_buffers[i] != nullptr, ESP_ERR_NO_MEM, TAG,
                            "failed to allocate camera capture buffer[%u]", i);
    }
    ESP_LOGI(TAG, "using dedicated CSI capture buffers and separate LCD compose buffers");
    ESP_RETURN_ON_ERROR(app_video_set_bufs(g_video_fd, EXAMPLE_CAM_BUF_NUM, (const void **)g_camera_buffers),
                        TAG, "failed to set video buffers");

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_video_fd, VIDIOC_G_FMT, &format);
    ESP_LOGI(TAG, "MIPI display ready: %" PRIu32 "x%" PRIu32 " fmt=0x%08" PRIx32 " fd=%d",
             format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat, g_video_fd);
    return ESP_OK;
}

#if AI_CAMERA_MULTITASK_BOOT_MODE == AI_CAMERA_MULTITASK_BOOT_MODE_FLASH
static void dump_spi_flash_status(const char *label)
{
    spi_flash_status_t status = {};
    if (!get_spi_flash_status(&status)) {
        ESP_LOGW(TAG, "%s: failed to read SPI flash status", label);
        return;
    }
    ESP_LOGI(TAG,
             "%s: status=%" PRIu32 " result=%" PRIu32 " bytes=%" PRIu32 "/%" PRIu32,
             label,
             status.status,
             status.result,
             status.bytes_done,
             status.bytes_total);
}

static bool program_flash_assets()
{
    if (!g_active_model) {
        return false;
    }

    ESP_LOGI(TAG,
             "programming IMX500 flash with embedded model=%s fw=%u bytes nn_info=%u bytes",
             g_active_model->key,
             static_cast<unsigned>(g_active_model->firmware.size),
             static_cast<unsigned>(g_active_model->network_info.size));
    if (!spi_slave_write_model_to_flash(g_active_model->firmware.data, static_cast<uint32_t>(g_active_model->firmware.size))) {
        dump_spi_flash_status("model flash failed");
        return false;
    }

    if (!spi_slave_write_nn_info_to_flash(g_active_model->network_info.data, static_cast<uint32_t>(g_active_model->network_info.size))) {
        dump_spi_flash_status("network_info flash failed");
        return false;
    }

    dump_spi_flash_status("flash programming complete");
    return true;
}
#endif

static const char *get_boot_mode_name()
{
#if AI_CAMERA_MULTITASK_BOOT_MODE == AI_CAMERA_MULTITASK_BOOT_MODE_DIRECT
    return "DIRECT BOOT";
#elif AI_CAMERA_MULTITASK_BOOT_MODE == AI_CAMERA_MULTITASK_BOOT_MODE_FLASH
    return "FLASH BOOT";
#else
    return "UNKNOWN BOOT";
#endif
}

static bool open_imx500_stream()
{
    uint32_t module_fw_ver = 0;
    uint32_t module_pid = 0;
    get_fw_ver(&module_fw_ver);
    get_pid(&module_pid);
    ESP_LOGI(TAG, "module fw version: 0x%" PRIx32, module_fw_ver);
    ESP_LOGI(TAG, "module pid: 0x%" PRIx32, module_pid);
    ESP_LOGI(TAG, "selected IMX500 boot mode: %s", get_boot_mode_name());
    ESP_LOGI(TAG, "selected embedded model: %s", g_active_model ? g_active_model->key : "null");

    if (!g_active_model) {
        return false;
    }

#if AI_CAMERA_MULTITASK_BOOT_MODE == AI_CAMERA_MULTITASK_BOOT_MODE_DIRECT
    ESP_LOGI(TAG,
             "opening IMX500 with embedded model directly: fw=%u bytes nn_info=%u bytes",
             static_cast<unsigned>(g_active_model->firmware.size),
             static_cast<unsigned>(g_active_model->network_info.size));
    return open(g_active_model->firmware.data,
                static_cast<uint32_t>(g_active_model->firmware.size),
                g_active_model->network_info.data,
                static_cast<uint32_t>(g_active_model->network_info.size),
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#elif AI_CAMERA_MULTITASK_BOOT_MODE == AI_CAMERA_MULTITASK_BOOT_MODE_FLASH
    if (!program_flash_assets()) {
        return false;
    }

    ESP_LOGI(TAG, "opening IMX500 from module flash");
    return open(nullptr, 0, nullptr, 0,
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#else
#error "Unsupported AI_CAMERA_MULTITASK_BOOT_MODE"
#endif
}

static void spi_metadata_task(void *arg)
{
    (void)arg;
    uint32_t wait_loops = 0;
    uint32_t metadata_frame_count = 0;
    ESP_LOGI(TAG, "spi task started: stack=%u parsed_metadata=%u stack_hwm=%u",
             static_cast<unsigned>(kSpiTaskStackSize),
             static_cast<unsigned>(sizeof(IMX500ParsedMetadata)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)));

    while (!g_display_first_frame_seen && wait_loops < 400) {
        if (wait_loops == 0) {
            ESP_LOGI(TAG, "waiting to start SPI metadata parsing until the first MIPI frame arrives");
        } else if ((wait_loops % 100) == 0) {
            ESP_LOGI(TAG, "still waiting for first MIPI frame before parsing SPI metadata, loops=%" PRIu32, wait_loops);
        }
        wait_loops++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "SPI metadata parser armed: first_frame_seen=%d wait_loops=%" PRIu32,
             g_display_first_frame_seen ? 1 : 0, wait_loops);

    while (true) {
        int32_t data_size = read_metadata(g_frame_buf, g_frame_buf_capacity);
        if (data_size <= 0) {
            continue;
        }

        if (metadata_frame_count < 5 || (metadata_frame_count % 30) == 0) {
            ESP_LOGI(TAG, "metadata frame=%" PRIu32 " payload=%ld stack_hwm=%u",
                     metadata_frame_count, static_cast<long>(data_size),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)));
            log_metadata_first_12_bytes(g_frame_buf, static_cast<uint32_t>(data_size), "metadata raw");
        }

        if (!parse_output_tensor_data_with_metadata(g_frame_buf, static_cast<uint32_t>(data_size), g_parsed_metadata)) {
            ESP_LOGW(TAG, "failed to parse metadata payload=%ld", static_cast<long>(data_size));
            if (is_zero_header_payload(g_frame_buf, static_cast<uint32_t>(data_size))) {
                if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    g_overlay_state.zero_header_failures++;
                    if (g_overlay_state.zero_header_failures >= kMetadataDisableThreshold) {
                        ESP_LOGW(TAG, "metadata parse failed for %u consecutive zero-header frames; entering recovery mode and waiting for valid SPI metadata",
                                 g_overlay_state.zero_header_failures);
                        g_overlay_state.spi_metadata_enabled = false;
                        g_overlay_state.ai_overlay_enabled = false;
                        clear_overlay_results_locked();
                        set_overlay_summary_locked("SPI METADATA ERROR", "WAITING FOR RECOVERY");
                    }
                    xSemaphoreGive(g_overlay_mutex);
                }
            }
            metadata_frame_count++;
            continue;
        }

        bool metadata_recovered = false;
        if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            metadata_recovered = !g_overlay_state.spi_metadata_enabled || !g_overlay_state.ai_overlay_enabled;
            g_overlay_state.zero_header_failures = 0;
            g_overlay_state.spi_metadata_enabled = true;
            g_overlay_state.ai_overlay_enabled = true;
            xSemaphoreGive(g_overlay_mutex);
        }
        if (metadata_recovered) {
            ESP_LOGI(TAG, "SPI metadata recovered after transient parse errors");
        }
        if (metadata_frame_count < 5 || (metadata_frame_count % 30) == 0) {
            log_parsed_metadata_basic_info(*g_parsed_metadata, static_cast<uint32_t>(data_size));
        }
        update_spi_thumbnail_from_metadata(*g_parsed_metadata);

        switch (g_active_model ? g_active_model->task_type : Imx500ModelTaskType::kPoseEstimation) {
        case Imx500ModelTaskType::kClassification: {
            Imx500ClassificationOverlayState classification = {};
            std::string line1;
            std::string line2;
            if (imx500_postprocess_classification(*g_parsed_metadata, g_active_labels, &classification, &line1, &line2)) {
                if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    g_overlay_state.classification = classification;
                    g_overlay_state.result_updated_us = now_us();
                    set_overlay_summary_locked(line1, line2);
                    xSemaphoreGive(g_overlay_mutex);
                }
            }
            break;
        }
        case Imx500ModelTaskType::kObjectDetection: {
            Imx500DetectionOverlayState detection = {};
            std::string line1;
            std::string line2;
            if (imx500_postprocess_detection(*g_parsed_metadata, g_active_labels, &detection, &line1, &line2)) {
                if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    g_overlay_state.detection = detection;
                    g_overlay_state.result_updated_us = now_us();
                    set_overlay_summary_locked(line1, line2);
                    xSemaphoreGive(g_overlay_mutex);
                }
            }
            break;
        }
        case Imx500ModelTaskType::kPoseEstimation: {
            if (!g_spi_hrnet_result) {
                ESP_LOGW(TAG, "hrnet result buffer is not ready");
                break;
            }
            memset(g_spi_hrnet_result, 0, sizeof(*g_spi_hrnet_result));
            if (higherhrnet_postprocess_from_metadata(g_parsed_metadata, g_spi_hrnet_result)) {
                char line1[64] = {};
                char line2[64] = {};
                std::snprintf(line1, sizeof(line1), "PEOPLE %u", static_cast<unsigned>(g_spi_hrnet_result->pose_count));
                std::snprintf(line2, sizeof(line2), "POSE OVERLAY READY");
                if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    memcpy(&g_overlay_state.hrnet, g_spi_hrnet_result, sizeof(*g_spi_hrnet_result));
                    g_overlay_state.result_updated_us = now_us();
                    set_overlay_summary_locked(line1, line2);
                    xSemaphoreGive(g_overlay_mutex);
                }
            }
            break;
        }
        case Imx500ModelTaskType::kSegmentation: {
            uint16_t thumb_w = 0;
            uint16_t thumb_h = 0;
            if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                thumb_w = g_overlay_state.thumb_width;
                thumb_h = g_overlay_state.thumb_height;
                xSemaphoreGive(g_overlay_mutex);
            }

            Imx500SegmentationOverlayState segmentation = {};
            std::string line1;
            std::string line2;
            if (imx500_postprocess_segmentation(*g_parsed_metadata, g_active_labels, thumb_w, thumb_h,
                                               g_segmentation_mask, &segmentation, &line1, &line2)) {
                if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    g_overlay_state.segmentation = segmentation;
                    g_overlay_state.result_updated_us = now_us();
                    set_overlay_summary_locked(line1, line2);
                    xSemaphoreGive(g_overlay_mutex);
                }
            }
            break;
        }
        }
        metadata_frame_count++;
    }
}

static void switch_to_next_model_and_restart()
{
    if (g_switching_model || g_model_count == 0) {
        return;
    }
    g_switching_model = true;

    const size_t next_index = (g_active_model_index + 1) % g_model_count;
    const Imx500EmbeddedModelDescriptor *next_model = imx500_get_embedded_model(next_index);
    const std::string next_name = next_model ? next_model->display_name : "UNKNOWN";

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_overlay_modal_locked(true, "SWITCHING MODEL", next_name);
        set_overlay_summary_locked("SAVING SELECTION", next_name);
        xSemaphoreGive(g_overlay_mutex);
    }

    if (!save_selected_model_index(next_index)) {
        if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            set_overlay_modal_locked(true, "MODEL SWITCH FAILED", "NVS SAVE ERROR");
            xSemaphoreGive(g_overlay_mutex);
        }
        imx500_present_status_screen(g_display_panel, g_lcd_buffers, EXAMPLE_LCD_BUF_NUM,
                                     "MODEL SWITCH FAILED", "NVS SAVE ERROR", "RESTART DEVICE");
        g_switching_model = false;
        return;
    }

    imx500_present_status_screen(g_display_panel, g_lcd_buffers, EXAMPLE_LCD_BUF_NUM,
                                 "SWITCHING MODEL", next_name, "RESTARTING");
    vTaskDelay(pdMS_TO_TICKS(kModelSwitchNoticeMs));
    esp_restart();
}

static void boot_button_task(void *arg)
{
    (void)arg;

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << kBootButtonGpio;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));

    bool was_pressed = false;
    TickType_t pressed_tick = 0;
    while (true) {
        const bool pressed = gpio_get_level(kBootButtonGpio) == 0;
        if (pressed && !was_pressed) {
            pressed_tick = xTaskGetTickCount();
        } else if (!pressed && was_pressed) {
            const TickType_t held = xTaskGetTickCount() - pressed_tick;
            if (held >= pdMS_TO_TICKS(kBootButtonDebounceMs)) {
                switch_to_next_model_and_restart();
            }
        }
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(kBootButtonPollMs));
    }
}

static esp_err_t start_worker_tasks()
{
    ESP_RETURN_ON_ERROR(app_video_register_frame_operation_cb(mipi_display_frame_operation),
                        TAG, "failed to register display callback");
    ESP_RETURN_ON_ERROR(app_video_stream_task_start(g_video_fd, 0, nullptr),
                        TAG, "failed to start app_video stream task");
    ESP_LOGI(TAG, "display pipeline armed: callback registered and app_video stream task started");

    if (!g_spi_task_handle) {
        BaseType_t result = xTaskCreatePinnedToCore(spi_metadata_task, "spi metadata task",
                                                    kSpiTaskStackSize, nullptr, 4,
                                                    &g_spi_task_handle, 1);
        ESP_RETURN_ON_FALSE(result == pdPASS, ESP_FAIL, TAG, "failed to create SPI task");
    }
    if (!g_boot_button_task_handle) {
        BaseType_t result = xTaskCreatePinnedToCore(boot_button_task, "boot button task",
                                                    kBootButtonTaskStackSize, nullptr, 3,
                                                    &g_boot_button_task_handle, 1);
        ESP_RETURN_ON_FALSE(result == pdPASS, ESP_FAIL, TAG, "failed to create BOOT task");
    }
    return ESP_OK;
}

} // namespace

extern "C" esp_err_t ai_camera_multitask_run(void)
{
    select_active_model();
    ESP_RETURN_ON_FALSE(g_active_model != nullptr, ESP_FAIL, TAG, "no embedded model selected");
    ESP_RETURN_ON_FALSE(bind_peripherals_api(), ESP_FAIL, TAG, "failed to bind SDK peripherals");
    ESP_RETURN_ON_FALSE(allocate_spi_thumbnail_buffers(), ESP_FAIL, TAG, "failed to prepare SPI thumbnail buffers");
    ESP_RETURN_ON_FALSE(ensure_lcd_ready(), ESP_FAIL, TAG, "failed to prepare LCD");
    imx500_present_status_screen(g_display_panel, g_lcd_buffers, EXAMPLE_LCD_BUF_NUM,
                                 "LOADING MODEL", g_active_model->display_name, get_boot_mode_name());

    ESP_RETURN_ON_FALSE(open_imx500_stream(), ESP_FAIL, TAG, "open() failed");
    ESP_RETURN_ON_FALSE(allocate_parsed_metadata_buffer(), ESP_FAIL, TAG, "failed to prepare parsed metadata buffer");
    ESP_RETURN_ON_ERROR(init_mipi_display_path(), TAG, "failed to initialize MIPI display path");
    ESP_RETURN_ON_ERROR(start_worker_tasks(), TAG, "failed to start display workers");

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        set_overlay_summary_locked("WAITING FOR AI RESULT", "PRESS BOOT TO SWITCH MODEL");
        set_overlay_modal_locked(false, "", "");
        xSemaphoreGive(g_overlay_mutex);
    }

    ESP_LOGI(TAG, "starting IMX500 stream after arming display workers");
    stream_on();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "IMX500 multitask demo running: model=%s task=%s",
             g_active_model->key, g_active_model->task_name);
    return ESP_OK;
}
