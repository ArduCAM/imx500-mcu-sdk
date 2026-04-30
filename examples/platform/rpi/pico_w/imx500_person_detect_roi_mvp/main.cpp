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
#include "lwip/udp.h"
#include "peripherals_adapter.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "wifi_config.h"

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 30000
#endif

#define BOARD_UDP_PORT 42424
#define BOARD_DISCOVERY_REQUEST "IMX500_ROI_MVP_DISCOVER"
#define BOARD_PING_REQUEST "IMX500_ROI_MVP_PING"
#define BOARD_FRAME_REQUEST "IMX500_ROI_MVP_GET_FRAME"
#define BOARD_SET_ROI_REQUEST "IMX500_ROI_MVP_SET_ROI"
#define BOARD_ANNOUNCE_INTERVAL_MS 2000
#define BACKEND_UDP_CHUNK_BYTES 1024
#define BACKEND_JSON_BYTES 3072
#define BACKEND_HEADER_BYTES 160
#define BACKEND_UDP_PACKET_BYTES (BACKEND_HEADER_BYTES + BACKEND_JSON_BYTES)
#define IMX500_DEFAULT_SPI_BAUDRATE_HZ (5 * 1000 * 1000)
#define MAX_PERSON_DETECTIONS 10

static constexpr spi_data_format_t kImx500SpiMetadataFormat = IMX500_SPI_METADATA_FORMAT;

static bool s_cyw43_ready = false;
static struct udp_pcb *s_udp_pcb = nullptr;
static uint32_t s_last_announce_ms = 0;
static uint32_t s_metadata_parse_failures = 0;
static uint8_t s_metadata_buf[METADATA_BUFFER_SIZE];
static IMX500ParsedMetadata s_parsed_metadata;

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

struct FrameBackendRequest {
    bool pending;
    ip_addr_t addr;
    uint16_t port;
    uint32_t request_id;
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

static critical_section_t s_backend_lock;
static mutex_t s_latest_frame_mutex;
static mutex_t s_roi_mutex;
static FrameBackendRequest s_frame_request = {};
static LatestBackendFrame s_latest_frame = {};
static RoiConfig s_roi_config = {};
static uint8_t s_udp_frame_packet[BACKEND_UDP_PACKET_BYTES];
static uint32_t s_next_request_id = 1;
static volatile bool s_metadata_started = false;

static void announce_board_if_due(void);
static void send_frame_response_if_pending(void);

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
    PersonDetectionResult staged = {};
    staged.source_height = input_h;
    staged.source_width = input_w;

    for (uint32_t i = 0; i < valid_count && staged.count < MAX_PERSON_DETECTIONS; ++i) {
        const float score = tensor_read_value_raw_index(scores_tensor, i);
        if (score < PERSON_SCORE_THRESHOLD) {
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
    printf("\n========== frame %u ==========\n", (unsigned)frame_id);
    printf("payload=%lu bytes person_count=%lu source=%ux%u threshold=%.2f class=%d coords=relative\n",
           (unsigned long)payload_size,
           (unsigned long)result.count,
           (unsigned)result.source_width,
           (unsigned)result.source_height,
           (double)PERSON_SCORE_THRESHOLD,
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
}

static void store_latest_frame(uint32_t payload_size,
                               const IMX500ParsedMetadata &parsed,
                               const PersonDetectionResult &detections) {
    uint32_t jpeg_len = parsed.jpeg_data_len;
    if (parsed.jpeg_data == nullptr) {
        jpeg_len = 0;
    }
    jpeg_len = std::min<uint32_t>(jpeg_len, METADATA_BUFFER_SIZE);

    mutex_enter_blocking(&s_latest_frame_mutex);
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
    if (!open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, kImx500SpiMetadataFormat, IMX500_STREAM_FPS)) {
        printf("open() failed\n");
        return false;
    }

    stream_on();
    printf("IMX500 stream_on complete\n");
    return true;
}

static void metadata_worker_core1(void) {
    printf("metadata worker started on core1\n");
    while (true) {
        const int32_t bytes_read = read_metadata(s_metadata_buf, sizeof(s_metadata_buf));
        if (bytes_read <= 0) {
            printf("read_metadata failed\n");
            sleep_ms(10);
            continue;
        }
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
            printf("metadata parse failed payload=%ld\n", (long)bytes_read);
            if (s_metadata_parse_failures <= METADATA_DEBUG_FRAMES ||
                (s_metadata_parse_failures % 30u) == 0u) {
                dump_metadata_parse_debug(
                    s_metadata_buf,
                    static_cast<uint32_t>(bytes_read),
                    "parse_metadata failed"
                );
            }
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
        }
    }
}

static const char *get_board_ip_string(void) {
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
}

static bool payload_matches(struct pbuf *p, const char *expected) {
    char request[64] = {0};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';
    return strcmp(request, expected) == 0;
}

static bool payload_starts_with(struct pbuf *p, const char *prefix) {
    char request[80] = {0};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';
    return strncmp(request, prefix, strlen(prefix)) == 0;
}

static uint32_t parse_frame_request_id(struct pbuf *p) {
    char request[96] = {0};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';

    const char *cursor = strstr(request, "id=");
    if (cursor != nullptr) {
        const uint32_t parsed = static_cast<uint32_t>(strtoul(cursor + 3, nullptr, 10));
        if (parsed != 0) {
            return parsed;
        }
    }

    cursor = request + strlen(BOARD_FRAME_REQUEST);
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    const uint32_t parsed = static_cast<uint32_t>(strtoul(cursor, nullptr, 10));
    return parsed != 0 ? parsed : s_next_request_id++;
}

static uint32_t parse_request_id(struct pbuf *p) {
    char request[96] = {0};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';

    const char *cursor = strstr(request, "id=");
    if (cursor == nullptr) {
        return 0;
    }

    const uint32_t parsed = static_cast<uint32_t>(strtoul(cursor + 3, nullptr, 10));
    return parsed;
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

static bool parse_roi_request(struct pbuf *p, RelativePoint *points) {
    if (points == nullptr) {
        return false;
    }

    char request[220] = {0};
    const uint16_t copy_len = (p->tot_len < sizeof(request) - 1) ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';

    const char *cursor = strstr(request, "points=");
    if (cursor == nullptr) {
        return false;
    }
    cursor += strlen("points=");

    for (uint32_t i = 0; i < 4; ++i) {
        char *end = nullptr;
        const float x = strtof(cursor, &end);
        if (end == cursor || *end != ',') {
            return false;
        }
        cursor = end + 1;

        const float y = strtof(cursor, &end);
        if (end == cursor) {
            return false;
        }
        points[i].x = clamp01(x);
        points[i].y = clamp01(y);
        cursor = end;

        if (i < 3) {
            if (*cursor != ';') {
                return false;
            }
            ++cursor;
        }
    }

    return true;
}

static void build_status_response(char *response, size_t response_size) {
    snprintf(
        response,
        response_size,
        "IMX500_ROI_MVP_OK ip=%s port=%u uptime_ms=%lu",
        get_board_ip_string(),
        (unsigned int)BOARD_UDP_PORT,
        (unsigned long)to_ms_since_boot(get_absolute_time())
    );
}

static void send_udp_packet(struct udp_pcb *pcb, const ip_addr_t *addr, uint16_t port, const void *data, size_t data_len) {
    if (pcb == nullptr || addr == nullptr || data == nullptr || data_len == 0) {
        return;
    }

    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, data_len, PBUF_RAM);
    if (reply == nullptr) {
        printf("UDP response allocation failed\n");
        return;
    }

    memcpy(reply->payload, data, data_len);
    udp_sendto(pcb, reply, addr, port);
    pbuf_free(reply);
}

static void send_udp_response(struct udp_pcb *pcb, const ip_addr_t *addr, uint16_t port, const char *response) {
    send_udp_packet(pcb, addr, port, response, strlen(response));
}

static void queue_frame_request(const ip_addr_t *addr, uint16_t port, uint32_t request_id) {
    critical_section_enter_blocking(&s_backend_lock);
    s_frame_request.pending = true;
    s_frame_request.addr = *addr;
    s_frame_request.port = port;
    s_frame_request.request_id = request_id;
    critical_section_exit(&s_backend_lock);
}

static bool take_frame_request(FrameBackendRequest *request) {
    bool has_request = false;
    critical_section_enter_blocking(&s_backend_lock);
    if (s_frame_request.pending) {
        *request = s_frame_request;
        s_frame_request.pending = false;
        has_request = true;
    }
    critical_section_exit(&s_backend_lock);
    return has_request;
}

static size_t append_json(char *buffer, size_t buffer_size, size_t used, const char *format, ...) {
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

static size_t build_frame_json(char *buffer,
                               size_t buffer_size,
                               uint32_t request_id,
                               const LatestBackendFrame &frame,
                               const PersonDetectionResult &detections) {
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const RoiConfig roi = copy_roi_config();
    size_t used = 0;
    used = append_json(
        buffer,
        buffer_size,
        used,
        "{\"type\":\"frame\",\"id\":%lu,\"frame\":%u,\"uptime_ms\":%lu,"
        "\"payload_bytes\":%lu,\"mode\":%u,\"image\":{\"format\":\"jpeg\",\"bytes\":%lu},"
        "\"roi\":{\"configured\":%s,\"points\":[[%.6f,%.6f],[%.6f,%.6f],[%.6f,%.6f],[%.6f,%.6f]]},"
        "\"detections\":[",
        (unsigned long)request_id,
        (unsigned)frame.frame_id,
        (unsigned long)now_ms,
        (unsigned long)frame.payload_size,
        (unsigned)kImx500SpiMetadataFormat,
        (unsigned long)frame.jpeg_len,
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
        used = append_json(
            buffer,
            buffer_size,
            used,
            "%s{\"class_id\":%d,\"label\":\"person\",\"score\":%.4f,\"box\":[%.6f,%.6f,%.6f,%.6f],\"roi_intersects\":%s}",
            (i == 0) ? "" : ",",
            item.class_id,
            (double)item.score,
            (double)item.x1,
            (double)item.y1,
            (double)item.x2,
            (double)item.y2,
            item.roi_intersects ? "true" : "false"
        );
    }

    used = append_json(buffer, buffer_size, used, "]}");
    return used;
}

static bool latest_frame_available(void) {
    bool available = false;
    mutex_enter_blocking(&s_latest_frame_mutex);
    available = s_latest_frame.valid;
    mutex_exit(&s_latest_frame_mutex);
    return available;
}

static void send_frame_response_if_pending(void) {
    if (!latest_frame_available()) {
        return;
    }

    FrameBackendRequest request = {};
    if (!take_frame_request(&request)) {
        return;
    }

    mutex_enter_blocking(&s_latest_frame_mutex);

    char json[BACKEND_JSON_BYTES] = {};
    const size_t json_len = build_frame_json(
        json,
        sizeof(json),
        request.request_id,
        s_latest_frame,
        s_latest_frame.detections
    );

    char header[BACKEND_HEADER_BYTES] = {};
    const uint32_t jpeg_len = s_latest_frame.jpeg_len;
    const uint32_t detection_count = s_latest_frame.detections.count;
    const uint32_t chunk_count = (jpeg_len + BACKEND_UDP_CHUNK_BYTES - 1u) / BACKEND_UDP_CHUNK_BYTES;

    cyw43_arch_lwip_begin();
    int header_len = snprintf(
        header,
        sizeof(header),
        "IMX500_ROI_MVP_FRAME_JSON id=%lu jpeg_len=%lu chunks=%lu json_len=%lu\n",
        (unsigned long)request.request_id,
        (unsigned long)jpeg_len,
        (unsigned long)chunk_count,
        (unsigned long)json_len
    );
    if (header_len > 0 && static_cast<size_t>(header_len) + json_len <= sizeof(header) + sizeof(json)) {
        memcpy(s_udp_frame_packet, header, static_cast<size_t>(header_len));
        memcpy(s_udp_frame_packet + header_len, json, json_len);
        send_udp_packet(s_udp_pcb, &request.addr, request.port, s_udp_frame_packet, static_cast<size_t>(header_len) + json_len);
    }

    for (uint32_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        const uint32_t offset = chunk_index * BACKEND_UDP_CHUNK_BYTES;
        const uint32_t chunk_len = std::min<uint32_t>(BACKEND_UDP_CHUNK_BYTES, jpeg_len - offset);
        header_len = snprintf(
            header,
            sizeof(header),
            "IMX500_ROI_MVP_FRAME_JPEG id=%lu index=%lu chunks=%lu offset=%lu len=%lu\n",
            (unsigned long)request.request_id,
            (unsigned long)chunk_index,
            (unsigned long)chunk_count,
            (unsigned long)offset,
            (unsigned long)chunk_len
        );
        if (header_len <= 0 || static_cast<size_t>(header_len) + chunk_len > sizeof(header) + BACKEND_UDP_CHUNK_BYTES) {
            continue;
        }

        memcpy(s_udp_frame_packet, header, static_cast<size_t>(header_len));
        memcpy(s_udp_frame_packet + header_len, s_latest_frame.jpeg + offset, chunk_len);
        send_udp_packet(s_udp_pcb, &request.addr, request.port, s_udp_frame_packet, static_cast<size_t>(header_len) + chunk_len);
    }

    header_len = snprintf(
        header,
        sizeof(header),
        "IMX500_ROI_MVP_FRAME_END id=%lu\n",
        (unsigned long)request.request_id
    );
    if (header_len > 0) {
        send_udp_packet(s_udp_pcb, &request.addr, request.port, header, static_cast<size_t>(header_len));
    }
    cyw43_arch_lwip_end();
    mutex_exit(&s_latest_frame_mutex);

    printf("backend frame response id=%lu detections=%lu jpeg=%lu chunks=%lu\n",
           (unsigned long)request.request_id,
           (unsigned long)detection_count,
           (unsigned long)jpeg_len,
           (unsigned long)chunk_count);
}

static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, uint16_t port) {
    (void)arg;

    if (p == nullptr) {
        return;
    }

    if (payload_matches(p, BOARD_DISCOVERY_REQUEST) || payload_matches(p, BOARD_PING_REQUEST)) {
        char response[128];
        build_status_response(response, sizeof(response));
        send_udp_response(pcb, addr, port, response);
    } else if (payload_starts_with(p, BOARD_FRAME_REQUEST)) {
        const uint32_t request_id = parse_frame_request_id(p);
        queue_frame_request(addr, port, request_id);
        char response[96];
        snprintf(response, sizeof(response), "IMX500_ROI_MVP_FRAME_PENDING id=%lu", (unsigned long)request_id);
        send_udp_response(pcb, addr, port, response);
    } else if (payload_starts_with(p, BOARD_SET_ROI_REQUEST)) {
        const uint32_t request_id = parse_request_id(p);
        RelativePoint points[4] = {};
        char response[128];
        if (parse_roi_request(p, points)) {
            set_roi_config(points);
            snprintf(response, sizeof(response), "IMX500_ROI_MVP_SET_ROI_OK id=%lu", (unsigned long)request_id);
            printf("roi configured: (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n",
                   (double)points[0].x,
                   (double)points[0].y,
                   (double)points[1].x,
                   (double)points[1].y,
                   (double)points[2].x,
                   (double)points[2].y,
                   (double)points[3].x,
                   (double)points[3].y);
        } else {
            snprintf(response, sizeof(response), "IMX500_ROI_MVP_SET_ROI_ERR id=%lu", (unsigned long)request_id);
        }
        for (uint32_t i = 0; i < 3; ++i) {
            send_udp_response(pcb, addr, port, response);
        }
    }

    pbuf_free(p);
}

static bool start_udp_control_server(void) {
    cyw43_arch_lwip_begin();

    s_udp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (s_udp_pcb == nullptr) {
        cyw43_arch_lwip_end();
        printf("udp_new_ip_type() failed\n");
        return false;
    }
    ip_set_option(s_udp_pcb, SOF_BROADCAST);

    const err_t bind_err = udp_bind(s_udp_pcb, IP_ADDR_ANY, BOARD_UDP_PORT);
    if (bind_err != ERR_OK) {
        udp_remove(s_udp_pcb);
        s_udp_pcb = nullptr;
        cyw43_arch_lwip_end();
        printf("udp_bind() failed, err=%d\n", bind_err);
        return false;
    }

    udp_recv(s_udp_pcb, udp_recv_callback, nullptr);
    printf("UDP control server listening on %s:%u\n", get_board_ip_string(), (unsigned int)BOARD_UDP_PORT);

    cyw43_arch_lwip_end();
    return true;
}

static bool get_subnet_broadcast_addr(ip_addr_t *addr) {
    if (addr == nullptr) {
        return false;
    }

    struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
    const ip4_addr_t *ip = netif_ip4_addr(netif);
    const ip4_addr_t *netmask = netif_ip4_netmask(netif);
    if (ip4_addr_isany_val(*ip) || ip4_addr_isany_val(*netmask)) {
        return false;
    }

    ip4_addr_t broadcast_ip4;
    ip4_addr_set_u32(&broadcast_ip4, ip4_addr_get_u32(ip) | ~ip4_addr_get_u32(netmask));
    IP_ADDR4(addr,
             ip4_addr1(&broadcast_ip4),
             ip4_addr2(&broadcast_ip4),
             ip4_addr3(&broadcast_ip4),
             ip4_addr4(&broadcast_ip4));
    return true;
}

static void announce_board_if_due(void) {
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (s_udp_pcb == nullptr || now_ms - s_last_announce_ms < BOARD_ANNOUNCE_INTERVAL_MS) {
        return;
    }
    s_last_announce_ms = now_ms;

    cyw43_arch_lwip_begin();
    char response[128];
    ip_addr_t broadcast_addr;
    IP_ADDR4(&broadcast_addr, 255, 255, 255, 255);
    build_status_response(response, sizeof(response));
    send_udp_response(s_udp_pcb, &broadcast_addr, BOARD_UDP_PORT, response);

    ip_addr_t subnet_broadcast_addr;
    if (get_subnet_broadcast_addr(&subnet_broadcast_addr) &&
        !ip_addr_cmp(&broadcast_addr, &subnet_broadcast_addr)) {
        send_udp_response(s_udp_pcb, &subnet_broadcast_addr, BOARD_UDP_PORT, response);
    }
    cyw43_arch_lwip_end();
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
    critical_section_init(&s_backend_lock);
    mutex_init(&s_latest_frame_mutex);
    mutex_init(&s_roi_mutex);
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
    start_udp_control_server();

    if (!init_imx500_streaming()) {
        while (true) {
            service_wifi_connected_led();
            announce_board_if_due();
            sleep_ms(5);
        }
    }

    multicore_launch_core1(metadata_worker_core1);
    while (true) {
        service_wifi_connected_led();
        announce_board_if_due();
        send_frame_response_if_pending();
        sleep_ms(5);
    }
}
