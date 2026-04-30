#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>

#include "ArducamIMX500SDK.h"
#include "g_config.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "peripherals_adapter.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "wifi_config.h"

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 30000
#endif

#define BOARD_UDP_PORT 42424
#define BOARD_DISCOVERY_REQUEST "IMX500_ROI_MVP_DISCOVER"
#define BOARD_PING_REQUEST "IMX500_ROI_MVP_PING"
#define BOARD_ANNOUNCE_INTERVAL_MS 2000
#define IMX500_DEFAULT_SPI_BAUDRATE_HZ (5 * 1000 * 1000)
#define MAX_PERSON_DETECTIONS 10

static constexpr spi_data_format_t kImx500SpiMetadataFormat = SPI_METADATA_OUTPUT_TENSOR;

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
};

struct PersonDetectionResult {
    uint32_t count;
    uint16_t source_width;
    uint16_t source_height;
    PersonDetection items[MAX_PERSON_DETECTIONS];
};

static void announce_board_if_due(void);

static void wait_for_usb_serial(void) {
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
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
    for (uint32_t i = 0; i < staged.count; ++i) {
        PersonDetection &item = staged.items[i];
        if (normalized_coords) {
            item.x1 *= static_cast<float>(input_w);
            item.x2 *= static_cast<float>(input_w);
            item.y1 *= static_cast<float>(input_h);
            item.y2 *= static_cast<float>(input_h);
        }
        item.x1 = clamp_float(item.x1, 0.0f, static_cast<float>(input_w));
        item.x2 = clamp_float(item.x2, 0.0f, static_cast<float>(input_w));
        item.y1 = clamp_float(item.y1, 0.0f, static_cast<float>(input_h));
        item.y2 = clamp_float(item.y2, 0.0f, static_cast<float>(input_h));
    }

    *out = staged;
    return true;
}

static void print_person_detections(uint8_t frame_id, uint32_t payload_size, const PersonDetectionResult &result) {
    printf("\n========== frame %u ==========\n", (unsigned)frame_id);
    printf("payload=%lu bytes person_count=%lu source=%ux%u threshold=%.2f class=%d\n",
           (unsigned long)payload_size,
           (unsigned long)result.count,
           (unsigned)result.source_width,
           (unsigned)result.source_height,
           (double)PERSON_SCORE_THRESHOLD,
           (int)PERSON_CLASS_ID);

    for (uint32_t i = 0; i < result.count; ++i) {
        const PersonDetection &item = result.items[i];
        printf("person[%lu]: score=%.3f box_xyxy=(%.1f, %.1f, %.1f, %.1f)\n",
               (unsigned long)i,
               (double)item.score,
               (double)item.x1,
               (double)item.y1,
               (double)item.x2,
               (double)item.y2);
    }
}

static bool init_imx500_streaming(void) {
    i2c_master_init(100 * 1000);
    spi_master_init(IMX500_DEFAULT_SPI_BAUDRATE_HZ);
    bind_peripherals_api();

    uint32_t module_fw_ver = 0;
    get_fw_ver(&module_fw_ver);
    printf("module fw version: 0x%08lx\n", (unsigned long)module_fw_ver);

    uint32_t module_pid = 0;
    get_pid(&module_pid);
    printf("module pid: 0x%08lx\n", (unsigned long)module_pid);

    printf("Opening IMX500 with model/network_info already deployed in module flash...\n");
    if (!open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, kImx500SpiMetadataFormat, IMX500_STREAM_FPS)) {
        printf("open() failed\n");
        return false;
    }

    stream_on();
    printf("IMX500 stream_on complete\n");
    return true;
}

static void process_imx500_frames_forever(void) {
    while (true) {
        announce_board_if_due();

        const int32_t bytes_read = read_metadata(s_metadata_buf, sizeof(s_metadata_buf));
        announce_board_if_due();
        if (bytes_read <= 0) {
            printf("read_metadata failed\n");
            sleep_ms(10);
            continue;
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

static void send_udp_response(struct udp_pcb *pcb, const ip_addr_t *addr, uint16_t port, const char *response) {
    const size_t response_len = strlen(response);
    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, response_len, PBUF_RAM);
    if (reply == nullptr) {
        printf("UDP response allocation failed\n");
        return;
    }

    memcpy(reply->payload, response, response_len);
    udp_sendto(pcb, reply, addr, port);
    pbuf_free(reply);
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
    wait_for_usb_serial();
    printf("USB serial connected\n");

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

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    start_udp_control_server();

    if (!init_imx500_streaming()) {
        while (true) {
            announce_board_if_due();
            sleep_ms(1000);
        }
    }

    process_imx500_frames_forever();
}
