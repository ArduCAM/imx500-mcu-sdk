#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

#include "g_config.h"
#include "ArducamIMX500SDK.h"
#include "peripherals_adapter.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_NoID.h"
#include "imx500_firmware_cpp/imx500_firmware/InputTensorOnly_network_info.h"

#define NN_FW_DATA              InputTensorOnly_NoID_data
#define NN_FW_SIZE              InputTensorOnly_NoID_size
#define NN_NETWORK_INFO_DATA    InputTensorOnly_network_info_data
#define NN_NETWORK_INFO_SIZE    InputTensorOnly_network_info_size
#define MAX_FRAME_SIZE          (1024 * 10)
#define COMMAND_BUFFER_SIZE     64

namespace {

uint8_t frame_buf[MAX_FRAME_SIZE];

void i2c_master_init(uint32_t baudrate) {
    i2c_init(i2c_default, baudrate);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

void spi_master_init(uint32_t baudrate) {
    spi_init(spi_default, baudrate);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CSN_PIN);
    gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
    gpio_put(SPI_CSN_PIN, 1);

    spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

void print_banner() {
    printf("IMX500 Pico2 production test ready\n");
    printf("TEST_STATUS: READY\n");
    printf("Send RUN to start the production test\n");
    fflush(stdout);
}

bool read_command_line(char *buf, size_t buf_len) {
    size_t pos = 0;
    while (true) {
        int ch = getchar_timeout_us(1000);
        if (ch == PICO_ERROR_TIMEOUT) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (pos == 0) {
                continue;
            }
            buf[pos] = '\0';
            return true;
        }
        if (ch == 0x03) {
            buf[0] = '\0';
            return true;
        }
        if (pos + 1 < buf_len) {
            buf[pos++] = (char)ch;
        }
    }
}

void print_module_identity() {
    uint32_t module_fw_ver = 0;
    uint32_t module_pid = 0;
    get_fw_ver(&module_fw_ver);
    get_pid(&module_pid);
    printf("module fw version: 0x%08lx\n", (unsigned long)module_fw_ver);
    printf("module pid: 0x%08lx\n", (unsigned long)module_pid);
}

void print_frame_prefix(const uint8_t *buf, uint32_t size) {
    const uint32_t count = size < 16 ? size : 16;
    printf("first frame prefix (%lu bytes):", (unsigned long)count);
    for (uint32_t i = 0; i < count; ++i) {
        printf(" %02X", buf[i]);
    }
    printf("\n");
}

bool run_production_test() {
    printf("TEST_STATUS: RUNNING\n");
    print_module_identity();

    if (!open(NN_FW_DATA,
              NN_FW_SIZE,
              NN_NETWORK_INFO_DATA,
              NN_NETWORK_INFO_SIZE,
              MIPI_DATA_IMAGE,
              SPI_METADATA_OUTPUT_TENSOR,
              10)) {
        printf("TEST_RESULT: FAIL\n");
        printf("TEST_REASON: open_failed\n");
        fflush(stdout);
        return false;
    }

    printf("open() completed, starting stream\n");
    stream_on();

    printf("Waiting for first frame...\n");
    int32_t bytes_read = read_metadata(frame_buf, sizeof(frame_buf));
    if (bytes_read <= 0) {
        printf("TEST_RESULT: FAIL\n");
        printf("TEST_REASON: first_frame_read_failed\n");
        fflush(stdout);
        return false;
    }

    printf("first frame bytes: %ld\n", (long)bytes_read);
    print_frame_prefix(frame_buf, (uint32_t)bytes_read);

    const uint8_t first_byte = frame_buf[0];
    printf("first frame first byte: 0x%02X\n", first_byte);
    if (first_byte == 0x01u) {
        printf("TEST_RESULT: PASS\n");
        printf("TEST_REASON: first_byte_is_0x01\n");
        fflush(stdout);
        return true;
    }

    printf("TEST_RESULT: FAIL\n");
    printf("TEST_REASON: first_byte_not_0x01\n");
    fflush(stdout);
    return false;
}

}  // namespace

int main() {
    stdio_init_all();

    i2c_master_init(100 * 1000);
    spi_master_init(1000 * 1000 * 5);
    bind_peripherals_api();

    print_banner();

    char command[COMMAND_BUFFER_SIZE];
    bool has_run = false;
    while (true) {
        if (!read_command_line(command, sizeof(command))) {
            continue;
        }

        if (strcmp(command, "RUN") == 0) {
            if (has_run) {
                printf("TEST_STATUS: BUSY\n");
                printf("TEST_REASON: already_executed_reset_board_to_retry\n");
                fflush(stdout);
                continue;
            }
            has_run = true;
            run_production_test();
            fflush(stdout);
            sleep_ms(200);
            watchdog_reboot(0, 0, 0);
            while (true) {
                tight_loop_contents();
            }
            continue;
        }

        if (strcmp(command, "PING") == 0) {
            printf("PONG\n");
            fflush(stdout);
            continue;
        }

        printf("Unknown command: %s\n", command);
        printf("Supported commands: RUN, PING\n");
        fflush(stdout);
    }
}
