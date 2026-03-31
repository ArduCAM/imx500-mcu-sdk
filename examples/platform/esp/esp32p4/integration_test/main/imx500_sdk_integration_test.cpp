#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ArducamIMX500SDK.h"
#include "imx500_sdk_integration_test.h"
#include "peripherals_adapter.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_NoID.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_network_info.h"

#define NN_FW_DATA InputTensorOnly_NoID_data
#define NN_FW_SIZE InputTensorOnly_NoID_size
#define NN_NETWORK_INFO_DATA InputTensorOnly_network_info_data
#define NN_NETWORK_INFO_SIZE InputTensorOnly_network_info_size

static const char *TAG = "imx500_sdk_test";

namespace {

constexpr uint32_t kMaxFrameSize = CONFIG_EXAMPLE_IMX500_SDK_MAX_FRAME_SIZE;
uint8_t g_frame_buf[kMaxFrameSize];

typedef struct {
    uint32_t requested_frames;
    uint32_t success_frames;
    uint32_t failed_frames;
    uint64_t total_bytes;
    uint64_t first_frame_us;
    uint64_t last_frame_us;
    uint64_t total_read_cost_us;
    float fps;
    float avg_read_ms;
    float avg_bytes_per_frame;
} FrameBenchmarkResult;

static float bench_us_to_ms(uint64_t us)
{
    return static_cast<float>(us) / 1000.0f;
}

static uint64_t now_us()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

static void print_benchmark_row(const char *metric, const char *value)
{
    std::printf("| %-39s | %-23s |\n", metric, value);
}

static void print_hex_sample(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        std::printf("%02X ", buf[i]);
    }
}

static void print_benchmark_table(uint64_t open_cost_us, uint64_t stream_on_cost_us,
                                  uint64_t startup_total_us, uint64_t first_frame_latency_us,
                                  const FrameBenchmarkResult *pre_inject,
                                  const FrameBenchmarkResult *post_inject)
{
    char value[64];

    std::printf("\n================ IMX500 Benchmark Summary ================\n");
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
                  static_cast<unsigned long>(post_inject->success_frames),
                  static_cast<unsigned long>(post_inject->requested_frames));
    print_benchmark_row("frames after injection", value);

    std::snprintf(value, sizeof(value), "%.2f fps", post_inject->fps);
    print_benchmark_row("FPS after injection", value);

    std::snprintf(value, sizeof(value), "%.2f bytes", post_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (after)", value);

    std::printf("+-----------------------------------------+-------------------------+\n");
}

static uint32_t provider_fill_0x55(uint8_t *buf, uint32_t max_len, uint32_t offset)
{
    (void)offset;
    std::memset(buf, 0x55, max_len);
    return max_len;
}

static FrameBenchmarkResult benchmark_read_frame(uint32_t count, uint8_t *buf,
                                                 uint32_t max_buf_size, bool print_frame_sample)
{
    FrameBenchmarkResult result = {};
    result.requested_frames = count;

    for (uint32_t i = 0; i < count; ++i) {
        std::printf("\n=== Frame %lu/%lu ===\n",
                    static_cast<unsigned long>(i + 1),
                    static_cast<unsigned long>(count));
        uint64_t read_start_us = now_us();
        int32_t bytes_read = read_metadata(buf, max_buf_size);
        uint64_t read_end_us = now_us();

        if (bytes_read > 0) {
            if (result.success_frames == 0) {
                result.first_frame_us = read_end_us;
            }
            result.last_frame_us = read_end_us;
            result.success_frames++;
            result.total_bytes += static_cast<uint64_t>(bytes_read);
            result.total_read_cost_us += (read_end_us - read_start_us);
            std::printf("Successfully read %ld bytes, read cost=%.2f ms\n",
                        static_cast<long>(bytes_read),
                        bench_us_to_ms(read_end_us - read_start_us));
            if (print_frame_sample) {
                print_hex_sample(buf, 12);
                std::printf("\n");
            }
        } else {
            result.failed_frames++;
            std::printf("Failed to read frame\n");
        }
    }

    if (result.success_frames > 1 && result.last_frame_us > result.first_frame_us) {
        uint64_t span_us = result.last_frame_us - result.first_frame_us;
        result.fps = (static_cast<float>(result.success_frames - 1) * 1000000.0f) /
                     static_cast<float>(span_us);
    }
    if (result.success_frames > 0) {
        result.avg_read_ms = bench_us_to_ms(result.total_read_cost_us) /
                             static_cast<float>(result.success_frames);
        result.avg_bytes_per_frame = static_cast<float>(result.total_bytes) /
                                     static_cast<float>(result.success_frames);
    }

    return result;
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
    std::printf("Programming model to module flash...\n");
    if (!spi_slave_write_model_to_flash(NN_FW_DATA, NN_FW_SIZE)) {
        dump_spi_flash_status("[MODEL FLASH]");
        return false;
    }

    std::printf("Programming network_info to module flash...\n");
    if (!spi_slave_write_nn_info_to_flash(NN_NETWORK_INFO_DATA, NN_NETWORK_INFO_SIZE)) {
        dump_spi_flash_status("[NN INFO FLASH]");
        return false;
    }

    dump_spi_flash_status("[FLASH PROGRAM DONE]");
    return true;
}

static const char *get_boot_mode_name()
{
#if CONFIG_EXAMPLE_IMX500_SDK_BOOT_MODE_FLASH
    return "FLASH_BOOT";
#else
    return "DIRECT_BOOT";
#endif
}

static bool reset_sensor_before_open()
{
    int ret = sensor_i2c_write_16_8(0x0103, 0x01);
    if (ret < 0) {
        ESP_LOGE(TAG, "sensor reset failed, ret=%d", ret);
        return false;
    }

    ESP_LOGI(TAG, "sensor reset issued, waiting 5 seconds for startup");
    vTaskDelay(pdMS_TO_TICKS(5000));
    return true;
}

static bool open_with_selected_boot_mode()
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    // std::printf("Boot mode: %s\n", get_boot_mode_name());
    // if (!reset_sensor_before_open()) {
    //     return false;
    // }
#if CONFIG_EXAMPLE_IMX500_SDK_BOOT_MODE_FLASH
    if (!program_flash_assets()) {
        std::printf("flash programming failed\n");
        return false;
    }
    return open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, SPI_METADATA_OUTPUT_TENSOR, 10);
#else
    return open(NN_FW_DATA, NN_FW_SIZE,
                NN_NETWORK_INFO_DATA, NN_NETWORK_INFO_SIZE,
                MIPI_DATA_IMAGE, SPI_METADATA_OUTPUT_TENSOR, 10);
#endif
}

} // namespace

esp_err_t imx500_sdk_integration_test_run(void)
{
    if (!bind_peripherals_api()) {
        ESP_LOGE(TAG, "failed to bind SDK peripheral adapters");
        return ESP_FAIL;
    }

    uint32_t module_fw_ver = 0;
    get_fw_ver(&module_fw_ver);
    ESP_LOGI(TAG, "module fw version: 0x%" PRIx32, module_fw_ver);

    uint32_t module_pid = 0;
    get_pid(&module_pid);
    ESP_LOGI(TAG, "module pid: 0x%" PRIx32, module_pid);

    uint64_t open_start_us = now_us();
    bool open_ret = open_with_selected_boot_mode();
    uint64_t open_end_us = now_us();
    if (!open_ret) {
        ESP_LOGE(TAG, "open() failed");
        return ESP_FAIL;
    }

    uint64_t stream_on_start_us = now_us();
    stream_on();
    uint64_t stream_on_end_us = now_us();

    FrameBenchmarkResult pre_inject_bench = benchmark_read_frame(
        CONFIG_EXAMPLE_IMX500_SDK_PRE_INJECT_FRAMES,
        g_frame_buf,
        sizeof(g_frame_buf),
        true);

    uint64_t first_frame_latency_us = 0;
    uint64_t startup_total_us = 0;
    if (pre_inject_bench.success_frames > 0) {
        if (pre_inject_bench.first_frame_us >= stream_on_end_us) {
            first_frame_latency_us = pre_inject_bench.first_frame_us - stream_on_end_us;
        }
        if (pre_inject_bench.first_frame_us >= open_start_us) {
            startup_total_us = pre_inject_bench.first_frame_us - open_start_us;
        }
    }

    switch_spi_data_forward_mode(SPI_SLAVE_TO_IMX500_SSPI);
    do_data_injection_stream(provider_fill_0x55, 640U * 480U * 3U, true);
    stop_data_injection();
    switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_MSPI);

    FrameBenchmarkResult post_inject_bench = benchmark_read_frame(
        CONFIG_EXAMPLE_IMX500_SDK_POST_INJECT_FRAMES,
        g_frame_buf,
        sizeof(g_frame_buf),
        false);

    print_benchmark_table(open_end_us - open_start_us,
                          stream_on_end_us - stream_on_start_us,
                          startup_total_us,
                          first_frame_latency_us,
                          &pre_inject_bench,
                          &post_inject_bench);
    return ESP_OK;
}
