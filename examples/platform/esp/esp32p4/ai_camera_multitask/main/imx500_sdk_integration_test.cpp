#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <sys/ioctl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "linux/videodev2.h"
#include "driver/jpeg_decode.h"

#include "ArducamIMX500SDK.h"
#include "app_drawing_utils.h"
#include "app_lcd.h"
#include "app_video.h"
#include "example_video_common.h"
#include "generated_imx500_models/network.h"
#include "generated_imx500_models/network_info.h"
#include "higherhrnet_postprocess.h"
#include "imx500_sdk_integration_test.h"
#include "peripherals_adapter.h"

static const char *TAG = "imx500_sdk_test";

#define NN_FW_DATA           network_data
#define NN_FW_SIZE           network_size
#define NN_NETWORK_INFO_DATA network_info_data
#define NN_NETWORK_INFO_SIZE network_info_size

namespace {

constexpr uint32_t kMaxFrameBufferSize = 284 * 1024;
constexpr uint16_t kSpiThumbMaxWidth = 192;
constexpr uint16_t kSpiThumbMaxHeight = 144;
constexpr uint32_t kSpiTaskStackSize = 8 * 1024;
constexpr uint32_t kMetadataDisableThreshold = 3;
constexpr uint32_t kPoseExpiryUs = 1000000;
constexpr uint32_t kThumbDecodeMaxWidth = 384;
constexpr uint32_t kThumbDecodeMaxHeight = 288;
constexpr uint32_t kThumbDecodeBufferBytes = kThumbDecodeMaxWidth * kThumbDecodeMaxHeight * 2;
constexpr size_t kPsramFrameBufferAlignment = 64;
constexpr uint32_t kJpegAlignment = 1024;
constexpr uint32_t kJpegMinBlockBytes = 4 * 1024;
constexpr uint32_t kSkeleton[][2] = {
    {0, 1}, {0, 2}, {1, 3}, {2, 4},
    {5, 6}, {5, 11}, {11, 12}, {12, 6},
    {5, 7}, {7, 9}, {6, 8}, {8, 10},
    {11, 13}, {13, 15}, {12, 14}, {14, 16},
};

uint8_t *g_frame_buf = nullptr;
uint32_t g_frame_buf_capacity = 0;
IMX500ParsedMetadata *g_parsed_metadata = nullptr;
TaskHandle_t g_spi_task_handle = nullptr;
SemaphoreHandle_t g_overlay_mutex = nullptr;
int g_video_fd = -1;
esp_lcd_panel_handle_t g_display_panel = nullptr;
void *g_lcd_buffers[EXAMPLE_LCD_BUF_NUM] = {};
void *g_camera_buffers[EXAMPLE_LCD_BUF_NUM] = {};

uint8_t *g_spi_thumb_buffer = nullptr;
uint8_t *g_spi_thumb_staging = nullptr;
uint8_t *g_spi_jpeg_input_buffer = nullptr;
size_t g_spi_jpeg_input_buffer_size = 0;
uint8_t *g_spi_jpeg_full_buffer = nullptr;
size_t g_spi_jpeg_full_buffer_size = 0;
jpeg_decoder_handle_t g_jpeg_decoder = nullptr;
HigherhrnetResult *g_overlay_pose_snapshot = nullptr;
HigherhrnetResult *g_spi_hrnet_result = nullptr;

struct SharedOverlayState {
    bool thumbnail_ready = false;
    uint16_t thumb_width = 0;
    uint16_t thumb_height = 0;
    uint64_t thumb_updated_us = 0;
    bool ai_overlay_enabled = true;
    bool spi_metadata_enabled = true;
    uint32_t zero_header_failures = 0;
    HigherhrnetResult hrnet = {};
    uint64_t hrnet_updated_us = 0;
};

SharedOverlayState g_overlay_state = {};
volatile bool g_display_first_frame_seen = false;

static uint64_t now_us()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

static uint32_t align_up_u32(uint32_t value, uint32_t base)
{
    return ((value + base - 1) / base) * base;
}

static uint32_t calculate_jpeg_aligned_size(uint32_t jpeg_size)
{
    return std::max(kJpegMinBlockBytes, align_up_u32(jpeg_size + 3, kJpegAlignment));
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

static void clear_overlay_state_locked()
{
    g_overlay_state.thumbnail_ready = false;
    g_overlay_state.thumb_width = 0;
    g_overlay_state.thumb_height = 0;
    g_overlay_state.thumb_updated_us = 0;
    memset(&g_overlay_state.hrnet, 0, sizeof(g_overlay_state.hrnet));
    g_overlay_state.hrnet_updated_us = 0;
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
        g_spi_thumb_staging = static_cast<uint8_t *>(heap_caps_malloc(kSpiThumbMaxWidth * kSpiThumbMaxHeight * 2, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    }
    if (!g_spi_jpeg_full_buffer) {
        jpeg_decode_memory_alloc_cfg_t out_mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        g_spi_jpeg_full_buffer = static_cast<uint8_t *>(
            jpeg_alloc_decoder_mem(kThumbDecodeBufferBytes, &out_mem_cfg, &g_spi_jpeg_full_buffer_size));
    }
    if (!g_spi_thumb_buffer || !g_spi_thumb_staging || !g_spi_jpeg_full_buffer) {
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

    ESP_LOGI(TAG, "allocated SPI thumbnail buffers: %ux%u decode=%u alloc=%u hrnet=%u",
             kSpiThumbMaxWidth, kSpiThumbMaxHeight, kThumbDecodeBufferBytes,
             static_cast<unsigned>(g_spi_jpeg_full_buffer_size),
             static_cast<unsigned>(sizeof(HigherhrnetResult)));
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

    uint16_t scaled_width = static_cast<uint16_t>(std::max<uint32_t>(1, info.width / 2));
    uint16_t scaled_height = static_cast<uint16_t>(std::max<uint32_t>(1, info.height / 2));
    if (scaled_width > kSpiThumbMaxWidth || scaled_height > kSpiThumbMaxHeight) {
        ESP_LOGW(TAG, "SPI JPEG thumbnail too large after scaling: raw=%" PRIu32 "x%" PRIu32 " scaled=%ux%u limit=%ux%u",
                 info.width, info.height, scaled_width, scaled_height, kSpiThumbMaxWidth, kSpiThumbMaxHeight);
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

static void blit_spi_thumbnail_with_pose(uint16_t *frame, uint32_t frame_w, uint32_t frame_h)
{
    if (!frame || !g_overlay_mutex || !g_overlay_pose_snapshot) {
        return;
    }

    uint16_t local_thumb_w = 0;
    uint16_t local_thumb_h = 0;
    uint64_t local_pose_updated_us = 0;

    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    if (g_overlay_state.thumbnail_ready) {
        local_thumb_w = g_overlay_state.thumb_width;
        local_thumb_h = g_overlay_state.thumb_height;
        for (uint16_t y = 0; y < local_thumb_h && y < frame_h; ++y) {
            memcpy(frame + y * frame_w, g_spi_thumb_buffer + y * local_thumb_w * 2, local_thumb_w * 2);
        }
    }

    memcpy(g_overlay_pose_snapshot, &g_overlay_state.hrnet, sizeof(*g_overlay_pose_snapshot));
    local_pose_updated_us = g_overlay_state.hrnet_updated_us;
    xSemaphoreGive(g_overlay_mutex);

    if (local_thumb_w == 0 || local_thumb_h == 0 || now_us() - local_pose_updated_us > kPoseExpiryUs) {
        return;
    }

    float max_x = 0.0f;
    float max_y = 0.0f;
    for (uint32_t i = 0; i < g_overlay_pose_snapshot->pose_count; ++i) {
        for (uint32_t j = 0; j < kHigherhrnetJointCount; ++j) {
            if (g_overlay_pose_snapshot->poses[i].keypoints[j].score > 0.05f) {
                max_x = std::max(max_x, g_overlay_pose_snapshot->poses[i].keypoints[j].x);
                max_y = std::max(max_y, g_overlay_pose_snapshot->poses[i].keypoints[j].y);
            }
        }
    }

    float src_w = (max_x <= local_thumb_w * 1.5f && max_y <= local_thumb_h * 1.5f) ? static_cast<float>(local_thumb_w) : 384.0f;
    float src_h = (max_x <= local_thumb_w * 1.5f && max_y <= local_thumb_h * 1.5f) ? static_cast<float>(local_thumb_h) : 288.0f;
    static uint32_t mapping_log_count = 0;
    if (mapping_log_count < 5 || (mapping_log_count % 60) == 0) {
        ESP_LOGI(TAG, "pose overlay mapping: thumb=%ux%u max_pose=%.1fx%.1f src=%.0fx%.0f people=%" PRIu32,
                 local_thumb_w, local_thumb_h, max_x, max_y, src_w, src_h, g_overlay_pose_snapshot->pose_count);
    }
    mapping_log_count++;

    for (uint32_t i = 0; i < g_overlay_pose_snapshot->pose_count; ++i) {
        const HigherhrnetPose &pose = g_overlay_pose_snapshot->poses[i];
        int x1 = static_cast<int>(clamp_coord(pose.x1 * local_thumb_w / src_w, static_cast<float>(local_thumb_w - 1)));
        int y1 = static_cast<int>(clamp_coord(pose.y1 * local_thumb_h / src_h, static_cast<float>(local_thumb_h - 1)));
        int x2 = static_cast<int>(clamp_coord(pose.x2 * local_thumb_w / src_w, static_cast<float>(local_thumb_w - 1)));
        int y2 = static_cast<int>(clamp_coord(pose.y2 * local_thumb_h / src_h, static_cast<float>(local_thumb_h - 1)));
        draw_rectangle_rgb(frame, frame_w, frame_h, x1, y1, x2, y2, 0, 0, 0, 255, 0, 2, false);

        for (const auto &joint : kSkeleton) {
            const HigherhrnetKeypoint &a = pose.keypoints[joint[0]];
            const HigherhrnetKeypoint &b = pose.keypoints[joint[1]];
            if (a.score > 0.05f && b.score > 0.05f) {
                int ax = static_cast<int>(clamp_coord(a.x * local_thumb_w / src_w, static_cast<float>(local_thumb_w - 1)));
                int ay = static_cast<int>(clamp_coord(a.y * local_thumb_h / src_h, static_cast<float>(local_thumb_h - 1)));
                int bx = static_cast<int>(clamp_coord(b.x * local_thumb_w / src_w, static_cast<float>(local_thumb_w - 1)));
                int by = static_cast<int>(clamp_coord(b.y * local_thumb_h / src_h, static_cast<float>(local_thumb_h - 1)));
                draw_line_rgb(frame, frame_w, frame_h, ax, ay, bx, by, 0, 0, 255, 255, 255, 2, false);
            }
        }

        for (uint32_t j = 0; j < kHigherhrnetJointCount; ++j) {
            const HigherhrnetKeypoint &keypoint = pose.keypoints[j];
            if (keypoint.score <= 0.05f) {
                continue;
            }
            int x = static_cast<int>(clamp_coord(keypoint.x * local_thumb_w / src_w, static_cast<float>(local_thumb_w - 1)));
            int y = static_cast<int>(clamp_coord(keypoint.y * local_thumb_h / src_h, static_cast<float>(local_thumb_h - 1)));
            draw_point_rgb(frame, frame_w, frame_h, x, y, 0, 0, 255, 0, 0, 3, false);
        }
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

    uint8_t *display_buf = camera_buf;
    if (camera_buf_index < EXAMPLE_LCD_BUF_NUM && g_lcd_buffers[camera_buf_index] != nullptr) {
        display_buf = reinterpret_cast<uint8_t *>(g_lcd_buffers[camera_buf_index]);
        memcpy(display_buf, camera_buf, camera_buf_len);
        if (!compose_mode_logged) {
            ESP_LOGI(TAG, "display composition uses dedicated LCD buffer index=%u; SPI thumbnail is overlaid after the main frame leaves the capture pipeline",
                     camera_buf_index);
            compose_mode_logged = true;
        }
    }

    blit_spi_thumbnail_with_pose(reinterpret_cast<uint16_t *>(display_buf), camera_buf_hes, camera_buf_ves);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_display_panel, 0, 0, camera_buf_hes, camera_buf_ves, display_buf));

    uint64_t now = now_us();
    float fps = 0.0f;
    if (frame_count > 0 && now > first_frame_us) {
        fps = static_cast<float>(frame_count) * 1000000.0f / static_cast<float>(now - first_frame_us);
    }
    if (frame_count < 5 || (frame_count % 60) == 0) {
        ESP_LOGI(TAG, "display frame=%" PRIu32 " buf=%u size=%" PRIu32 "x%" PRIu32 " bytes=%u fps=%.2f",
                 frame_count, camera_buf_index, camera_buf_hes, camera_buf_ves, static_cast<unsigned>(camera_buf_len), fps);
    }
    frame_count++;
}

static esp_err_t init_mipi_display_path()
{
    struct v4l2_format format = {};

    ESP_LOGI(TAG, "initializing MIPI display path without reconfiguring the sensor");
    ESP_RETURN_ON_ERROR(app_lcd_init(&g_display_panel), TAG, "failed to init LCD");
    ESP_RETURN_ON_ERROR(example_video_init_preserving_sensor_state(), TAG, "failed to initialize video pipeline");

    g_video_fd = app_video_open((char *)EXAMPLE_CAM_DEV_PATH, APP_VIDEO_FMT);
    ESP_RETURN_ON_FALSE(g_video_fd >= 0, ESP_FAIL, TAG, "video open failed");

    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(g_display_panel, 2, &g_lcd_buffers[0], &g_lcd_buffers[1]),
                        TAG, "failed to get LCD frame buffers");

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
    ESP_LOGI(TAG,
             "programming IMX500 flash with embedded higherhrnet assets: fw=%u bytes nn_info=%u bytes",
             static_cast<unsigned>(NN_FW_SIZE),
             static_cast<unsigned>(NN_NETWORK_INFO_SIZE));
    if (!spi_slave_write_model_to_flash(NN_FW_DATA, static_cast<uint32_t>(NN_FW_SIZE))) {
        dump_spi_flash_status("model flash failed");
        return false;
    }

    if (!spi_slave_write_nn_info_to_flash(NN_NETWORK_INFO_DATA, static_cast<uint32_t>(NN_NETWORK_INFO_SIZE))) {
        dump_spi_flash_status("network_info flash failed");
        return false;
    }

    dump_spi_flash_status("flash programming complete");
    return true;
}

static const char *get_boot_mode_name()
{
#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    return "DIRECT_BOOT";
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    return "FLASH_BOOT";
#else
    return "UNKNOWN_BOOT_MODE";
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

#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    ESP_LOGI(TAG,
             "opening IMX500 with embedded higherhrnet assets directly: fw=%u bytes nn_info=%u bytes",
             static_cast<unsigned>(NN_FW_SIZE),
             static_cast<unsigned>(NN_NETWORK_INFO_SIZE));
    return open(NN_FW_DATA,
                static_cast<uint32_t>(NN_FW_SIZE),
                NN_NETWORK_INFO_DATA,
                static_cast<uint32_t>(NN_NETWORK_INFO_SIZE),
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    if (!program_flash_assets()) {
        return false;
    }

    ESP_LOGI(TAG, "opening IMX500 from module flash");
    return open(nullptr, 0, nullptr, 0,
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#else
#error "Unsupported INTEGRATION_TEST_BOOT_MODE"
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

        if (!g_overlay_state.spi_metadata_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!parse_output_tensor_data_with_metadata(g_frame_buf, static_cast<uint32_t>(data_size), g_parsed_metadata)) {
            ESP_LOGW(TAG, "failed to parse metadata frame=%u payload=%ld", 0u, static_cast<long>(data_size));
            if (is_zero_header_payload(g_frame_buf, static_cast<uint32_t>(data_size))) {
                g_overlay_state.zero_header_failures++;
                if (g_overlay_state.zero_header_failures >= kMetadataDisableThreshold) {
                    ESP_LOGW(TAG, "metadata appears disabled in firmware; disabling SPI metadata/AI overlay after %u consecutive zero-header frames",
                             g_overlay_state.zero_header_failures);
                    if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        g_overlay_state.spi_metadata_enabled = false;
                        g_overlay_state.ai_overlay_enabled = false;
                        clear_overlay_state_locked();
                        xSemaphoreGive(g_overlay_mutex);
                    }
                }
            } else {
                g_overlay_state.zero_header_failures = 0;
            }
            metadata_frame_count++;
            continue;
        }

        g_overlay_state.zero_header_failures = 0;
        if (metadata_frame_count < 5 || (metadata_frame_count % 30) == 0) {
            log_parsed_metadata_basic_info(*g_parsed_metadata, static_cast<uint32_t>(data_size));
        }
        update_spi_thumbnail_from_metadata(*g_parsed_metadata);

        if (g_spi_hrnet_result == nullptr) {
            ESP_LOGW(TAG, "hrnet result buffer is not ready");
            metadata_frame_count++;
            continue;
        }
        memset(g_spi_hrnet_result, 0, sizeof(*g_spi_hrnet_result));
        if (higherhrnet_postprocess_from_metadata(g_parsed_metadata, g_spi_hrnet_result)) {
            if (xSemaphoreTake(g_overlay_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                memcpy(&g_overlay_state.hrnet, g_spi_hrnet_result, sizeof(*g_spi_hrnet_result));
                g_overlay_state.hrnet_updated_us = now_us();
                xSemaphoreGive(g_overlay_mutex);
            }
            ESP_LOGI(TAG, "higherhrnet poses=%" PRIu32 " spi_thumb=%s %ux%u",
                     g_spi_hrnet_result->pose_count,
                     g_overlay_state.thumbnail_ready ? "ready" : "not-ready",
                     g_overlay_state.thumb_width,
                     g_overlay_state.thumb_height);
        }
        metadata_frame_count++;
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
    return ESP_OK;
}

} // namespace

extern "C" esp_err_t imx500_sdk_integration_test_run(void)
{
    ESP_RETURN_ON_FALSE(bind_peripherals_api(), ESP_FAIL, TAG, "failed to bind SDK peripherals");
    ESP_RETURN_ON_FALSE(allocate_spi_thumbnail_buffers(), ESP_FAIL, TAG, "failed to prepare SPI thumbnail buffers");

    ESP_RETURN_ON_FALSE(open_imx500_stream(), ESP_FAIL, TAG, "open() failed");
    ESP_RETURN_ON_FALSE(allocate_parsed_metadata_buffer(), ESP_FAIL, TAG, "failed to prepare parsed metadata buffer");
    ESP_RETURN_ON_ERROR(init_mipi_display_path(), TAG, "failed to initialize MIPI display path");
    ESP_RETURN_ON_ERROR(start_worker_tasks(), TAG, "failed to start display workers");

    ESP_LOGI(TAG, "starting IMX500 stream after arming display workers");
    stream_on();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "IMX500 test running: MIPI image is displayed on LCD, SPI metadata is parsed in the background");
    return ESP_OK;
}
