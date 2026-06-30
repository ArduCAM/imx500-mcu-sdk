#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>

#include "ArducamIMX500SDK.h"
#include "g_config.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "peripherals_adapter.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "wifi_config.h"

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 30000
#endif

#ifndef IMX500_ROI_MVP_VERBOSE_LOGS
#define IMX500_ROI_MVP_VERBOSE_LOGS 0
#endif

#define HTTP_SERVER_PORT 80
#define HTTP_HEADER_BYTES 256
#define HTTP_HTML_BYTES 8192
#define HTTP_JSON_BYTES 2048
#define HTTP_MAX_CLIENTS 2
#define HTTP_FRAME_REFRESH_MS 1000
#define HTTP_FRAME_RETRY_MS 1500
#define PERSON_DETECT_GPIO0_PIN 0
#define PERSON_DETECT_GPIO1_PIN 1
#ifndef PERSON_DETECT_GPIO_ACTIVE_LEVEL
#define PERSON_DETECT_GPIO_ACTIVE_LEVEL 1
#endif
#define IMX500_DEFAULT_SPI_BAUDRATE_HZ (5 * 1000 * 1000)
#define MAX_PERSON_DETECTIONS 10

static constexpr spi_data_format_t kImx500SpiMetadataFormat = IMX500_SPI_METADATA_FORMAT;
static constexpr bool kPersonDetectGpioActiveHigh = (PERSON_DETECT_GPIO_ACTIVE_LEVEL != 0);

static bool s_cyw43_ready = false;
static struct tcp_pcb *s_http_pcb = nullptr;
static uint32_t s_metadata_parse_failures = 0;
static uint8_t s_metadata_buf[METADATA_BUFFER_SIZE];
static IMX500ParsedMetadata s_parsed_metadata;
static float s_person_score_threshold = PERSON_SCORE_THRESHOLD;

struct PersonDetection {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int class_id;
    bool roi_intersects;
};

struct RelativePoint {
    float x;
    float y;
};

struct RoiConfig {
    bool configured;
    RelativePoint points[4];
};

struct PersonDetectionResult {
    uint32_t count;
    uint16_t source_width;
    uint16_t source_height;
    PersonDetection items[MAX_PERSON_DETECTIONS];
};

struct LatestBackendFrame {
    bool valid;
    uint32_t sequence;
    uint32_t payload_size;
    uint8_t frame_id;
    uint32_t jpeg_len;
    PersonDetectionResult detections;
    uint8_t jpeg[METADATA_BUFFER_SIZE];
};

struct HttpClientState {
    char header[HTTP_HEADER_BYTES];
    char inline_body[HTTP_HTML_BYTES];
    uint16_t header_len;
    uint16_t header_offset;
    uint8_t *body;
    uint32_t body_len;
    uint32_t body_offset;
    uint8_t poll_count;
    bool in_use;
    bool request_handled;
    bool latest_frame_locked;
};

static mutex_t s_latest_frame_mutex;
static mutex_t s_config_mutex;
static mutex_t s_roi_mutex;
static mutex_t s_http_client_mutex;
static LatestBackendFrame s_latest_frame = {};
static HttpClientState s_http_clients[HTTP_MAX_CLIENTS] = {};
static RoiConfig s_roi_config = {
    true,
    {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}
};
static volatile bool s_metadata_started = false;

static void service_wifi_connected_led(void) {
    if (!s_cyw43_ready) {
        return;
    }
    if (s_metadata_started) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        return;
    }

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const uint32_t phase = now_ms % 2000u;
    const uint32_t brightness = (phase < 1000u) ? phase / 10u : (2000u - phase) / 10u;
    const bool on = (now_ms % 100u) < brightness;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

static bool person_detect_output_level(bool warning_active) {
    return warning_active == kPersonDetectGpioActiveHigh;
}

static const char *gpio_level_text(bool high) {
    return high ? "high" : "low";
}

static const char *person_detect_gpio_active_level_text(void) {
    return kPersonDetectGpioActiveHigh ? "high" : "low";
}

static const char *person_detect_gpio_state_text(bool warning_active) {
    return warning_active ? "warning" : "idle";
}

static void put_person_detect_outputs(bool high) {
    gpio_put(PERSON_DETECT_GPIO0_PIN, high ? 1 : 0);
    gpio_put(PERSON_DETECT_GPIO1_PIN, high ? 1 : 0);
}

static void init_person_detect_outputs(void) {
    gpio_init(PERSON_DETECT_GPIO0_PIN);
    gpio_set_dir(PERSON_DETECT_GPIO0_PIN, GPIO_OUT);

    gpio_init(PERSON_DETECT_GPIO1_PIN);
    gpio_set_dir(PERSON_DETECT_GPIO1_PIN, GPIO_OUT);

    put_person_detect_outputs(person_detect_output_level(false));
}

static void set_person_detect_outputs(bool detected) {
    put_person_detect_outputs(person_detect_output_level(detected));
}

static void configure_spi_output_pin(uint pin) {
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
}

static void i2c_master_init(uint32_t baudrate) {
    i2c_init(I2C_HW_ADDR, baudrate);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    printf("I2C initialized at %lu Hz\n", (unsigned long)baudrate);
}

static void spi_master_init(uint32_t baudrate) {
    spi_init(SPI_HW_ADDR, baudrate);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CSN_PIN);
    gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
    gpio_put(SPI_CSN_PIN, 1);

    configure_spi_output_pin(SPI_SCK_PIN);
    configure_spi_output_pin(SPI_TX_PIN);
    configure_spi_output_pin(SPI_CSN_PIN);

    spi_set_format(SPI_HW_ADDR, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    printf("SPI initialized at %lu Hz\n", (unsigned long)baudrate);
}

static uint32_t tensor_bytes_per_element(const IMX500ParsedTensor &tensor) {
    return std::max<uint32_t>(1u, (static_cast<uint32_t>(tensor.bits_per_element) + 7u) / 8u);
}

static float tensor_read_value_raw_index(const IMX500ParsedTensor &tensor, uint32_t index) {
    if (tensor.data == nullptr || index >= tensor.element_count) {
        return 0.0f;
    }

    const uint8_t *ptr = tensor.data + index * tensor_bytes_per_element(tensor);
    float raw = 0.0f;
    switch (tensor.bits_per_element) {
    case 8:
        raw = (tensor.format == 0) ? static_cast<float>(*reinterpret_cast<const int8_t *>(ptr))
                                   : static_cast<float>(*ptr);
        break;
    case 16:
        raw = (tensor.format == 0) ? static_cast<float>(*reinterpret_cast<const int16_t *>(ptr))
                                   : static_cast<float>(*reinterpret_cast<const uint16_t *>(ptr));
        break;
    case 32:
        raw = (tensor.format == 0) ? static_cast<float>(*reinterpret_cast<const int32_t *>(ptr))
                                   : static_cast<float>(*reinterpret_cast<const uint32_t *>(ptr));
        break;
    default:
        break;
    }
    return (raw - static_cast<float>(tensor.zero_point)) * tensor.scale;
}

static bool tensor_logical_offset(const IMX500ParsedTensor &tensor,
                                  const uint32_t *coords,
                                  uint32_t coord_count,
                                  uint32_t *offset) {
    if (coords == nullptr || offset == nullptr || coord_count != tensor.dimension_count) {
        return false;
    }

    uint32_t stride = 1;
    uint32_t raw_index = 0;
    for (uint32_t i = 0; i < coord_count; ++i) {
        const uint32_t dim_size = tensor.dimensions[i].size;
        if (dim_size == 0 || coords[i] >= dim_size) {
            return false;
        }
        raw_index += coords[i] * stride;
        if (i + 1 < coord_count && stride > UINT32_MAX / dim_size) {
            return false;
        }
        stride *= dim_size;
    }

    *offset = raw_index;
    return true;
}

static bool tensor_read_value_logical(const IMX500ParsedTensor &tensor,
                                      const uint32_t *coords,
                                      uint32_t coord_count,
                                      float *value) {
    uint32_t raw_index = 0;
    if (value == nullptr || !tensor_logical_offset(tensor, coords, coord_count, &raw_index)) {
        return false;
    }
    *value = tensor_read_value_raw_index(tensor, raw_index);
    return true;
}

static bool detection_box_component(const IMX500ParsedTensor &tensor,
                                    uint32_t box_index,
                                    uint32_t component,
                                    float *value) {
    if (value == nullptr || component >= 4) {
        return false;
    }

    if (tensor.dimension_count >= 2) {
        uint32_t coords[IMX500_MAX_TENSOR_DIMS] = {};
        if (tensor.dimensions[tensor.dimension_count - 1].size == 4) {
            coords[tensor.dimension_count - 2] = box_index;
            coords[tensor.dimension_count - 1] = component;
            return tensor_read_value_logical(tensor, coords, tensor.dimension_count, value);
        }
        if (tensor.dimensions[0].size == 4) {
            coords[0] = component;
            coords[1] = box_index;
            return tensor_read_value_logical(tensor, coords, tensor.dimension_count, value);
        }
    }

    const uint32_t stride = tensor.element_count / 4u;
    if (stride == 0 || box_index >= stride) {
        return false;
    }
    *value = tensor_read_value_raw_index(tensor, box_index + stride * component);
    return true;
}

static bool get_network_input_hw(const IMX500ParsedNetwork &network, uint16_t *height, uint16_t *width) {
    if (network.input_tensor_count == 0 || network.input_tensors[0].dimension_count < 2) {
        return false;
    }
    if (height != nullptr) {
        *height = network.input_tensors[0].dimensions[0].size;
    }
    if (width != nullptr) {
        *width = network.input_tensors[0].dimensions[1].size;
    }
    return true;
}

static float clamp_float(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static float clamp01(float value) {
    return clamp_float(value, 0.0f, 1.0f);
}

static float get_person_score_threshold(void) {
    mutex_enter_blocking(&s_config_mutex);
    const float threshold = s_person_score_threshold;
    mutex_exit(&s_config_mutex);
    return threshold;
}

static void set_person_score_threshold(float threshold) {
    mutex_enter_blocking(&s_config_mutex);
    s_person_score_threshold = clamp_float(threshold, 0.0f, 1.0f);
    mutex_exit(&s_config_mutex);
}

static RoiConfig copy_roi_config(void) {
    RoiConfig roi = {};
    mutex_enter_blocking(&s_roi_mutex);
    roi = s_roi_config;
    mutex_exit(&s_roi_mutex);
    return roi;
}

static float cross_product(const RelativePoint &a, const RelativePoint &b, const RelativePoint &c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool point_in_rect(const RelativePoint &p, float x1, float y1, float x2, float y2) {
    return p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2;
}

static bool point_in_polygon(const RelativePoint &p, const RelativePoint *poly, uint32_t count) {
    bool inside = false;
    for (uint32_t i = 0, j = count - 1; i < count; j = i++) {
        const RelativePoint &a = poly[i];
        const RelativePoint &b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + 1e-7f) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

static bool segments_intersect(const RelativePoint &a,
                               const RelativePoint &b,
                               const RelativePoint &c,
                               const RelativePoint &d) {
    const float ab_c = cross_product(a, b, c);
    const float ab_d = cross_product(a, b, d);
    const float cd_a = cross_product(c, d, a);
    const float cd_b = cross_product(c, d, b);
    const float eps = 1e-6f;

    if (std::fabs(ab_c) <= eps && point_in_rect(c, std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y))) {
        return true;
    }
    if (std::fabs(ab_d) <= eps && point_in_rect(d, std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y))) {
        return true;
    }
    if (std::fabs(cd_a) <= eps && point_in_rect(a, std::min(c.x, d.x), std::min(c.y, d.y), std::max(c.x, d.x), std::max(c.y, d.y))) {
        return true;
    }
    if (std::fabs(cd_b) <= eps && point_in_rect(b, std::min(c.x, d.x), std::min(c.y, d.y), std::max(c.x, d.x), std::max(c.y, d.y))) {
        return true;
    }

    return ((ab_c > 0.0f) != (ab_d > 0.0f)) && ((cd_a > 0.0f) != (cd_b > 0.0f));
}

static bool detection_intersects_roi(const PersonDetection &item, const RoiConfig &roi) {
    if (!roi.configured) {
        return false;
    }

    const float x1 = std::min(item.x1, item.x2);
    const float y1 = std::min(item.y1, item.y2);
    const float x2 = std::max(item.x1, item.x2);
    const float y2 = std::max(item.y1, item.y2);
    const RelativePoint rect[4] = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};

    for (uint32_t i = 0; i < 4; ++i) {
        if (point_in_polygon(rect[i], roi.points, 4) || point_in_rect(roi.points[i], x1, y1, x2, y2)) {
            return true;
        }
    }

    for (uint32_t i = 0; i < 4; ++i) {
        const RelativePoint &ra = rect[i];
        const RelativePoint &rb = rect[(i + 1u) % 4u];
        for (uint32_t j = 0; j < 4; ++j) {
            if (segments_intersect(ra, rb, roi.points[j], roi.points[(j + 1u) % 4u])) {
                return true;
            }
        }
    }

    return false;
}

static bool is_all_zero_payload(const uint8_t *data, uint32_t len) {
    if (data == nullptr || len == 0) {
        return true;
    }

    for (uint32_t i = 0; i < len; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

#if IMX500_ROI_MVP_VERBOSE_LOGS
static uint32_t read_le32(const uint8_t *data, uint32_t len, uint32_t offset) {
    if (data == nullptr || offset + 4u > len) {
        return 0;
    }
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

static void print_hex_range(const char *label, const uint8_t *data, uint32_t len, uint32_t offset, uint32_t count) {
    printf("%s off=%lu len=%lu:", label, (unsigned long)offset, (unsigned long)count);
    if (data == nullptr || offset >= len) {
        printf(" <out-of-range>\n");
        return;
    }
    const uint32_t end = std::min<uint32_t>(len, offset + count);
    for (uint32_t i = offset; i < end; ++i) {
        printf(" %02X", data[i]);
    }
    printf("\n");
}

static void print_ascii_range(const char *label, const uint8_t *data, uint32_t len, uint32_t offset, uint32_t count) {
    printf("%s off=%lu len=%lu: \"", label, (unsigned long)offset, (unsigned long)count);
    if (data != nullptr && offset < len) {
        const uint32_t end = std::min<uint32_t>(len, offset + count);
        for (uint32_t i = offset; i < end; ++i) {
            const uint8_t ch = data[i];
            putchar((ch >= 32 && ch <= 126) ? (int)ch : '.');
        }
    }
    printf("\"\n");
}

static void dump_metadata_parse_debug(const uint8_t *data, uint32_t len, const char *reason) {
    IMX500OutputHeader header = {};
    if (data != nullptr && len >= IMX500_HEADER_LEN) {
        unpack_imx500_output_header(data, &header);
    }

    const uint32_t ap_offset = IMX500_HEADER_LEN;
    const uint32_t ap_size = header.size_of_ap_parameter;
    const uint32_t ap_end = ap_offset + ap_size;
    const bool ap_range_ok = data != nullptr && ap_end <= len;
    const uint32_t header_payload_bytes = ap_range_ok ? (len - ap_end) : 0;
    const uint32_t guessed_ap_size = (len >= IMX500_HEADER_LEN + SSD_EXPECTED_OUTPUT_BYTES)
                                         ? (len - IMX500_HEADER_LEN - SSD_EXPECTED_OUTPUT_BYTES)
                                         : 0;

    printf("\n----- metadata parse debug: %s -----\n", reason ? reason : "unknown");
    printf("payload_len=%lu buffer_capacity=%lu\n",
           (unsigned long)len,
           (unsigned long)sizeof(s_metadata_buf));
    printf("header valid=%u frame=%u max_line=%u ap_size=%u network_ordinal=%u indicator=%u\n",
           (unsigned)header.valid_flag,
           (unsigned)header.frame_count,
           (unsigned)header.max_length_of_line,
           (unsigned)header.size_of_ap_parameter,
           (unsigned)header.network_ordinal,
           (unsigned)header.indicator);
    printf("ap_offset=%lu ap_end=%lu ap_range_ok=%u payload_after_header_ap=%lu\n",
           (unsigned long)ap_offset,
           (unsigned long)ap_end,
           ap_range_ok ? 1u : 0u,
           (unsigned long)header_payload_bytes);
    printf("ssd_expected_output_bytes=%lu guessed_ap_size_from_payload=%lu header_ap_delta=%ld\n",
           (unsigned long)SSD_EXPECTED_OUTPUT_BYTES,
           (unsigned long)guessed_ap_size,
           (long)ap_size - (long)guessed_ap_size);

    if (data != nullptr && len >= 4) {
        printf("u32_le[0]=0x%08lx u32_le[4]=0x%08lx u32_le[8]=0x%08lx\n",
               (unsigned long)read_le32(data, len, 0),
               (unsigned long)read_le32(data, len, 4),
               (unsigned long)read_le32(data, len, 8));
    }

    print_hex_range("raw first 64", data, len, 0, 64);
    print_ascii_range("raw first 64 ascii", data, len, 0, 64);

    if (ap_range_ok) {
        print_hex_range("ap first 64", data, len, ap_offset, 64);
        print_ascii_range("ap first 64 ascii", data, len, ap_offset, 64);
        if (ap_size > 32) {
            print_hex_range("ap last 32", data, len, ap_end - 32, 32);
        }
        print_hex_range("raw output first 64 by header ap", data, len, ap_end, 64);
    }

    if (guessed_ap_size > 0 && guessed_ap_size != ap_size) {
        const uint32_t guessed_raw_offset = IMX500_HEADER_LEN + guessed_ap_size;
        print_hex_range("raw output first 64 by guessed ap", data, len, guessed_raw_offset, 64);
        if (guessed_ap_size > 32) {
            print_hex_range("guessed ap last 32", data, len, guessed_raw_offset - 32, 32);
        }
    }

    printf("----- metadata parse debug end -----\n\n");
}
#endif

static bool postprocess_person_detections(const IMX500ParsedMetadata &parsed, PersonDetectionResult *out) {
    if (out == nullptr || parsed.network_count == 0) {
        return false;
    }
    *out = {};

    const IMX500ParsedNetwork &network = parsed.networks[parsed.selected_network_index];
    if (network.output_tensor_count < 4) {
        printf("frame %u: expected 4 SSD output tensors, got %u\n",
               (unsigned)parsed.primary_header.frame_count,
               (unsigned)network.output_tensor_count);
        return false;
    }

    const IMX500ParsedTensor &boxes_tensor = network.output_tensors[0];
    const IMX500ParsedTensor &scores_tensor = network.output_tensors[1];
    const IMX500ParsedTensor &classes_tensor = network.output_tensors[2];
    const IMX500ParsedTensor &valid_tensor = network.output_tensors[3];

    uint16_t input_h = 320;
    uint16_t input_w = 320;
    get_network_input_hw(network, &input_h, &input_w);
    out->source_height = input_h;
    out->source_width = input_w;

    int valid_count_i = static_cast<int>(std::lround(tensor_read_value_raw_index(valid_tensor, 0)));
    if (valid_count_i < 0) {
        valid_count_i = 0;
    }
    uint32_t valid_count = static_cast<uint32_t>(valid_count_i);
    valid_count = std::min<uint32_t>(valid_count, scores_tensor.element_count);
    valid_count = std::min<uint32_t>(valid_count, classes_tensor.element_count);
    valid_count = std::min<uint32_t>(valid_count, boxes_tensor.element_count / 4u);

    float max_coord = 0.0f;
    const float score_threshold = get_person_score_threshold();
    PersonDetectionResult staged = {};
    staged.source_height = input_h;
    staged.source_width = input_w;

    for (uint32_t i = 0; i < valid_count && staged.count < MAX_PERSON_DETECTIONS; ++i) {
        const float score = tensor_read_value_raw_index(scores_tensor, i);
        if (score < score_threshold) {
            continue;
        }

        const int class_id = static_cast<int>(std::lround(tensor_read_value_raw_index(classes_tensor, i)));
        if (class_id != PERSON_CLASS_ID) {
            continue;
        }

        float y1 = 0.0f;
        float x1 = 0.0f;
        float y2 = 0.0f;
        float x2 = 0.0f;
        if (!detection_box_component(boxes_tensor, i, 0, &y1) ||
            !detection_box_component(boxes_tensor, i, 1, &x1) ||
            !detection_box_component(boxes_tensor, i, 2, &y2) ||
            !detection_box_component(boxes_tensor, i, 3, &x2)) {
            continue;
        }

        max_coord = std::max(max_coord, std::fabs(x1));
        max_coord = std::max(max_coord, std::fabs(y1));
        max_coord = std::max(max_coord, std::fabs(x2));
        max_coord = std::max(max_coord, std::fabs(y2));

        PersonDetection &item = staged.items[staged.count++];
        item.x1 = x1;
        item.y1 = y1;
        item.x2 = x2;
        item.y2 = y2;
        item.score = score;
    }

    const bool normalized_coords = max_coord <= 1.5f;
    const RoiConfig roi = copy_roi_config();
    for (uint32_t i = 0; i < staged.count; ++i) {
        PersonDetection &item = staged.items[i];
        if (!normalized_coords) {
            item.x1 /= static_cast<float>(input_w);
            item.x2 /= static_cast<float>(input_w);
            item.y1 /= static_cast<float>(input_h);
            item.y2 /= static_cast<float>(input_h);
        }
        item.x1 = clamp01(item.x1);
        item.x2 = clamp01(item.x2);
        item.y1 = clamp01(item.y1);
        item.y2 = clamp01(item.y2);
        item.class_id = PERSON_CLASS_ID;
        item.roi_intersects = detection_intersects_roi(item, roi);
    }

    *out = staged;
    return true;
}

static void print_person_detections(uint8_t frame_id, uint32_t payload_size, const PersonDetectionResult &result) {
#if IMX500_ROI_MVP_VERBOSE_LOGS
    const float score_threshold = get_person_score_threshold();
    printf("\n========== frame %u ==========\n", (unsigned)frame_id);
    printf("payload=%lu bytes person_count=%lu source=%ux%u threshold=%.2f class=%d coords=relative\n",
           (unsigned long)payload_size,
           (unsigned long)result.count,
           (unsigned)result.source_width,
           (unsigned)result.source_height,
           (double)score_threshold,
           (int)PERSON_CLASS_ID);

    for (uint32_t i = 0; i < result.count; ++i) {
        const PersonDetection &item = result.items[i];
        printf("person[%lu]: score=%.3f box_rel_xyxy=(%.4f, %.4f, %.4f, %.4f) roi_intersects=%u\n",
               (unsigned long)i,
               (double)item.score,
               (double)item.x1,
               (double)item.y1,
               (double)item.x2,
               (double)item.y2,
               item.roi_intersects ? 1u : 0u);
    }
#else
    (void)frame_id;
    (void)payload_size;
    (void)result;
#endif
}

static bool store_latest_frame(uint32_t payload_size,
                               const IMX500ParsedMetadata &parsed,
                               const PersonDetectionResult &detections) {
    uint32_t jpeg_len = parsed.jpeg_data_len;
    if (parsed.jpeg_data == nullptr) {
        jpeg_len = 0;
    }
    jpeg_len = std::min<uint32_t>(jpeg_len, METADATA_BUFFER_SIZE);

    if (!mutex_try_enter(&s_latest_frame_mutex, nullptr)) {
        return false;
    }
    s_latest_frame.valid = true;
    s_latest_frame.sequence++;
    s_latest_frame.payload_size = payload_size;
    s_latest_frame.frame_id = parsed.primary_header.frame_count;
    s_latest_frame.jpeg_len = jpeg_len;
    s_latest_frame.detections = detections;
    if (jpeg_len > 0) {
        memcpy(s_latest_frame.jpeg, parsed.jpeg_data, jpeg_len);
    }
    mutex_exit(&s_latest_frame_mutex);
    return true;
}

static bool init_imx500_streaming(void) {
    service_wifi_connected_led();
    i2c_master_init(100 * 1000);
    service_wifi_connected_led();
    spi_master_init(IMX500_DEFAULT_SPI_BAUDRATE_HZ);
    service_wifi_connected_led();
    bind_peripherals_api();

    uint32_t module_fw_ver = 0;
    get_fw_ver(&module_fw_ver);
    printf("module fw version: 0x%08lx\n", (unsigned long)module_fw_ver);
    service_wifi_connected_led();

    uint32_t module_pid = 0;
    get_pid(&module_pid);
    printf("module pid: 0x%08lx\n", (unsigned long)module_pid);
    service_wifi_connected_led();

    printf("Opening IMX500 with model/network_info already deployed in module flash...\n");
    if (!imx500_open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, kImx500SpiMetadataFormat, IMX500_STREAM_FPS)) {
        printf("imx500_open() failed\n");
        return false;
    }

    stream_on();
    printf("IMX500 stream_on complete\n");
    return true;
}

static void metadata_worker_core1(void) {
    printf("metadata worker started on core1\n");
    uint32_t metadata_read_failures = 0;
    while (true) {
        const int32_t bytes_read = read_metadata(s_metadata_buf, sizeof(s_metadata_buf));
        if (bytes_read <= 0) {
            ++metadata_read_failures;
            if (metadata_read_failures <= 3 || (metadata_read_failures % 100u) == 0u) {
                printf("read_metadata failed count=%lu\n", (unsigned long)metadata_read_failures);
            }
            sleep_ms(10);
            continue;
        }
        metadata_read_failures = 0;
        if (is_all_zero_payload(s_metadata_buf, static_cast<uint32_t>(bytes_read))) {
            continue;
        }
        if (!s_metadata_started) {
            s_metadata_started = true;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        }

        if (!parse_metadata(
                s_metadata_buf,
                static_cast<uint32_t>(bytes_read),
                kImx500SpiMetadataFormat,
                &s_parsed_metadata)) {
            ++s_metadata_parse_failures;
            if (s_metadata_parse_failures <= 3 || (s_metadata_parse_failures % 100u) == 0u) {
                printf(
                    "metadata parse failed count=%lu payload=%ld\n",
                    (unsigned long)s_metadata_parse_failures,
                    (long)bytes_read
                );
            }
#if IMX500_ROI_MVP_VERBOSE_LOGS
            if (s_metadata_parse_failures <= METADATA_DEBUG_FRAMES ||
                (s_metadata_parse_failures % 30u) == 0u) {
                dump_metadata_parse_debug(
                    s_metadata_buf,
                    static_cast<uint32_t>(bytes_read),
                    "parse_metadata failed"
                );
            }
#endif
            continue;
        }
        s_metadata_parse_failures = 0;

        PersonDetectionResult detections = {};
        if (postprocess_person_detections(s_parsed_metadata, &detections)) {
            print_person_detections(
                s_parsed_metadata.primary_header.frame_count,
                static_cast<uint32_t>(bytes_read),
                detections
            );
            store_latest_frame(static_cast<uint32_t>(bytes_read), s_parsed_metadata, detections);
            set_person_detect_outputs(detections.count > 0);
        }
    }
}

static const char *get_board_ip_string(void) {
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
}

static void set_roi_config(const RelativePoint *points) {
    mutex_enter_blocking(&s_roi_mutex);
    s_roi_config.configured = true;
    for (uint32_t i = 0; i < 4; ++i) {
        s_roi_config.points[i].x = clamp01(points[i].x);
        s_roi_config.points[i].y = clamp01(points[i].y);
    }
    mutex_exit(&s_roi_mutex);
}

static HttpClientState *allocate_http_client_state(void) {
    HttpClientState *state = nullptr;
    mutex_enter_blocking(&s_http_client_mutex);
    for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i) {
        if (!s_http_clients[i].in_use) {
            state = &s_http_clients[i];
            memset(state, 0, sizeof(*state));
            state->in_use = true;
            break;
        }
    }
    mutex_exit(&s_http_client_mutex);
    return state;
}

static void release_http_client_state(HttpClientState *state) {
    if (state == nullptr) {
        return;
    }
    if (state->latest_frame_locked) {
        mutex_exit(&s_latest_frame_mutex);
        state->latest_frame_locked = false;
    }

    mutex_enter_blocking(&s_http_client_mutex);
    memset(state, 0, sizeof(*state));
    mutex_exit(&s_http_client_mutex);
}

static void free_http_client_state(HttpClientState *state) {
    release_http_client_state(state);
}

static err_t close_http_client(struct tcp_pcb *pcb, HttpClientState *state) {
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    tcp_err(pcb, nullptr);
    free_http_client_state(state);

    const err_t close_err = tcp_close(pcb);
    if (close_err != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static bool lock_latest_jpeg_for_response(HttpClientState *state) {
    if (state == nullptr) {
        return false;
    }

    if (!mutex_try_enter(&s_latest_frame_mutex, nullptr)) {
        return false;
    }
    const bool available = s_latest_frame.valid && s_latest_frame.jpeg_len > 0;
    if (!available) {
        mutex_exit(&s_latest_frame_mutex);
        return false;
    }

    state->body = s_latest_frame.jpeg;
    state->body_len = s_latest_frame.jpeg_len;
    state->latest_frame_locked = true;
    return true;
}

static err_t send_http_chunks(struct tcp_pcb *pcb, HttpClientState *state) {
    if (pcb == nullptr || state == nullptr) {
        return ERR_ARG;
    }

    while (state->header_offset < state->header_len) {
        const uint16_t available = tcp_sndbuf(pcb);
        if (available == 0) {
            break;
        }
        const uint16_t remaining = state->header_len - state->header_offset;
        const uint16_t chunk = std::min<uint16_t>(available, remaining);
        const err_t err = tcp_write(
            pcb,
            state->header + state->header_offset,
            chunk,
            TCP_WRITE_FLAG_COPY
        );
        if (err != ERR_OK) {
            if (err == ERR_MEM) {
                tcp_output(pcb);
                return ERR_OK;
            }
            return err;
        }
        state->header_offset += chunk;
    }

    while (state->header_offset >= state->header_len && state->body_offset < state->body_len) {
        const uint16_t available = tcp_sndbuf(pcb);
        if (available == 0) {
            break;
        }
        const uint32_t remaining = state->body_len - state->body_offset;
        const uint16_t chunk = std::min<uint32_t>(available, std::min<uint32_t>(remaining, TCP_MSS));
        const err_t err = tcp_write(
            pcb,
            state->body + state->body_offset,
            chunk,
            TCP_WRITE_FLAG_COPY
        );
        if (err != ERR_OK) {
            if (err == ERR_MEM) {
                tcp_output(pcb);
                return ERR_OK;
            }
            return err;
        }
        state->body_offset += chunk;
    }

    tcp_output(pcb);

    if (state->header_offset >= state->header_len && state->body_offset >= state->body_len) {
        return close_http_client(pcb, state);
    }
    return ERR_OK;
}

static err_t http_sent_callback(void *arg, struct tcp_pcb *pcb, uint16_t len) {
    (void)len;
    HttpClientState *state = static_cast<HttpClientState *>(arg);
    if (state != nullptr) {
        state->poll_count = 0;
    }
    return send_http_chunks(pcb, state);
}

static err_t http_poll_callback(void *arg, struct tcp_pcb *pcb) {
    HttpClientState *state = static_cast<HttpClientState *>(arg);
    if (state == nullptr) {
        return close_http_client(pcb, state);
    }

    if (++state->poll_count > 10) {
        return close_http_client(pcb, state);
    }

    if (state->request_handled) {
        return send_http_chunks(pcb, state);
    }
    return ERR_OK;
}

static void http_error_callback(void *arg, err_t err) {
    (void)err;
    free_http_client_state(static_cast<HttpClientState *>(arg));
}

static bool set_http_body(HttpClientState *state, const uint8_t *body, uint32_t body_len) {
    if (state == nullptr) {
        return false;
    }

    if (body_len == 0) {
        return true;
    }
    if (body_len > sizeof(state->inline_body)) {
        return false;
    }

    state->body = reinterpret_cast<uint8_t *>(state->inline_body);
    memcpy(state->body, body, body_len);
    state->body_len = body_len;
    return true;
}

static bool set_http_text_body(HttpClientState *state, const char *body) {
    return set_http_body(
        state,
        reinterpret_cast<const uint8_t *>(body),
        static_cast<uint32_t>(strlen(body))
    );
}

static size_t append_http_text(char *buffer, size_t buffer_size, size_t used, const char *format, ...) {
    if (used >= buffer_size) {
        return used;
    }

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);
    if (written < 0) {
        return used;
    }

    const size_t requested = static_cast<size_t>(written);
    return (requested >= buffer_size - used) ? (buffer_size - 1) : (used + requested);
}

static void prepare_http_body_response(HttpClientState *state,
                                       const char *status,
                                       const char *content_type,
                                       const char *body) {
    const size_t body_len = strlen(body);
    if (!set_http_text_body(state, body)) {
        status = "500 Internal Server Error";
        content_type = "text/plain";
        body = "Out of memory.\n";
        set_http_text_body(state, body);
    }

    const int header_len = snprintf(
        state->header,
        sizeof(state->header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        content_type,
        (unsigned long)state->body_len
    );
    state->header_len = header_len > 0 ? std::min<int>(header_len, sizeof(state->header) - 1) : 0;
}

static bool query_get_float(const char *query, const char *key, float *value) {
    if (query == nullptr || key == nullptr || value == nullptr) {
        return false;
    }

    const size_t key_len = strlen(key);
    const char *cursor = query;
    while (*cursor != '\0') {
        if ((cursor == query || cursor[-1] == '&') &&
            strncmp(cursor, key, key_len) == 0 &&
            cursor[key_len] == '=') {
            char *end = nullptr;
            const float parsed = strtof(cursor + key_len + 1, &end);
            if (end != cursor + key_len + 1) {
                *value = parsed;
                return true;
            }
        }

        cursor = strchr(cursor, '&');
        if (cursor == nullptr) {
            break;
        }
        ++cursor;
    }

    return false;
}

static bool apply_config_query(const char *query) {
    bool changed = false;
    float confidence = 0.0f;
    if (query_get_float(query, "conf", &confidence)) {
        set_person_score_threshold(confidence);
        changed = true;
    }

    RelativePoint points[4] = {};
    bool roi_complete = true;
    for (uint32_t i = 0; i < 4; ++i) {
        char key_x[4];
        char key_y[4];
        snprintf(key_x, sizeof(key_x), "x%lu", (unsigned long)i);
        snprintf(key_y, sizeof(key_y), "y%lu", (unsigned long)i);
        roi_complete = query_get_float(query, key_x, &points[i].x) &&
                       query_get_float(query, key_y, &points[i].y) &&
                       roi_complete;
    }

    if (roi_complete) {
        set_roi_config(points);
        changed = true;
    }

    return changed;
}

static void build_home_page(char *body, size_t body_size, const char *message) {
    const RoiConfig roi = copy_roi_config();
    uint32_t frame_sequence = 0;
    uint32_t jpeg_len = 0;
    uint32_t detection_count = 0;
    if (mutex_try_enter(&s_latest_frame_mutex, nullptr)) {
        frame_sequence = s_latest_frame.sequence;
        jpeg_len = s_latest_frame.jpeg_len;
        detection_count = s_latest_frame.detections.count;
        mutex_exit(&s_latest_frame_mutex);
    }

    const float threshold = get_person_score_threshold();
    const bool warning_active = detection_count > 0;
    const bool gpio_high = person_detect_output_level(warning_active);
    char gpio_status[64];
    snprintf(
        gpio_status,
        sizeof(gpio_status),
        "%s (%s, active-%s)",
        gpio_level_text(gpio_high),
        person_detect_gpio_state_text(warning_active),
        person_detect_gpio_active_level_text()
    );

    snprintf(
        body,
        body_size,
        "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>IMX500 Person Detect</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "html,body{height:100%%}"
        "body{margin:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,sans-serif;background:#101214;color:#eee;overflow:hidden}"
        "main{height:100vh;display:grid;grid-template-columns:minmax(0,1fr) 310px;gap:10px;padding:10px}"
        "#viewer{position:relative;min-width:0;min-height:0;background:#202326;border:1px solid #3a3d41;display:flex;align-items:center;justify-content:center;overflow:hidden}"
        "img{display:block;max-width:100%%;max-height:calc(100vh - 22px);width:auto;height:auto;background:#222}"
        "#overlay{position:absolute;left:0;top:0;pointer-events:none}"
        "aside{min-width:0;display:grid;grid-template-rows:auto auto 1fr;gap:10px}"
        "section{padding:10px;border:1px solid #34383d;background:#181b1f}"
        "h2{font-size:18px;margin:0 0 8px}"
        ".stats{display:grid;grid-template-columns:1fr 1fr;gap:6px;font-size:13px}"
        ".stat{padding:6px;background:#22262b;border:1px solid #363b41}"
        ".stat b{display:block;color:#8db7ff;font-weight:600}"
        ".msg{color:#8fdb8f;font-size:13px;min-height:18px}"
        "label{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:6px 0;color:#ccc;font-size:13px}"
        "input{width:5.6rem;background:#222;color:#eee;border:1px solid #555;padding:4px}"
        "button{width:100%%;margin-top:8px;padding:8px 10px;background:#2b6cff;color:white;border:0}"
        ".grid{display:grid;grid-template-columns:1fr 1fr;gap:0 8px}"
        "@media(max-width:760px){body{overflow:auto}main{height:auto;grid-template-columns:1fr}img{max-height:none;width:100%%}aside{grid-template-rows:auto}}"
        "</style></head><body><main>"
        "<div id=\"viewer\"><img id=\"frame\" src=\"/frame.jpg?t=0\" alt=\"latest jpeg frame\"><canvas id=\"overlay\"></canvas></div>"
        "<aside><section><h2>IMX500 Person Detect</h2><div class=\"stats\">"
        "<div class=\"stat\"><b>Frame</b><span id=\"statFrame\">%lu</span></div>"
        "<div class=\"stat\"><b>JPEG</b><span id=\"statJpeg\">%lu</span></div>"
        "<div class=\"stat\"><b>Persons</b><span id=\"statPersons\">%lu</span></div>"
        "<div class=\"stat\"><b>GP0/GP1</b><span id=\"statGpio\">%s</span></div>"
        "<div class=\"stat\"><b>Conf</b><span id=\"statConf\">%.2f</span></div>"
        "<div class=\"stat\"><b>ROI</b><span id=\"statRoi\">full</span></div>"
        "</div><div class=\"msg\">%s</div></section>"
        "<section><form action=\"/config\" method=\"get\">"
        "<label>conf<input name=\"conf\" type=\"number\" min=\"0\" max=\"1\" step=\"0.01\" value=\"%.2f\"></label>"
        "<div class=\"grid\">"
        "<label>x0<input name=\"x0\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>y0<input name=\"y0\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>x1<input name=\"x1\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>y1<input name=\"y1\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>x2<input name=\"x2\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>y2<input name=\"y2\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>x3<input name=\"x3\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "<label>y3<input name=\"y3\" type=\"number\" min=\"0\" max=\"1\" step=\"0.001\" value=\"%.3f\"></label>"
        "</div><button type=\"submit\">Apply</button></form></section></aside>"
        "<script>"
        "const viewer=document.getElementById('viewer');"
        "const img=document.getElementById('frame');"
        "const canvas=document.getElementById('overlay');"
        "const ctx=canvas.getContext('2d');"
        "let pending=false;"
        "function resize(){const vr=viewer.getBoundingClientRect();const r=img.getBoundingClientRect();const w=Math.max(1,Math.round(r.width));const h=Math.max(1,Math.round(r.height));"
        "canvas.style.left=Math.round(r.left-vr.left)+'px';canvas.style.top=Math.round(r.top-vr.top)+'px';canvas.style.width=w+'px';canvas.style.height=h+'px';"
        "if(canvas.width!==w)canvas.width=w;if(canvas.height!==h)canvas.height=h;}"
        "function box(b){const w=canvas.width,h=canvas.height;const x=b[0]*w,y=b[1]*h;return[x,y,(b[2]-b[0])*w,(b[3]-b[1])*h]}"
        "function text(id,v){const e=document.getElementById(id);if(e)e.textContent=v}"
        "function gpioText(g){return g?g.level+' ('+(g.warning?'warning':'idle')+', active-'+g.active_level+')':'n/a'}"
        "function draw(s){text('statFrame',s.sequence);text('statJpeg',s.jpeg_bytes);text('statPersons',s.person_count);text('statGpio',gpioText(s.gpio));text('statConf',Number(s.threshold).toFixed(2));text('statRoi',s.roi&&s.roi.configured?'set':'off');resize();const w=canvas.width,h=canvas.height;ctx.clearRect(0,0,w,h);ctx.lineWidth=2;ctx.font='14px system-ui';"
        "if(s.roi&&s.roi.configured){const p=s.roi.points;ctx.beginPath();ctx.moveTo(p[0][0]*w,p[0][1]*h);for(let i=1;i<p.length;i++)ctx.lineTo(p[i][0]*w,p[i][1]*h);ctx.closePath();ctx.strokeStyle='rgba(0,200,255,.95)';ctx.stroke();}"
        "(s.detections||[]).forEach(d=>{const r=box(d.box);ctx.strokeStyle=d.roi_intersects?'#ff3b30':'#2bd66f';ctx.fillStyle=ctx.strokeStyle;ctx.strokeRect(r[0],r[1],r[2],r[3]);ctx.fillText('person '+Math.round(d.score*100)+'%%',r[0]+4,Math.max(14,r[1]-4));});}"
        "async function loadStatus(){try{const r=await fetch('/status.json?t='+Date.now(),{cache:'no-store'});if(r.ok)draw(await r.json())}catch(e){}}"
        "function refresh(){"
        "if(pending)return;"
        "pending=true;"
        "const next=new Image();"
        "next.onload=()=>{img.src=next.src;loadStatus();pending=false;setTimeout(refresh,%u)};"
        "next.onerror=()=>{pending=false;setTimeout(refresh,%u)};"
        "next.src='/frame.jpg?t='+Date.now();"
        "}"
        "window.addEventListener('resize',loadStatus);"
        "setTimeout(loadStatus,500);"
        "setTimeout(refresh,200);"
        "</script></main></body></html>",
        (unsigned long)frame_sequence,
        (unsigned long)jpeg_len,
        (unsigned long)detection_count,
        gpio_status,
        (double)threshold,
        message != nullptr ? message : "",
        (double)threshold,
        (double)roi.points[0].x,
        (double)roi.points[0].y,
        (double)roi.points[1].x,
        (double)roi.points[1].y,
        (double)roi.points[2].x,
        (double)roi.points[2].y,
        (double)roi.points[3].x,
        (double)roi.points[3].y,
        (unsigned)HTTP_FRAME_REFRESH_MS,
        (unsigned)HTTP_FRAME_RETRY_MS
    );
}

static bool prepare_http_home_response(HttpClientState *state, const char *message) {
    if (state == nullptr) {
        return false;
    }

    build_home_page(state->inline_body, sizeof(state->inline_body), message);
    state->body = reinterpret_cast<uint8_t *>(state->inline_body);
    state->body_len = static_cast<uint32_t>(strlen(state->inline_body));

    const int header_len = snprintf(
        state->header,
        sizeof(state->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        (unsigned long)state->body_len
    );
    state->header_len = header_len > 0 ? std::min<int>(header_len, sizeof(state->header) - 1) : 0;
    return state->header_len > 0;
}

static bool prepare_http_not_found_response(HttpClientState *state) {
    if (state == nullptr) {
        return false;
    }
    prepare_http_body_response(state, "404 Not Found", "text/plain", "Not found.\n");
    return state->header_len > 0;
}

static size_t build_status_json(char *body, size_t body_size) {
    const RoiConfig roi = copy_roi_config();
    const float threshold = get_person_score_threshold();

    bool valid = false;
    uint32_t sequence = 0;
    uint8_t frame_id = 0;
    uint32_t jpeg_len = 0;
    PersonDetectionResult detections = {};
    if (mutex_try_enter(&s_latest_frame_mutex, nullptr)) {
        valid = s_latest_frame.valid;
        sequence = s_latest_frame.sequence;
        frame_id = s_latest_frame.frame_id;
        jpeg_len = s_latest_frame.jpeg_len;
        detections = s_latest_frame.detections;
        mutex_exit(&s_latest_frame_mutex);
    }

    size_t used = 0;
    const bool warning_active = detections.count > 0;
    const bool gpio_high = person_detect_output_level(warning_active);
    used = append_http_text(
        body,
        body_size,
        used,
        "{\"valid\":%s,\"sequence\":%lu,\"frame\":%u,\"jpeg_bytes\":%lu,"
        "\"person_count\":%lu,\"threshold\":%.3f,"
        "\"gpio\":{\"pins\":[%u,%u],\"level\":\"%s\",\"active_level\":\"%s\",\"warning\":%s},"
        "\"roi\":{\"configured\":%s,\"points\":[[%.6f,%.6f],[%.6f,%.6f],[%.6f,%.6f],[%.6f,%.6f]]},"
        "\"detections\":[",
        valid ? "true" : "false",
        (unsigned long)sequence,
        (unsigned)frame_id,
        (unsigned long)jpeg_len,
        (unsigned long)detections.count,
        (double)threshold,
        (unsigned)PERSON_DETECT_GPIO0_PIN,
        (unsigned)PERSON_DETECT_GPIO1_PIN,
        gpio_level_text(gpio_high),
        person_detect_gpio_active_level_text(),
        warning_active ? "true" : "false",
        roi.configured ? "true" : "false",
        (double)roi.points[0].x,
        (double)roi.points[0].y,
        (double)roi.points[1].x,
        (double)roi.points[1].y,
        (double)roi.points[2].x,
        (double)roi.points[2].y,
        (double)roi.points[3].x,
        (double)roi.points[3].y
    );

    for (uint32_t i = 0; i < detections.count; ++i) {
        const PersonDetection &item = detections.items[i];
        used = append_http_text(
            body,
            body_size,
            used,
            "%s{\"score\":%.4f,\"box\":[%.6f,%.6f,%.6f,%.6f],\"roi_intersects\":%s}",
            i == 0 ? "" : ",",
            (double)item.score,
            (double)item.x1,
            (double)item.y1,
            (double)item.x2,
            (double)item.y2,
            item.roi_intersects ? "true" : "false"
        );
    }

    used = append_http_text(body, body_size, used, "]}");
    return used;
}

static bool prepare_http_status_response(HttpClientState *state) {
    if (state == nullptr) {
        return false;
    }

    state->body_len = static_cast<uint32_t>(build_status_json(state->inline_body, HTTP_JSON_BYTES));
    state->body = reinterpret_cast<uint8_t *>(state->inline_body);

    const int header_len = snprintf(
        state->header,
        sizeof(state->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        (unsigned long)state->body_len
    );
    state->header_len = header_len > 0 ? std::min<int>(header_len, sizeof(state->header) - 1) : 0;
    return state->header_len > 0;
}

static bool prepare_http_jpeg_response(HttpClientState *state) {
    if (state == nullptr) {
        return false;
    }

    if (!lock_latest_jpeg_for_response(state)) {
        prepare_http_body_response(state, "503 Service Unavailable", "text/plain", "No JPEG frame available yet.\n");
        return true;
    }

    const int header_len = snprintf(
        state->header,
        sizeof(state->header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        (unsigned long)state->body_len
    );
    state->header_len = header_len > 0 ? std::min<int>(header_len, sizeof(state->header) - 1) : 0;
    return state->header_len > 0;
}

static void parse_http_request_uri(struct pbuf *p, char *path, size_t path_size, char *query, size_t query_size) {
    if (path == nullptr || query == nullptr || path_size == 0 || query_size == 0) {
        return;
    }
    path[0] = '\0';
    query[0] = '\0';

    char request[256] = {};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';

    char *method_end = strchr(request, ' ');
    if (method_end == nullptr) {
        snprintf(path, path_size, "/");
        return;
    }
    char *uri = method_end + 1;
    char *uri_end = strchr(uri, ' ');
    if (uri_end == nullptr) {
        snprintf(path, path_size, "/");
        return;
    }
    *uri_end = '\0';

    char *query_start = strchr(uri, '?');
    if (query_start != nullptr) {
        *query_start = '\0';
        ++query_start;
        snprintf(query, query_size, "%s", query_start);
    }

    snprintf(path, path_size, "%s", uri[0] != '\0' ? uri : "/");
}

static bool prepare_http_response_for_request(HttpClientState *state, const char *path, const char *query) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        return prepare_http_home_response(state, "");
    }
    if (strcmp(path, "/frame.jpg") == 0 || strcmp(path, "/frame.jpeg") == 0) {
        return prepare_http_jpeg_response(state);
    }
    if (strcmp(path, "/status.json") == 0) {
        return prepare_http_status_response(state);
    }
    if (strcmp(path, "/config") == 0) {
        const bool changed = apply_config_query(query);
        return prepare_http_home_response(state, changed ? "Config applied." : "No config value changed.");
    }
    return prepare_http_not_found_response(state);
}

static err_t http_recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    HttpClientState *state = static_cast<HttpClientState *>(arg);
    if (err != ERR_OK) {
        if (p != nullptr) {
            pbuf_free(p);
        }
        return close_http_client(pcb, state);
    }

    if (p == nullptr) {
        return close_http_client(pcb, state);
    }

    char path[64] = {};
    char query[192] = {};
    parse_http_request_uri(p, path, sizeof(path), query, sizeof(query));
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (state == nullptr || state->request_handled) {
        return ERR_OK;
    }

    state->request_handled = true;
    if (!prepare_http_response_for_request(state, path, query)) {
        return close_http_client(pcb, state);
    }

    tcp_arg(pcb, state);
    tcp_sent(pcb, http_sent_callback);
    tcp_err(pcb, http_error_callback);
    return send_http_chunks(pcb, state);
}

static err_t http_accept_callback(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || new_pcb == nullptr) {
        return ERR_VAL;
    }

    HttpClientState *state = allocate_http_client_state();
    if (state == nullptr) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    tcp_arg(new_pcb, state);
    tcp_recv(new_pcb, http_recv_callback);
    tcp_err(new_pcb, http_error_callback);
    tcp_poll(new_pcb, http_poll_callback, 4);
    tcp_nagle_disable(new_pcb);
    return ERR_OK;
}

static bool start_http_jpeg_server(void) {
    cyw43_arch_lwip_begin();

    s_http_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (s_http_pcb == nullptr) {
        cyw43_arch_lwip_end();
        printf("tcp_new_ip_type() failed\n");
        return false;
    }

    const err_t bind_err = tcp_bind(s_http_pcb, IP_ADDR_ANY, HTTP_SERVER_PORT);
    if (bind_err != ERR_OK) {
        tcp_close(s_http_pcb);
        s_http_pcb = nullptr;
        cyw43_arch_lwip_end();
        printf("tcp_bind() failed, err=%d\n", bind_err);
        return false;
    }

    s_http_pcb = tcp_listen_with_backlog(s_http_pcb, 2);
    if (s_http_pcb == nullptr) {
        cyw43_arch_lwip_end();
        printf("tcp_listen_with_backlog() failed\n");
        return false;
    }

    tcp_accept(s_http_pcb, http_accept_callback);
    printf("HTTP JPEG server listening on http://%s/\n", get_board_ip_string());

    cyw43_arch_lwip_end();
    return true;
}

static bool connect_wifi(void) {
    printf("Initializing CYW43 Wi-Fi stack...\n");
    if (cyw43_arch_init_with_country(WIFI_COUNTRY_CODE)) {
        printf("cyw43_arch_init_with_country() failed\n");
        return false;
    }
    s_cyw43_ready = true;

    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);

    const int result = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        WIFI_CONNECT_TIMEOUT_MS
    );

    if (result != 0) {
        printf("Wi-Fi connection failed, result=%d\n", result);
        return false;
    }

    cyw43_arch_lwip_begin();
    printf("Wi-Fi connected, IP address: %s\n", get_board_ip_string());
    cyw43_arch_lwip_end();
    return true;
}

int main() {
    stdio_init_all();
    init_person_detect_outputs();
    mutex_init(&s_latest_frame_mutex);
    mutex_init(&s_config_mutex);
    mutex_init(&s_roi_mutex);
    mutex_init(&s_http_client_mutex);
    printf("booting imx500 person detect ROI MVP\n");

    if (!connect_wifi()) {
        while (true) {
            if (s_cyw43_ready) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                sleep_ms(150);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                sleep_ms(850);
            } else {
                sleep_ms(1000);
            }
        }
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    start_http_jpeg_server();

    if (!init_imx500_streaming()) {
        while (true) {
            service_wifi_connected_led();
            sleep_ms(5);
        }
    }

    multicore_launch_core1(metadata_worker_core1);
    while (true) {
        service_wifi_connected_led();
        sleep_ms(5);
    }
}
