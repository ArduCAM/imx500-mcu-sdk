#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "g_config.h"
#include "log.h"
#include "string.h"
#include "ArducamIMX500SDK.h"
#include "peripherals_adapter.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_NoID.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_network_info.h"

#define NN_FW_DATA              InputTensorOnly_NoID_data
#define NN_FW_SIZE              InputTensorOnly_NoID_size
#define NN_NETOWRK_INFO_DATA    InputTensorOnly_network_info_data
#define NN_NETOWRK_INFO_SIZE    InputTensorOnly_network_info_size
#define MAX_FRAME_SIZE          (1024 * 220)
#define BENCHMARK_FRAME_COUNT_PRE_INJECT   30
#define BENCHMARK_FRAME_COUNT_POST_INJECT  30

#define INTEGRATION_TEST_BOOT_MODE_DIRECT 0
#define INTEGRATION_TEST_BOOT_MODE_FLASH  1

#ifndef INTEGRATION_TEST_BOOT_MODE
#define INTEGRATION_TEST_BOOT_MODE INTEGRATION_TEST_BOOT_MODE_FLASH
#endif

#ifndef INTEGRATION_TEST_SPI_BAUDRATE_HZ
#define INTEGRATION_TEST_SPI_BAUDRATE_HZ (1000 * 1000 * 5)
#endif

uint8_t frame_buf[MAX_FRAME_SIZE];

static void configure_spi_output_pin(uint pin) {
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
}

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

static float bench_us_to_ms(uint64_t us) {
    return (float)us / 1000.0f;
}

static void print_benchmark_row(const char* metric, const char* value) {
    printf("| %-39s | %-23s |\n", metric, value);
}

static void print_benchmark_table(
    uint64_t open_cost_us,
    uint64_t stream_on_cost_us,
    uint64_t startup_total_us,
    uint64_t first_frame_latency_us,
    const FrameBenchmarkResult* pre_inject,
    const FrameBenchmarkResult* post_inject
) {
    char value[64];

    printf("\n================ IMX500 Benchmark Summary ================\n");
    printf("+-----------------------------------------+-------------------------+\n");
    printf("| Metric                                  | Value                   |\n");
    printf("+-----------------------------------------+-------------------------+\n");

    snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(open_cost_us));
    print_benchmark_row("open() duration", value);

    snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(stream_on_cost_us));
    print_benchmark_row("stream_on() duration", value);

    snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(first_frame_latency_us));
    print_benchmark_row("stream_on -> first frame", value);

    snprintf(value, sizeof(value), "%.2f ms", bench_us_to_ms(startup_total_us));
    print_benchmark_row("open -> first frame (startup)", value);

    snprintf(value, sizeof(value), "%lu/%lu",
             (unsigned long)pre_inject->success_frames,
             (unsigned long)pre_inject->requested_frames);
    print_benchmark_row("frames before injection", value);

    snprintf(value, sizeof(value), "%.2f fps", pre_inject->fps);
    print_benchmark_row("FPS before injection", value);

    snprintf(value, sizeof(value), "%.2f bytes", pre_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (before)", value);

    snprintf(value, sizeof(value), "%lu/%lu",
             (unsigned long)post_inject->success_frames,
             (unsigned long)post_inject->requested_frames);
    print_benchmark_row("frames after injection", value);

    snprintf(value, sizeof(value), "%.2f fps", post_inject->fps);
    print_benchmark_row("FPS after injection", value);

    snprintf(value, sizeof(value), "%.2f bytes", post_inject->avg_bytes_per_frame);
    print_benchmark_row("avg metadata bytes/frame (after)", value);

    printf("+-----------------------------------------+-------------------------+\n");
}

/**
 * Initialize I2C interface
 * @param baudrate I2C clock frequency in Hz
 */
void i2c_master_init(uint32_t baudrate) {
    // Initialize I2C
    i2c_init(i2c_default, baudrate);
    
    // Configure I2C pins
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    printf("I2C initialized at %d Hz\n", baudrate);
}

/**
 * Initialize SPI interface
 * @param baudrate SPI clock frequency in Hz
 */
void spi_master_init(uint32_t baudrate) {
    // Enable SPI and connect to GPIOs
    spi_init(spi_default, baudrate);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
    
    // Configure CS pin as GPIO output (manual control)
    gpio_init(SPI_CSN_PIN);
    gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
    gpio_put(SPI_CSN_PIN, 1);  // CS idle high
    configure_spi_output_pin(SPI_SCK_PIN);
    configure_spi_output_pin(SPI_TX_PIN);
    configure_spi_output_pin(SPI_CSN_PIN);
    
    // Set SPI format: 8 bits, CPOL=1, CPHA=1, MSB first
    spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    printf("SPI initialized at %d Hz\n", baudrate);
}

uint32_t provider_fill_0x55(
    uint8_t *buf,
    uint32_t max_len,
    uint32_t offset
) {
    (void)offset;
    memset(buf, 0x55, max_len);
    return max_len;
}

FrameBenchmarkResult benchmark_read_frame(
    uint32_t count,
    uint8_t* buf,
    uint32_t max_buf_size,
    bool print_frame_sample
) {
    FrameBenchmarkResult result = {0};
    result.requested_frames = count;

    for (uint32_t i = 0; i < count; ++i) {
        printf("\n=== Frame %lu/%lu ===\n", (unsigned long)(i + 1), (unsigned long)count);
        uint64_t read_start_us = time_us_64();
        int32_t bytes_read = read_metadata(buf, max_buf_size);
        uint64_t read_end_us = time_us_64();

        if (bytes_read > 0) {
            if (result.success_frames == 0) {
                result.first_frame_us = read_end_us;
            }
            result.last_frame_us = read_end_us;
            result.success_frames++;
            result.total_bytes += (uint64_t)bytes_read;
            result.total_read_cost_us += (read_end_us - read_start_us);
            printf("Successfully read %ld bytes, read cost=%.2f ms\n",
                   (long)bytes_read, bench_us_to_ms(read_end_us - read_start_us));
            if (print_frame_sample) {
                print_buf_hex(buf, 12);
                printf("\n");
            }
        } else {
            result.failed_frames++;
            printf("Failed to read frame\n");
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

static void dump_spi_flash_status(const char *label) {
    spi_flash_status_t status = {0};
    if (!get_spi_flash_status(&status)) {
        printf("%s failed to read spi flash status\n", label);
        return;
    }
    printf("%s status=%lu result=%lu bytes=%lu/%lu\n",
           label,
           (unsigned long)status.status,
           (unsigned long)status.result,
           (unsigned long)status.bytes_done,
           (unsigned long)status.bytes_total);
}

static bool program_flash_assets(void) {
    printf("Programming model to module flash...\n");
    if (!write_model_to_cam_flash(NN_FW_DATA, NN_FW_SIZE)) {
        dump_spi_flash_status("[MODEL FLASH]");
        return false;
    }

    printf("Programming network_info to module flash...\n");
    if (!write_nn_info_to_cam_flash(NN_NETOWRK_INFO_DATA, NN_NETOWRK_INFO_SIZE)) {
        dump_spi_flash_status("[NN INFO FLASH]");
        return false;
    }

    dump_spi_flash_status("[FLASH PROGRAM DONE]");
    return true;
}

static const char* get_boot_mode_name(void) {
#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    return "DIRECT_BOOT";
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    return "FLASH_BOOT";
#else
    return "UNKNOWN_BOOT_MODE";
#endif
}

static bool open_with_selected_boot_mode(void) {
#if INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_DIRECT
    printf("Boot mode: %s\n", get_boot_mode_name());
    return open(NN_FW_DATA,
                NN_FW_SIZE,
                NN_NETOWRK_INFO_DATA,
                NN_NETOWRK_INFO_SIZE,
                MIPI_DATA_IMAGE,
                SPI_METADATA_OUTPUT_TENSOR,
                10);
#elif INTEGRATION_TEST_BOOT_MODE == INTEGRATION_TEST_BOOT_MODE_FLASH
    printf("Boot mode: %s\n", get_boot_mode_name());
    return open(nullptr, 0, nullptr, 0, MIPI_DATA_IMAGE, SPI_METADATA_OUTPUT_TENSOR, 10);
#else
#error "Unsupported INTEGRATION_TEST_BOOT_MODE"
#endif
}

int main() {
    stdio_init_all();

    // =========== For debug ===========
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    printf("USB serial connected!\n");
    // =========== For debug ===========

    sleep_ms(100);
    i2c_master_init(100 * 1000);
    spi_master_init(INTEGRATION_TEST_SPI_BAUDRATE_HZ);
    bind_peripherals_api();

    uint32_t module_fw_ver;
    get_fw_ver(&module_fw_ver);
    LOG_INFO("module fw version: 0x%x\n", module_fw_ver);

    uint32_t module_pid;
    get_pid(&module_pid);
    LOG_INFO("module pid: 0x%x\n", module_pid);

    uint64_t open_start_us = time_us_64();
    bool open_ret = open_with_selected_boot_mode();
    uint64_t open_end_us = time_us_64();
    if (!open_ret) {
        printf("open() failed\n");
        return 1;
    }

    uint64_t stream_on_start_us = time_us_64();
    stream_on();
    uint64_t stream_on_end_us = time_us_64();

    FrameBenchmarkResult pre_inject_bench = benchmark_read_frame(
        BENCHMARK_FRAME_COUNT_PRE_INJECT,
        frame_buf,
        MAX_FRAME_SIZE,
        true
    );

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
    // test data injection
    do_data_injection_stream(
        provider_fill_0x55,
        640 * 480 * 3,
        true
    );
    stop_data_injection();
    switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_MSPI);

    FrameBenchmarkResult post_inject_bench = benchmark_read_frame(
        BENCHMARK_FRAME_COUNT_POST_INJECT,
        frame_buf,
        MAX_FRAME_SIZE,
        false
    );

    print_benchmark_table(
        open_end_us - open_start_us,
        stream_on_end_us - stream_on_start_us,
        startup_total_us,
        first_frame_latency_us,
        &pre_inject_bench,
        &post_inject_bench
    );

    return 0;
}
