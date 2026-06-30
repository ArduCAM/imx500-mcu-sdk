#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/regs/io_qspi.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "g_config.h"
#include "ArducamIMX500SDK.h"
#include "peripherals_adapter.h"

#define NN_FW_DATA              nullptr
#define NN_FW_SIZE              0
#define NN_NETWORK_INFO_DATA    nullptr
#define NN_NETWORK_INFO_SIZE    0
#define MAX_FRAME_SIZE          (1024 * 10)
#define COMMAND_BUFFER_SIZE     64
#define BOOT_BUTTON_POLL_INTERVAL_MS 20
#define BOOT_BUTTON_STABLE_POLLS     3
#define MODULE_PROBE_INTERVAL_MS     100
#define MODULE_STABLE_POLLS          3

#ifndef PRODUCTION_TEST_BOOT_TRIGGER_MODE
#ifdef PRODUCTION_TEST_CONTINUOUS_LED_MODE
#define PRODUCTION_TEST_BOOT_TRIGGER_MODE PRODUCTION_TEST_CONTINUOUS_LED_MODE
#else
#define PRODUCTION_TEST_BOOT_TRIGGER_MODE 0
#endif
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

LedState g_led_state = LedState::Idle;
bool g_led_level = false;
absolute_time_t g_led_toggle_at;

void init_test_led() {
#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
    if constexpr (kHasBuiltinLed) {
        gpio_init(kLedPin);
        gpio_set_dir(kLedPin, GPIO_OUT);
        g_led_toggle_at = get_absolute_time();
        gpio_put(kLedPin, 0);
    }
#endif
}

void set_test_led_state(LedState state) {
#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
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
#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
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
#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
    printf("BOOT_TRIGGER_MODE: ON\n");
    printf("Press the Pico BOOT button to start one test cycle\n");
    printf("Additional BOOT presses are ignored while a test is running\n");
#else
    printf("BOOT_TRIGGER_MODE: OFF\n");
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

bool __no_inline_not_in_flash_func(read_boot_button_pressed_raw)() {
    const uint cs_pin_index = 1;

    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i) {
    }

#ifdef __ARM_ARCH_6M__
    constexpr uint32_t cs_bit = 1u << 1;
#else
    constexpr uint32_t cs_bit = SIO_GPIO_HI_IN_QSPI_CSN_BITS;
#endif
    const bool pin_high = (sio_hw->gpio_hi_in & cs_bit) != 0;

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return !pin_high;
}

bool poll_boot_button_pressed() {
    static bool last_raw = false;
    static bool debounced = false;
    static uint32_t stable_count = 0;

    const bool raw = read_boot_button_pressed_raw();
    if (raw == last_raw) {
        if (stable_count < BOOT_BUTTON_STABLE_POLLS) {
            stable_count++;
        }
    } else {
        last_raw = raw;
        stable_count = 1;
    }

    if (stable_count >= BOOT_BUTTON_STABLE_POLLS) {
        debounced = raw;
    }

    return debounced;
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

    if (!imx500_open(NN_FW_DATA,
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

    printf("imx500_open() completed, starting stream\n");
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

void print_boot_trigger_ready_message() {
    printf("TEST_STATUS: READY\n");
    printf("TEST_REASON: press_boot_button_or_send_run_to_start_test\n");
    fflush(stdout);
}

bool handle_boot_trigger_mode_command(const char *command) {
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
        return false;
    }

    if (strcmp(command, "LED_OFF") == 0) {
        set_test_led_state(LedState::Idle);
        printf("LED_STATUS: IDLE_OFF\n");
        fflush(stdout);
        return false;
    }

    if (strcmp(command, "RUN") == 0) {
        printf("TEST_TRIGGER: HOST_RUN\n");
        fflush(stdout);
        return true;
    }

    printf("Unknown command: %s\n", command);
    printf("Supported commands: RUN, PING, LED_OFF\n");
    fflush(stdout);
    return false;
}

void run_boot_trigger_mode() {
    char command[COMMAND_BUFFER_SIZE];
    bool last_button_pressed = false;
    bool module_present = false;
    bool last_module_sample = false;
    uint32_t module_stable_count = 0;
    absolute_time_t next_module_probe_at = get_absolute_time();

    print_boot_trigger_ready_message();

    while (true) {
        bool should_start_test = false;

        if (poll_command_line(command, sizeof(command))) {
            should_start_test = handle_boot_trigger_mode_command(command);
        }

        const bool button_pressed = poll_boot_button_pressed();
        if (button_pressed && !last_button_pressed) {
            printf("TEST_TRIGGER: BOOT_BUTTON\n");
            fflush(stdout);
            should_start_test = true;
        }
        last_button_pressed = button_pressed;

        if (time_reached(next_module_probe_at)) {
            uint32_t device_id = 0;
            uint32_t boot_status = 0;
            const bool ready = probe_module_ready(&device_id, &boot_status);

            if (ready == last_module_sample) {
                if (module_stable_count < MODULE_STABLE_POLLS) {
                    module_stable_count++;
                }
            } else {
                last_module_sample = ready;
                module_stable_count = 1;
            }

            if (module_stable_count >= MODULE_STABLE_POLLS && module_present != ready) {
                module_present = ready;
                if (module_present) {
                    printf("MODULE_DETECTED device_id=0x%08lx boot_status=%lu\n",
                           (unsigned long)device_id,
                           (unsigned long)boot_status);
                    fflush(stdout);
                } else {
                    set_test_led_state(LedState::Idle);
                    printf("MODULE_REMOVED\n");
                    printf("TEST_STATUS: READY\n");
                    printf("TEST_REASON: module_removed_reset_state\n");
                    fflush(stdout);
                }
            }

            next_module_probe_at = delayed_by_ms(get_absolute_time(), MODULE_PROBE_INTERVAL_MS);
        }

        if (should_start_test) {
            uint32_t device_id = 0;
            uint32_t boot_status = 0;
            if (!probe_module_ready(&device_id, &boot_status)) {
                set_test_led_state(LedState::Idle);
                printf("TEST_STATUS: READY\n");
                printf("TEST_REASON: module_not_detected_or_not_ready\n");
                fflush(stdout);
                sleep_ms(BOOT_BUTTON_POLL_INTERVAL_MS);
                continue;
            }
            module_present = true;
            last_module_sample = true;
            module_stable_count = MODULE_STABLE_POLLS;
            run_production_test();
            print_boot_trigger_ready_message();
        }

        sleep_ms(BOOT_BUTTON_POLL_INTERVAL_MS);
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

#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
    run_boot_trigger_mode();
    return 0;
#else

    char command[COMMAND_BUFFER_SIZE];
    while (true) {
        if (!read_command_line(command, sizeof(command))) {
            continue;
        }

        if (strcmp(command, "RUN") == 0) {
            (void)run_production_test();
            fflush(stdout);
            sleep_ms(200);
            watchdog_reboot(0, 0, 0);
            while (true) {
                tight_loop_contents();
            }
        }

        if (strcmp(command, "PING") == 0) {
            printf("PONG\n");
#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
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

#if PRODUCTION_TEST_BOOT_TRIGGER_MODE
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
