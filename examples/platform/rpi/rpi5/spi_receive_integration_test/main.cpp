#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ArducamIMX500SDK.h"
#include "g_config.h"
#include "log.h"
#include "peripherals_adapter.h"

#define NN_FW_DATA              nullptr
#define NN_FW_SIZE              0
#define NN_NETOWRK_INFO_DATA    nullptr
#define NN_NETOWRK_INFO_SIZE    0
#define MAX_FRAME_SIZE          (1024 * 264)
#define BENCHMARK_FRAME_COUNT_PRE_INJECT   30
#define BENCHMARK_FRAME_COUNT_POST_INJECT  30

#define INTEGRATION_TEST_BOOT_MODE_DIRECT 0
#define INTEGRATION_TEST_BOOT_MODE_FLASH  1

#ifndef INTEGRATION_TEST_BOOT_MODE
#define INTEGRATION_TEST_BOOT_MODE INTEGRATION_TEST_BOOT_MODE_FLASH
#endif

#ifndef INTEGRATION_TEST_DUMP_FRAME_COUNT
#define INTEGRATION_TEST_DUMP_FRAME_COUNT 2
#endif

static uint8_t frame_buf[MAX_FRAME_SIZE];

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

static uint64_t time_us_now()
{
    using clock = std::chrono::steady_clock;
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
        .count();
}

static float bench_us_to_ms(uint64_t us)
{
    return (float)us / 1000.0f;
}

static void print_benchmark_row(const char *metric, const char *value)
{
    std::printf("| %-39s | %-23s |\n", metric, value);
}

static void print_frame_hex_dump(const uint8_t *buf, uint32_t len, uint32_t frame_index)
{
    if (!buf || len == 0) {
        std::printf("[FRAME %lu DUMP] empty frame\n", (unsigned long)frame_index);
        return;
    }

    std::printf("\n========== FRAME %lu FULL DATA DUMP (%lu bytes) ==========\n",
                (unsigned long)frame_index,
                (unsigned long)len);
    for (uint32_t offset = 0; offset < len; offset += 16) {
        uint32_t line_len = len - offset;
        if (line_len > 16) {
            line_len = 16;
        }

        std::printf("%08lx:", (unsigned long)offset);
        for (uint32_t i = 0; i < line_len; ++i) {
            std::printf(" %02x", (unsigned)buf[offset + i]);
        }
        std::printf("\n");
    }
    std::printf("========== FRAME %lu FULL DATA DUMP END ==========\n\n",
                (unsigned long)frame_index);
}

static void print_benchmark_table(uint64_t open_cost_us,
                                  uint64_t stream_on_cost_us,
                                  uint64_t startup_total_us,
                                  uint64_t first_frame_latency_us,
                                  const FrameBenchmarkResult *pre_inject,
                                  const FrameBenchmarkResult *post_inject)
{
    char value[64];

    std::printf("\n================ IMX500 RPi5 Benchmark Summary ================\n");
    std::printf("+-----------------------------------------+-------------------------+\n");
    std::printf("| Metric                                  | Value                   |\n");
    std::printf("+-----------------------------------------+-------------------------+\n");

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(open_cost_us));
    print_benchmark_row("imx500_open() duration", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(stream_on_cost_us));
    print_benchmark_row("stream_on() duration", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(first_frame_latency_us));
    print_benchmark_row("stream_on -> first frame", value);

    std::snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(startup_total_us));
    print_benchmark_row("imx500_open -> first frame (startup)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  (unsigned long)pre_inject->success_frames,
                  (unsigned long)pre_inject->requested_frames);
    print_benchmark_row("frames before injection", value);

    std::snprintf(value, sizeof(value), "%.2f fps", pre_inject->fps);
    print_benchmark_row("FPS before injection", value);

    std::snprintf(value, sizeof(value), "%.2f bytes", pre_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (before)", value);

    std::snprintf(value, sizeof(value), "%lu/%lu",
                  (unsigned long)post_inject->success_frames,
                  (unsigned long)post_inject->requested_frames);
    print_benchmark_row("frames after injection", value);

    std::snprintf(value, sizeof(value), "%.2f fps", post_inject->fps);
    print_benchmark_row("FPS after injection", value);

    std::snprintf(value, sizeof(value), "%.2f bytes", post_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (after)", value);

    std::printf("+-----------------------------------------+-------------------------+\n");
}

uint32_t provider_fill_0x55(uint8_t *buf, uint32_t max_len, uint32_t offset)
{
    (void)offset;
    std::memset(buf, 0x55, max_len);
    return max_len;
}

static FrameBenchmarkResult benchmark_read_frame(uint32_t count,
                                                 uint8_t *buf,
                                                 uint32_t max_buf_size,
                                                 uint32_t dump_success_frame_count)
{
    FrameBenchmarkResult result = {};
    result.requested_frames = count;
    uint32_t dumped_success_frames = 0;

    for (uint32_t i = 0; i < count; ++i) {
        std::printf("\n=== Frame %lu/%lu ===\n",
                    (unsigned long)(i + 1),
                    (unsigned long)count);
        uint64_t read_start_us = time_us_now();
        int32_t bytes_read = read_metadata(buf, max_buf_size);
        uint64_t read_end_us = time_us_now();

        if (bytes_read > 0) {
            if (result.success_frames == 0) {
                result.first_frame_us = read_end_us;
            }
            result.last_frame_us = read_end_us;
            result.success_frames++;
            result.total_bytes += (uint64_t)bytes_read;
            result.total_read_cost_us += (read_end_us - read_start_us);
            std::printf("Successfully read %ld bytes, read cost=%.2f ms\n",
                        (long)bytes_read,
                        bench_us_to_ms(read_end_us - read_start_us));
            if (dumped_success_frames < dump_success_frame_count) {
                print_frame_hex_dump(buf,
                                     (uint32_t)bytes_read,
                                     result.success_frames);
                dumped_success_frames++;
            }
        } else {
            result.failed_frames++;
            std::printf("Failed to read frame\n");
        }
    }

    if (result.success_frames > 1 && result.last_frame_us > result.first_frame_us) {
        uint64_t span_us = result.last_frame_us - result.first_frame_us;
        result.fps = ((float)(result.success_frames - 1) * 1000000.0f) / (float)span_us;
    }
    if (result.success_frames > 0) {
        result.avg_read_ms = bench_us_to_ms(result.total_read_cost_us) / (float)result.success_frames;
        result.avg_bytes_per_frame = (float)result.total_bytes / (float)result.success_frames;
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
                (unsigned long)status.status,
                (unsigned long)status.result,
                (unsigned long)status.bytes_done,
                (unsigned long)status.bytes_total);
}

static bool program_flash_assets()
{
    std::printf("Programming model to module flash...\n");
    if (!write_model_to_cam_flash(NN_FW_DATA, NN_FW_SIZE)) {
        dump_spi_flash_status("[MODEL FLASH]");
        return false;
    }

    std::printf("Programming network_info to module flash...\n");
    if (!write_nn_info_to_cam_flash(NN_NETOWRK_INFO_DATA, NN_NETOWRK_INFO_SIZE)) {
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
#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    std::printf("Boot mode: %s\n", get_boot_mode_name());
    return imx500_open(NN_FW_DATA,
                NN_FW_SIZE,
                NN_NETOWRK_INFO_DATA,
                NN_NETOWRK_INFO_SIZE,
                MIPI_DATA_IMAGE,
                SPI_METADATA_OUTPUT_TENSOR,
                10);
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    std::printf("Boot mode: %s\n", get_boot_mode_name());
    return imx500_open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, SPI_METADATA_OUTPUT_TENSOR, 10);
#else
#error "Unsupported INTEGRATION_TEST_BOOT_MODE"
#endif
}

int main()
{
    std::printf("RPi5 SPI receive integration test\n");
    std::printf("I2C=%s SPI=%s speed=%lu Hz mode=%u\n",
                RPI5_I2C_DEVICE,
                RPI5_SPI_DEVICE,
                (unsigned long)RPI5_SPI_SPEED_HZ,
                (unsigned)RPI5_SPI_MODE);
    std::printf("Dump first %lu successful pre-injection frame(s)\n",
                (unsigned long)INTEGRATION_TEST_DUMP_FRAME_COUNT);

    if (!init_peripherals()) {
        return 1;
    }
    bind_peripherals_api();

    uint32_t module_fw_ver = 0;
    get_fw_ver(&module_fw_ver);
    LOG_INFO("module fw version: 0x%x\n", module_fw_ver);

    uint32_t module_pid = 0;
    get_pid(&module_pid);
    LOG_INFO("module pid: 0x%x\n", module_pid);

    uint64_t open_start_us = time_us_now();
    bool open_ret = open_with_selected_boot_mode();
    uint64_t open_end_us = time_us_now();
    if (!open_ret) {
        std::printf("imx500_open() failed\n");
        close_peripherals();
        return 1;
    }

    uint64_t stream_on_start_us = time_us_now();
    stream_on();
    uint64_t stream_on_end_us = time_us_now();

    FrameBenchmarkResult pre_inject_bench = benchmark_read_frame(
        BENCHMARK_FRAME_COUNT_PRE_INJECT,
        frame_buf,
        MAX_FRAME_SIZE,
        INTEGRATION_TEST_DUMP_FRAME_COUNT);

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
    do_data_injection_stream(provider_fill_0x55, 640 * 480 * 3, true);
    stop_data_injection();
    switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_MSPI);

    FrameBenchmarkResult post_inject_bench = benchmark_read_frame(
        BENCHMARK_FRAME_COUNT_POST_INJECT,
        frame_buf,
        MAX_FRAME_SIZE,
        0);

    print_benchmark_table(open_end_us - open_start_us,
                          stream_on_end_us - stream_on_start_us,
                          startup_total_us,
                          first_frame_latency_us,
                          &pre_inject_bench,
                          &post_inject_bench);

    close_peripherals();
    return 0;
}
