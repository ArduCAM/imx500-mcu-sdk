#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ArducamIMX500SDK.h"
#include "generated_imx500_models/higherhrnet_fpk.h"
#include "generated_imx500_models/higherhrnet_network_info.h"
#include "higherhrnet_postprocess.h"
#include "imx500_sdk_integration_test.h"
#include "peripherals_adapter.h"

static const char *TAG = "imx500_sdk_test";

namespace {

constexpr uint32_t kMaxFrameBufferSize = 284 * 1024;
constexpr uint32_t kBenchmarkFrameCountPreInject = 30;
constexpr uint32_t kBenchmarkFrameCountPostInject = 30;
constexpr uint32_t kInjectionDataBytes = 640 * 480 * 3;
constexpr uint32_t kHexSampleBytes = 12;

uint8_t *g_frame_buf = nullptr;
IMX500ParsedMetadata *g_parsed_metadata = nullptr;
HigherhrnetResult *g_higherhrnet_result = nullptr;

typedef struct {
    uint32_t requested_frames;
    uint32_t success_frames;
    uint32_t failed_frames;
    uint32_t parse_success_frames;
    uint32_t parse_failed_frames;
    uint32_t hrnet_success_frames;
    uint32_t jpeg_frames;
    uint64_t total_pose_count;
    uint64_t total_bytes;
    uint64_t first_frame_us;
    uint64_t last_frame_us;
    uint64_t total_read_cost_us;
    float fps;
    float avg_read_ms;
    float avg_bytes_per_frame;
    float avg_pose_count;
} FrameBenchmarkResult;

static uint64_t bench_now_us()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

static float bench_us_to_ms(uint64_t us)
{
    return static_cast<float>(us) / 1000.0f;
}

static void print_benchmark_row(const char *metric, const char *value)
{
    std::printf("| %-39s | %-23s |\n", metric, value);
}

static void print_hex_sample(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0) {
        std::printf("<empty>");
        return;
    }

    uint32_t sample_len = std::min<uint32_t>(len, kHexSampleBytes);
    for (uint32_t i = 0; i < sample_len; ++i) {
        std::printf("%02X%s", buf[i], (i + 1 < sample_len) ? " " : "");
    }
}

static void *alloc_zeroed_bytes(size_t bytes)
{
    void *ptr = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!ptr) {
        ptr = std::calloc(1, bytes);
    }
    return ptr;
}

static bool allocate_test_buffers(uint32_t reported_metadata_size)
{
    if (reported_metadata_size > kMaxFrameBufferSize) {
        ESP_LOGE(TAG, "reported metadata size=%" PRIu32 " exceeds buffer cap=%" PRIu32,
                 reported_metadata_size, kMaxFrameBufferSize);
        return false;
    }

    if (!g_frame_buf) {
        g_frame_buf = static_cast<uint8_t *>(alloc_zeroed_bytes(kMaxFrameBufferSize));
    }
    if (!g_parsed_metadata) {
        g_parsed_metadata = static_cast<IMX500ParsedMetadata *>(alloc_zeroed_bytes(sizeof(IMX500ParsedMetadata)));
    }
    if (!g_higherhrnet_result) {
        g_higherhrnet_result = static_cast<HigherhrnetResult *>(alloc_zeroed_bytes(sizeof(HigherhrnetResult)));
    }

    if (!g_frame_buf || !g_parsed_metadata || !g_higherhrnet_result) {
        ESP_LOGE(TAG, "failed to allocate test buffers: frame=%p parsed=%p hrnet=%p",
                 static_cast<void *>(g_frame_buf),
                 static_cast<void *>(g_parsed_metadata),
                 static_cast<void *>(g_higherhrnet_result));
        return false;
    }

    ESP_LOGI(TAG, "allocated test buffers: frame=%u parsed=%u hrnet=%u reported_metadata=%u",
             static_cast<unsigned>(kMaxFrameBufferSize),
             static_cast<unsigned>(sizeof(IMX500ParsedMetadata)),
             static_cast<unsigned>(sizeof(HigherhrnetResult)),
             static_cast<unsigned>(reported_metadata_size));
    return true;
}

static uint32_t provider_fill_0x55(uint8_t *buf, uint32_t max_len, uint32_t offset)
{
    (void)offset;
    std::memset(buf, 0x55, max_len);
    return max_len;
}

static void dump_spi_flash_status(const char *label)
{
    spi_flash_status_t status = {};
    if (!get_spi_flash_status(&status)) {
        std::printf("%s failed to read spi flash status\n", label);
        return;
    }
    std::printf("%s status=%lu result=%lu bytes=%lu/%lu\n",
                label,
                static_cast<unsigned long>(status.status),
                static_cast<unsigned long>(status.result),
                static_cast<unsigned long>(status.bytes_done),
                static_cast<unsigned long>(status.bytes_total));
}

static bool program_flash_assets()
{
    std::printf("Programming higherhrnet model to module flash...\n");
    if (!spi_slave_write_model_to_flash(higherhrnet_fpk_data, static_cast<uint32_t>(higherhrnet_fpk_size))) {
        dump_spi_flash_status("[MODEL FLASH]");
        return false;
    }

    std::printf("Programming higherhrnet network_info to module flash...\n");
    if (!spi_slave_write_nn_info_to_flash(higherhrnet_network_info_data,
                                          static_cast<uint32_t>(higherhrnet_network_info_size))) {
        dump_spi_flash_status("[NN INFO FLASH]");
        return false;
    }

    dump_spi_flash_status("[FLASH PROGRAM DONE]");
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

static bool open_with_selected_boot_mode()
{
    std::printf("Boot mode: %s\n", get_boot_mode_name());

#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    return open(higherhrnet_fpk_data,
                static_cast<uint32_t>(higherhrnet_fpk_size),
                higherhrnet_network_info_data,
                static_cast<uint32_t>(higherhrnet_network_info_size),
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    if (!program_flash_assets()) {
        std::printf("flash programming failed\n");
        return false;
    }
    return open(nullptr, 0, nullptr, 0,
                MIPI_DATA_IMAGE,
                SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
                10);
#else
#error "Unsupported INTEGRATION_TEST_BOOT_MODE"
#endif
}

static FrameBenchmarkResult benchmark_read_frame(uint32_t count, bool print_frame_sample)
{
    FrameBenchmarkResult result = {};
    result.requested_frames = count;

    for (uint32_t i = 0; i < count; ++i) {
        std::printf("\n=== Frame %lu/%lu ===\n",
                    static_cast<unsigned long>(i + 1),
                    static_cast<unsigned long>(count));

        uint64_t read_start_us = bench_now_us();
        int32_t bytes_read = read_metadata(g_frame_buf, kMaxFrameBufferSize);
        uint64_t read_end_us = bench_now_us();

        if (bytes_read <= 0) {
            result.failed_frames++;
            std::printf("Failed to read metadata frame\n");
            continue;
        }

        if (result.success_frames == 0) {
            result.first_frame_us = read_end_us;
        }
        result.last_frame_us = read_end_us;
        result.success_frames++;
        result.total_bytes += static_cast<uint64_t>(bytes_read);
        result.total_read_cost_us += (read_end_us - read_start_us);

        std::printf("Read %ld bytes in %.2f ms\n",
                    static_cast<long>(bytes_read),
                    bench_us_to_ms(read_end_us - read_start_us));
        if (print_frame_sample) {
            std::printf("metadata first12: ");
            print_hex_sample(g_frame_buf, static_cast<uint32_t>(bytes_read));
            std::printf("\n");
        }

        std::memset(g_parsed_metadata, 0, sizeof(*g_parsed_metadata));
        if (!parse_output_tensor_data_with_metadata(g_frame_buf, static_cast<uint32_t>(bytes_read), g_parsed_metadata)) {
            result.parse_failed_frames++;
            std::printf("Failed to parse metadata payload=%ld\n", static_cast<long>(bytes_read));
            continue;
        }

        result.parse_success_frames++;
        if (g_parsed_metadata->jpeg_data_len > 0 ||
            g_parsed_metadata->jpeg_block_end_offset > g_parsed_metadata->jpeg_block_offset) {
            result.jpeg_frames++;
        }

        std::memset(g_higherhrnet_result, 0, sizeof(*g_higherhrnet_result));
        if (higherhrnet_postprocess_from_metadata(g_parsed_metadata, g_higherhrnet_result)) {
            result.hrnet_success_frames++;
            result.total_pose_count += static_cast<uint64_t>(g_higherhrnet_result->pose_count);
        }

        std::printf("Parsed metadata: jpeg_len=%" PRIu32 " output_len=%" PRIu32 " poses=%" PRIu32 "\n",
                    g_parsed_metadata->jpeg_data_len,
                    g_parsed_metadata->output_payload_length,
                    g_higherhrnet_result->pose_count);
    }

    if (result.success_frames > 1 && result.last_frame_us > result.first_frame_us) {
        uint64_t span_us = result.last_frame_us - result.first_frame_us;
        result.fps = (static_cast<float>(result.success_frames - 1) * 1000000.0f) / static_cast<float>(span_us);
    }
    if (result.success_frames > 0) {
        result.avg_read_ms = bench_us_to_ms(result.total_read_cost_us) / static_cast<float>(result.success_frames);
        result.avg_bytes_per_frame = static_cast<float>(result.total_bytes) / static_cast<float>(result.success_frames);
    }
    if (result.hrnet_success_frames > 0) {
        result.avg_pose_count = static_cast<float>(result.total_pose_count) /
                                static_cast<float>(result.hrnet_success_frames);
    }

    return result;
}

static void print_benchmark_table(uint64_t open_cost_us,
                                  uint64_t stream_on_cost_us,
                                  uint64_t startup_total_us,
                                  uint64_t first_frame_latency_us,
                                  const FrameBenchmarkResult *pre_inject,
                                  const FrameBenchmarkResult *post_inject)
{
    char value[64] = {};

    std::printf("\n================ IMX500 SPI Metadata Benchmark Summary ================\n");
    std::printf("+-----------------------------------------+-------------------------+\n");
    std::printf("| Metric                                  | Value                   |\n");
    std::printf("+-----------------------------------------+-------------------------+\n");

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(open_cost_us));
    print_benchmark_row("open() duration", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(stream_on_cost_us));
    print_benchmark_row("stream_on() duration", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(first_frame_latency_us));
    print_benchmark_row("stream_on -> first frame", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(startup_total_us));
    print_benchmark_row("open -> first frame (startup)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(pre_inject->success_frames),
                  static_cast<unsigned long>(pre_inject->requested_frames));
    print_benchmark_row("frames before injection", value);

    std::snprintf(value, sizeof(value), "%.2f fps", pre_inject->fps);
    print_benchmark_row("FPS before injection", value);

    std::snprintf(value, sizeof(value), "%.2f bytes", pre_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (before)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(pre_inject->parse_success_frames),
                  static_cast<unsigned long>(pre_inject->success_frames));
    print_benchmark_row("parsed metadata frames (before)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(pre_inject->hrnet_success_frames),
                  static_cast<unsigned long>(pre_inject->parse_success_frames));
    print_benchmark_row("higherhrnet frames (before)", value);

    std::snprintf(value, sizeof(value), "%.2f", pre_inject->avg_pose_count);
    print_benchmark_row("avg poses/frame (before)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(post_inject->success_frames),
                  static_cast<unsigned long>(post_inject->requested_frames));
    print_benchmark_row("frames after injection", value);

    std::snprintf(value, sizeof(value), "%.2f fps", post_inject->fps);
    print_benchmark_row("FPS after injection", value);

    std::snprintf(value, sizeof(value), "%.2f bytes", post_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (after)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(post_inject->parse_success_frames),
                  static_cast<unsigned long>(post_inject->success_frames));
    print_benchmark_row("parsed metadata frames (after)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  static_cast<unsigned long>(post_inject->hrnet_success_frames),
                  static_cast<unsigned long>(post_inject->parse_success_frames));
    print_benchmark_row("higherhrnet frames (after)", value);

    std::snprintf(value, sizeof(value), "%.2f", post_inject->avg_pose_count);
    print_benchmark_row("avg poses/frame (after)", value);

    std::printf("+-----------------------------------------+-------------------------+\n");
}

} // namespace

extern "C" esp_err_t imx500_sdk_integration_test_run(void)
{
    ESP_RETURN_ON_FALSE(bind_peripherals_api(), ESP_FAIL, TAG, "failed to bind SDK peripherals");

    uint32_t module_fw_ver = 0;
    uint32_t module_pid = 0;
    get_fw_ver(&module_fw_ver);
    get_pid(&module_pid);
    ESP_LOGI(TAG, "module fw version: 0x%" PRIx32, module_fw_ver);
    ESP_LOGI(TAG, "module pid: 0x%" PRIx32, module_pid);

    uint64_t open_start_us = bench_now_us();
    ESP_RETURN_ON_FALSE(open_with_selected_boot_mode(), ESP_FAIL, TAG, "open() failed");
    uint64_t open_end_us = bench_now_us();

    uint32_t reported_metadata_size = get_metadata_size();
    ESP_RETURN_ON_FALSE(allocate_test_buffers(reported_metadata_size), ESP_FAIL, TAG,
                        "failed to allocate benchmark buffers");

    uint64_t stream_on_start_us = bench_now_us();
    stream_on();
    uint64_t stream_on_end_us = bench_now_us();

    FrameBenchmarkResult pre_inject = benchmark_read_frame(kBenchmarkFrameCountPreInject, true);

    uint64_t first_frame_latency_us = 0;
    uint64_t startup_total_us = 0;
    if (pre_inject.success_frames > 0) {
        if (pre_inject.first_frame_us >= stream_on_end_us) {
            first_frame_latency_us = pre_inject.first_frame_us - stream_on_end_us;
        }
        if (pre_inject.first_frame_us >= open_start_us) {
            startup_total_us = pre_inject.first_frame_us - open_start_us;
        }
    }

    ESP_LOGI(TAG, "switching SPI forwarding path for synthetic data injection");
    ESP_RETURN_ON_FALSE(switch_spi_data_forward_mode(SPI_SLAVE_TO_IMX500_SSPI), ESP_FAIL, TAG,
                        "failed to switch SPI forwarding to injection mode");
    do_data_injection_stream(provider_fill_0x55, kInjectionDataBytes, true);
    stop_data_injection();
    ESP_RETURN_ON_FALSE(switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_SSPI), ESP_FAIL, TAG,
                        "failed to restore SPI metadata forwarding mode");

    FrameBenchmarkResult post_inject = benchmark_read_frame(kBenchmarkFrameCountPostInject, false);

    print_benchmark_table(open_end_us - open_start_us,
                          stream_on_end_us - stream_on_start_us,
                          startup_total_us,
                          first_frame_latency_us,
                          &pre_inject,
                          &post_inject);
    return ESP_OK;
}
