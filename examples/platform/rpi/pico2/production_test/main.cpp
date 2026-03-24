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
#define MODULE_POLL_INTERVAL_MS 100
#define MODULE_STABLE_POLLS     3

#ifndef PRODUCTION_TEST_CONTINUOUS_LED_MODE
#define PRODUCTION_TEST_CONTINUOUS_LED_MODE 0
#endif

namespace {

uint8_t frame_buf[MAX_FRAME_SIZE];

#if defined(PICO_DEFAULT_LED_PIN)
constexpr uint kLedPin = PICO_DEFAULT_LED_PIN;
constexpr bool kHasBuiltinLed = true;
#else
constexpr uint kLedPin = 0;
constexpr bool kHasBuiltinLed = false;
#endif

enum class LedState {
    Idle,
    Pass,
    Fail,
};

enum class AutoTestState {
    WaitModuleInsert,
    WaitModuleRemove,
};

LedState g_led_state = LedState::Idle;
bool g_led_level = false;
absolute_time_t g_led_toggle_at;

void init_test_led() {
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
    if constexpr (kHasBuiltinLed) {
        gpio_init(kLedPin);
        gpio_set_dir(kLedPin, GPIO_OUT);
        g_led_toggle_at = get_absolute_time();
        gpio_put(kLedPin, 0);
    }
#endif
}

void set_test_led_state(LedState state) {
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
    g_led_state = state;
    g_led_level = false;
    g_led_toggle_at = get_absolute_time();
    if constexpr (!kHasBuiltinLed) {
        return;
    }
    if (state == LedState::Pass) {
        gpio_put(kLedPin, 1);
        g_led_level = true;
    } else if (state == LedState::Idle) {
        gpio_put(kLedPin, 0);
    } else {
        gpio_put(kLedPin, 0);
    }
#else
    (void)state;
#endif
}

void service_test_led() {
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
    if constexpr (!kHasBuiltinLed) {
        return;
    }
    if (g_led_state != LedState::Fail) {
        return;
    }
    if (!time_reached(g_led_toggle_at)) {
        return;
    }
    g_led_level = !g_led_level;
    gpio_put(kLedPin, g_led_level ? 1 : 0);
    g_led_toggle_at = delayed_by_ms(get_absolute_time(), 200);
#endif
}

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
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
    printf("CONTINUOUS_MODE: ON\n");
    printf("Board will automatically test a newly inserted module and then wait for removal\n");
    printf("TEST_STATUS: WAIT_MODULE_INSERT\n");
#else
    printf("CONTINUOUS_MODE: OFF\n");
    printf("TEST_STATUS: READY\n");
    printf("Send RUN to start the production test\n");
#endif
    fflush(stdout);
}

bool read_command_line(char *buf, size_t buf_len) {
    size_t pos = 0;
    while (true) {
        service_test_led();
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

bool poll_command_line(char *buf, size_t buf_len) {
    static char line_buf[COMMAND_BUFFER_SIZE];
    static size_t pos = 0;

    service_test_led();
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) {
        return false;
    }
    if (ch == '\r' || ch == '\n') {
        if (pos == 0) {
            return false;
        }
        const size_t copy_len = pos < (buf_len - 1) ? pos : (buf_len - 1);
        memcpy(buf, line_buf, copy_len);
        buf[copy_len] = '\0';
        pos = 0;
        return true;
    }
    if (ch == 0x03) {
        pos = 0;
        return false;
    }
    if (pos + 1 < sizeof(line_buf)) {
        line_buf[pos++] = (char)ch;
    }
    return false;
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
    set_test_led_state(LedState::Idle);
    print_module_identity();

    if (!open(NN_FW_DATA,
              NN_FW_SIZE,
              NN_NETWORK_INFO_DATA,
              NN_NETWORK_INFO_SIZE,
              MIPI_DATA_IMAGE,
              SPI_METADATA_OUTPUT_TENSOR,
              10)) {
        set_test_led_state(LedState::Fail);
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
        set_test_led_state(LedState::Fail);
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
        set_test_led_state(LedState::Pass);
        printf("TEST_RESULT: PASS\n");
        printf("TEST_REASON: first_byte_is_0x01\n");
        fflush(stdout);
        return true;
    }

    set_test_led_state(LedState::Fail);
    printf("TEST_RESULT: FAIL\n");
    printf("TEST_REASON: first_byte_not_0x01\n");
    fflush(stdout);
    return false;
}

bool probe_module_ready(uint32_t *device_id, uint32_t *boot_status) {
    uint32_t local_device_id = 0;
    uint32_t local_boot_status = 0;
    if (!probe_imx500_module(&local_device_id, &local_boot_status)) {
        return false;
    }
    if (device_id) {
        *device_id = local_device_id;
    }
    if (boot_status) {
        *boot_status = local_boot_status;
    }
    return local_boot_status >= 1u;
}

void print_probe_status(const char *label, uint32_t device_id, uint32_t boot_status) {
    printf("%s device_id=0x%08lx boot_status=%lu\n",
           label,
           (unsigned long)device_id,
           (unsigned long)boot_status);
    fflush(stdout);
}

void handle_continuous_mode_command(const char *command) {
    if (strcmp(command, "PING") == 0) {
        printf("PONG\n");
        if (g_led_state == LedState::Pass) {
            printf("LED_STATUS: PASS_SOLID_ON\n");
        } else if (g_led_state == LedState::Fail) {
            printf("LED_STATUS: FAIL_BLINKING\n");
        } else {
            printf("LED_STATUS: IDLE_OFF\n");
        }
        fflush(stdout);
        return;
    }

    if (strcmp(command, "LED_OFF") == 0) {
        set_test_led_state(LedState::Idle);
        printf("LED_STATUS: IDLE_OFF\n");
        fflush(stdout);
        return;
    }

    if (strcmp(command, "RUN") == 0) {
        printf("TEST_STATUS: AUTO_MODE_WAITING_MODULE_CHANGE\n");
        printf("TEST_REASON: continuous_mode_runs_automatically_after_module_insert\n");
        fflush(stdout);
        return;
    }

    printf("Unknown command: %s\n", command);
    printf("Supported commands: PING, LED_OFF\n");
    fflush(stdout);
}

void run_continuous_mode() {
    AutoTestState state = AutoTestState::WaitModuleInsert;
    bool module_present = false;
    uint32_t stable_count = 0;
    uint32_t last_device_id = 0;
    uint32_t last_boot_status = 0;
    char command[COMMAND_BUFFER_SIZE];

    printf("TEST_STATUS: WAIT_MODULE_INSERT\n");
    fflush(stdout);

    while (true) {
        if (poll_command_line(command, sizeof(command))) {
            handle_continuous_mode_command(command);
        }

        uint32_t device_id = 0;
        uint32_t boot_status = 0;
        const bool ready = probe_module_ready(&device_id, &boot_status);

        if (ready == module_present) {
            if (stable_count < MODULE_STABLE_POLLS) {
                stable_count++;
            }
        } else {
            module_present = ready;
            stable_count = 1;
            last_device_id = device_id;
            last_boot_status = boot_status;
        }

        if (stable_count >= MODULE_STABLE_POLLS) {
            if (state == AutoTestState::WaitModuleInsert && module_present) {
                print_probe_status("MODULE_DETECTED", device_id, boot_status);
                run_production_test();
                printf("TEST_STATUS: WAIT_MODULE_REMOVE\n");
                printf("TEST_REASON: remove_tested_module_before_next_cycle\n");
                fflush(stdout);
                state = AutoTestState::WaitModuleRemove;
                stable_count = 0;
            } else if (state == AutoTestState::WaitModuleRemove && !module_present) {
                set_test_led_state(LedState::Idle);
                printf("MODULE_REMOVED last_device_id=0x%08lx last_boot_status=%lu\n",
                       (unsigned long)last_device_id,
                       (unsigned long)last_boot_status);
                printf("TEST_STATUS: WAIT_MODULE_INSERT\n");
                fflush(stdout);
                state = AutoTestState::WaitModuleInsert;
                stable_count = 0;
                last_device_id = 0;
                last_boot_status = 0;
            }
        }

        sleep_ms(MODULE_POLL_INTERVAL_MS);
    }
}

}  // namespace

int main() {
    stdio_init_all();

    i2c_master_init(100 * 1000);
    spi_master_init(1000 * 1000 * 5);
    bind_peripherals_api();
    init_test_led();
    set_test_led_state(LedState::Idle);

    print_banner();

#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
    run_continuous_mode();
    return 0;
#else

    char command[COMMAND_BUFFER_SIZE];
    while (true) {
        if (!read_command_line(command, sizeof(command))) {
            continue;
        }

        if (strcmp(command, "RUN") == 0) {
            const bool passed = run_production_test();
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
            printf("TEST_STATUS: READY\n");
            printf("TEST_REASON: continuous_mode_waiting_next_run\n");
            printf("LED_STATUS: %s\n", passed ? "PASS_SOLID_ON" : "FAIL_BLINKING");
            fflush(stdout);
            continue;
#else
            (void)passed;
            fflush(stdout);
            sleep_ms(200);
            watchdog_reboot(0, 0, 0);
            while (true) {
                tight_loop_contents();
            }
#endif
        }

        if (strcmp(command, "PING") == 0) {
            printf("PONG\n");
#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
            if (g_led_state == LedState::Pass) {
                printf("LED_STATUS: PASS_SOLID_ON\n");
            } else if (g_led_state == LedState::Fail) {
                printf("LED_STATUS: FAIL_BLINKING\n");
            } else {
                printf("LED_STATUS: IDLE_OFF\n");
            }
#endif
            fflush(stdout);
            continue;
        }

#if PRODUCTION_TEST_CONTINUOUS_LED_MODE
        if (strcmp(command, "LED_OFF") == 0) {
            set_test_led_state(LedState::Idle);
            printf("LED_STATUS: IDLE_OFF\n");
            fflush(stdout);
            continue;
        }
#endif

        printf("Unknown command: %s\n", command);
        printf("Supported commands: RUN, PING\n");
        fflush(stdout);
    }
#endif
}
