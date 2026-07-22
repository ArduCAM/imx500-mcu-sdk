#include <stdint.h>
#include <stdio.h>

#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "ArducamIMX500SDK.h"
#include "ai_regs.h"
#include "g_config.h"
#include "peripherals_adapter.h"

#define BENCHMARK_SPI_MODE_OUTPUT_ONLY 0
#define BENCHMARK_SPI_MODE_JPEG_INPUT_OUTPUT 1

#ifndef BENCHMARK_SPI_MODE
#define BENCHMARK_SPI_MODE BENCHMARK_SPI_MODE_OUTPUT_ONLY
#endif

#ifndef BENCHMARK_SPI_BAUDRATE_HZ
#define BENCHMARK_SPI_BAUDRATE_HZ 20000000
#endif

#ifndef BENCHMARK_SENSOR_FPS
#define BENCHMARK_SENSOR_FPS 30
#endif

#ifndef BENCHMARK_WARMUP_FRAMES
#define BENCHMARK_WARMUP_FRAMES 10
#endif

#ifndef BENCHMARK_MEASURE_FRAMES
#define BENCHMARK_MEASURE_FRAMES 300
#endif

#ifndef BENCHMARK_WARMUP_RETRIES
#define BENCHMARK_WARMUP_RETRIES 8
#endif

#ifndef BENCHMARK_MAX_METADATA_BYTES
#define BENCHMARK_MAX_METADATA_BYTES (2 * 1024 * 1024)
#endif

namespace {

constexpr uint32_t kI2cBaudrateHz = 100 * 1000;
constexpr uint32_t kMetadataReadyTimeoutMs = 1000;
// Match read_metadata(): avoid hammering the I2C bridge while a JPEG frame is
// still being produced.
constexpr uint32_t kMetadataReadyPollMs = 10;
constexpr uint32_t kScratchBufferSize = 4 * 1024;
constexpr uint32_t kSpiCsSetupUs = 20;
constexpr uint32_t kSpiCsHoldUs = 20;
constexpr uint32_t kSpiPostTransferUs = 50;

alignas(4) uint8_t scratch_buffer[kScratchBufferSize];

enum class FrameReadStatus {
    Success,
    MetadataReadyTimeout,
    MetadataReadyReadFailed,
    MetadataSizeZero,
    MetadataTooLarge,
    SpiReadFailed,
};

struct FrameReadResult {
    FrameReadStatus status;
    int32_t bytes;
    uint32_t reported_bytes;
    uint64_t spi_us;
};

struct BenchmarkResult {
    uint32_t requested_frames;
    uint32_t successful_frames;
    uint32_t failed_frames;
    uint32_t min_frame_bytes;
    uint32_t max_frame_bytes;
    uint64_t total_bytes;
    uint64_t total_read_us;
    uint64_t total_spi_us;
    uint64_t started_us;
    uint64_t finished_us;
    uint64_t first_frame_us;
    uint64_t last_frame_us;
};

constexpr spi_data_format_t selected_spi_format() {
#if BENCHMARK_SPI_MODE == BENCHMARK_SPI_MODE_OUTPUT_ONLY
    return SPI_METADATA_OUTPUT_TENSOR;
#elif BENCHMARK_SPI_MODE == BENCHMARK_SPI_MODE_JPEG_INPUT_OUTPUT
    return SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR;
#else
#error "Unsupported BENCHMARK_SPI_MODE"
#endif
}

constexpr const char *selected_spi_format_name() {
#if BENCHMARK_SPI_MODE == BENCHMARK_SPI_MODE_OUTPUT_ONLY
    return "OUTPUT_TENSOR";
#elif BENCHMARK_SPI_MODE == BENCHMARK_SPI_MODE_JPEG_INPUT_OUTPUT
    return "JPEG_INPUT_TENSOR_OUTPUT_TENSOR";
#else
    return "UNKNOWN";
#endif
}

void configure_spi_output_pin(uint pin) {
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
}

void i2c_master_init(uint32_t baudrate) {
    i2c_init(I2C_HW_ADDR, baudrate);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

uint32_t spi_master_init(uint32_t baudrate) {
    const uint32_t actual_baudrate = spi_init(SPI_HW_ADDR, baudrate);
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
    return actual_baudrate;
}

FrameReadStatus wait_for_metadata_ready() {
    uint32_t waited_ms = 0;
    while (waited_ms < kMetadataReadyTimeoutMs) {
        uint32_t ready_status = 0;
        if (pivariety_i2c_bridge_read(DATA_READY_STATUS_REG, &ready_status, 4) < 0) {
            return FrameReadStatus::MetadataReadyReadFailed;
        }
        if ((ready_status & 0xffu) == 0x01u) {
            return FrameReadStatus::Success;
        }
        sleep_ms(kMetadataReadyPollMs);
        waited_ms += kMetadataReadyPollMs;
    }
    return FrameReadStatus::MetadataReadyTimeout;
}

const char *frame_read_status_name(FrameReadStatus status) {
    switch (status) {
    case FrameReadStatus::Success:
        return "success";
    case FrameReadStatus::MetadataReadyTimeout:
        return "metadata-ready timeout";
    case FrameReadStatus::MetadataReadyReadFailed:
        return "metadata-ready I2C read failed";
    case FrameReadStatus::MetadataSizeZero:
        return "metadata size is zero";
    case FrameReadStatus::MetadataTooLarge:
        return "metadata exceeds safety limit";
    case FrameReadStatus::SpiReadFailed:
        return "SPI read failed";
    }
    return "unknown";
}

FrameReadResult drain_metadata_frame() {
    FrameReadResult result = {};
    result.status = wait_for_metadata_ready();
    if (result.status != FrameReadStatus::Success) {
        return result;
    }

    const uint32_t metadata_size = get_metadata_size();
    result.reported_bytes = metadata_size;
    if (metadata_size == 0) {
        result.status = FrameReadStatus::MetadataSizeZero;
        return result;
    }
    if (metadata_size > BENCHMARK_MAX_METADATA_BYTES) {
        result.status = FrameReadStatus::MetadataTooLarge;
        return result;
    }

    gpio_put(SPI_CSN_PIN, 0);
    sleep_us(kSpiCsSetupUs);
    const uint64_t spi_start_us = time_us_64();

    uint32_t remaining = metadata_size;
    while (remaining > 0) {
        const uint32_t chunk = remaining < sizeof(scratch_buffer)
            ? remaining
            : sizeof(scratch_buffer);
        const int read_count = spi_read_blocking(
            SPI_HW_ADDR,
            0x00,
            scratch_buffer,
            chunk
        );
        if (read_count < 0 || static_cast<uint32_t>(read_count) != chunk) {
            result.status = FrameReadStatus::SpiReadFailed;
            break;
        }
        remaining -= chunk;
    }

    const uint64_t spi_end_us = time_us_64();
    sleep_us(kSpiCsHoldUs);
    gpio_put(SPI_CSN_PIN, 1);
    sleep_us(kSpiPostTransferUs);

    result.spi_us = spi_end_us - spi_start_us;
    if (remaining == 0) {
        result.status = FrameReadStatus::Success;
        result.bytes = static_cast<int32_t>(metadata_size);
    }
    return result;
}

bool drain_warmup_frames(uint32_t count, uint64_t *first_frame_us) {
    uint32_t successful_frames = 0;
    uint32_t failed_attempts = 0;
    while (successful_frames < count) {
        const FrameReadResult frame = drain_metadata_frame();
        if (frame.status == FrameReadStatus::MetadataTooLarge) {
            printf("Warmup metadata frame is too large: %lu > %lu bytes\n",
                   static_cast<unsigned long>(frame.reported_bytes),
                   static_cast<unsigned long>(BENCHMARK_MAX_METADATA_BYTES));
            return false;
        }
        if (frame.status != FrameReadStatus::Success || frame.bytes <= 0) {
            ++failed_attempts;
            printf("Warmup retry %lu/%lu: %s\n",
                   static_cast<unsigned long>(failed_attempts),
                   static_cast<unsigned long>(BENCHMARK_WARMUP_RETRIES),
                   frame_read_status_name(frame.status));
            if (failed_attempts >= BENCHMARK_WARMUP_RETRIES) {
                printf("Warmup failed after %lu retries at frame %lu/%lu\n",
                       static_cast<unsigned long>(failed_attempts),
                       static_cast<unsigned long>(successful_frames + 1),
                       static_cast<unsigned long>(count));
                return false;
            }
            continue;
        }
        if (successful_frames == 0 && first_frame_us != nullptr) {
            *first_frame_us = time_us_64();
        }
        ++successful_frames;
    }
    return true;
}

BenchmarkResult run_benchmark(uint32_t frame_count) {
    BenchmarkResult result = {};
    result.requested_frames = frame_count;
    result.started_us = time_us_64();

    for (uint32_t i = 0; i < frame_count; ++i) {
        const uint64_t read_start_us = time_us_64();
        const FrameReadResult frame = drain_metadata_frame();
        const uint64_t read_end_us = time_us_64();

        if (frame.status == FrameReadStatus::MetadataTooLarge) {
            printf("Metadata frame is too large: %lu > %lu bytes; stopping benchmark\n",
                   static_cast<unsigned long>(frame.reported_bytes),
                   static_cast<unsigned long>(BENCHMARK_MAX_METADATA_BYTES));
            ++result.failed_frames;
            break;
        }
        if (frame.status != FrameReadStatus::Success || frame.bytes <= 0) {
            ++result.failed_frames;
            continue;
        }

        const uint32_t frame_bytes = static_cast<uint32_t>(frame.bytes);
        if (result.successful_frames == 0) {
            result.first_frame_us = read_end_us;
            result.min_frame_bytes = frame_bytes;
        }
        result.last_frame_us = read_end_us;
        result.min_frame_bytes = frame_bytes < result.min_frame_bytes
            ? frame_bytes
            : result.min_frame_bytes;
        result.max_frame_bytes = frame_bytes > result.max_frame_bytes
            ? frame_bytes
            : result.max_frame_bytes;
        ++result.successful_frames;
        result.total_bytes += frame_bytes;
        result.total_read_us += read_end_us - read_start_us;
        result.total_spi_us += frame.spi_us;
    }

    result.finished_us = time_us_64();
    return result;
}

double us_to_ms(uint64_t us) {
    return static_cast<double>(us) / 1000.0;
}

double interval_fps(const BenchmarkResult &result) {
    if (result.successful_frames < 2 || result.last_frame_us <= result.first_frame_us) {
        return 0.0;
    }
    return static_cast<double>(result.successful_frames - 1) * 1000000.0 /
        static_cast<double>(result.last_frame_us - result.first_frame_us);
}

void print_summary(
    const BenchmarkResult &result,
    uint32_t actual_spi_baudrate,
    uint64_t open_us,
    uint64_t stream_on_us,
    uint64_t first_frame_latency_us
) {
    const double elapsed_s = static_cast<double>(result.finished_us - result.started_us) / 1000000.0;
    const double wall_fps = elapsed_s > 0.0
        ? static_cast<double>(result.successful_frames) / elapsed_s
        : 0.0;
    const double avg_frame_bytes = result.successful_frames > 0
        ? static_cast<double>(result.total_bytes) / result.successful_frames
        : 0.0;
    const double avg_read_ms = result.successful_frames > 0
        ? us_to_ms(result.total_read_us) / result.successful_frames
        : 0.0;
    const double payload_mbps = result.total_spi_us > 0
        ? static_cast<double>(result.total_bytes) * 8.0 / result.total_spi_us
        : 0.0;

    printf("\n================ IMX500 Inference FPS Benchmark ================\n");
    printf("Boot source              : camera module Flash\n");
    printf("SPI metadata format      : %s\n", selected_spi_format_name());
    printf("SPI clock requested      : %lu Hz\n", static_cast<unsigned long>(BENCHMARK_SPI_BAUDRATE_HZ));
    printf("SPI clock actual         : %lu Hz\n", static_cast<unsigned long>(actual_spi_baudrate));
    printf("Sensor FPS requested     : %lu\n", static_cast<unsigned long>(BENCHMARK_SENSOR_FPS));
    printf("Scratch buffer           : %lu bytes\n", static_cast<unsigned long>(sizeof(scratch_buffer)));
    printf("Warmup frames            : %lu\n", static_cast<unsigned long>(BENCHMARK_WARMUP_FRAMES));
    printf("Warmup retry limit       : %lu\n", static_cast<unsigned long>(BENCHMARK_WARMUP_RETRIES));
    printf("Measured frames          : %lu/%lu success, %lu failed\n",
           static_cast<unsigned long>(result.successful_frames),
           static_cast<unsigned long>(result.requested_frames),
           static_cast<unsigned long>(result.failed_frames));
    printf("imx500_open              : %.2f ms\n", us_to_ms(open_us));
    printf("stream_on                : %.2f ms\n", us_to_ms(stream_on_us));
    printf("stream_on -> first frame : %.2f ms\n", us_to_ms(first_frame_latency_us));
    printf("Inference frame rate     : %.2f FPS\n", interval_fps(result));
    printf("Measurement wall rate    : %.2f FPS\n", wall_fps);
    printf("Average wait + drain     : %.2f ms/frame\n", avg_read_ms);
    printf("Metadata bytes/frame     : avg %.1f, min %lu, max %lu\n",
           avg_frame_bytes,
           static_cast<unsigned long>(result.min_frame_bytes),
           static_cast<unsigned long>(result.max_frame_bytes));
    printf("SPI payload throughput   : %.2f Mbit/s\n", payload_mbps);
    printf("Measurement elapsed      : %.2f s\n", elapsed_s);
    printf("================================================================\n");
    fflush(stdout);
}

}  // namespace

int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(250);

    printf("IMX500 Pico 2 inference FPS benchmark\n");
    printf("Loading model and network_info from camera module Flash\n");
    printf("SPI mode: %s\n", selected_spi_format_name());
    fflush(stdout);

    i2c_master_init(kI2cBaudrateHz);
    const uint32_t actual_spi_baudrate = spi_master_init(BENCHMARK_SPI_BAUDRATE_HZ);
    bind_peripherals_api();

    const uint64_t open_start_us = time_us_64();
    const bool opened = imx500_open(
        nullptr,
        0,
        nullptr,
        0,
        MIPI_DATA_IMAGE,
        selected_spi_format(),
        BENCHMARK_SENSOR_FPS
    );
    const uint64_t open_end_us = time_us_64();
    if (!opened) {
        printf("ERROR: imx500_open() failed; verify model and network_info in module Flash\n");
        return 1;
    }

    const uint64_t stream_on_start_us = time_us_64();
    stream_on();
    const uint64_t stream_on_end_us = time_us_64();

    uint64_t first_frame_us = 0;
    if (!drain_warmup_frames(BENCHMARK_WARMUP_FRAMES, &first_frame_us)) {
        printf("ERROR: benchmark warmup failed\n");
        return 1;
    }

    const BenchmarkResult result = run_benchmark(BENCHMARK_MEASURE_FRAMES);
    const uint64_t observed_first_frame_us = first_frame_us != 0
        ? first_frame_us
        : result.first_frame_us;
    const uint64_t first_frame_latency_us = observed_first_frame_us >= stream_on_end_us
        ? observed_first_frame_us - stream_on_end_us
        : 0;
    print_summary(
        result,
        actual_spi_baudrate,
        open_end_us - open_start_us,
        stream_on_end_us - stream_on_start_us,
        first_frame_latency_us
    );

    while (true) {
        tight_loop_contents();
    }
}
