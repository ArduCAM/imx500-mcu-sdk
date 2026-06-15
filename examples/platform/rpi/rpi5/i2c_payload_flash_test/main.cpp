#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ArducamIMX500SDK.h"
#include "generated_i2c_payload_assets/higherhrnet_fpk.h"
#include "generated_i2c_payload_assets/higherhrnet_network_info.h"
#include "g_config.h"
#include "peripherals_adapter.h"

#define MODEL_NAME          "higherhrnet"
#define MODEL_DATA          higherhrnet_fpk_data
#define MODEL_SIZE          higherhrnet_fpk_size
#define NNINFO_DATA         higherhrnet_network_info_data
#define NNINFO_SIZE         higherhrnet_network_info_size

static void print_usage(const char *argv0)
{
    std::printf("Usage: %s [status|reset|model-flash|nninfo-flash|model-direct|nninfo-direct|all-flash|all-direct|all|load-flash]\n",
                argv0);
}

static const char *payload_status_name(uint32_t status)
{
    switch (status) {
    case SPI_FLASH_OP_IDLE:
        return "IDLE";
    case SPI_FLASH_OP_WAIT_HEADER:
        return "WAIT_HEADER";
    case SPI_FLASH_OP_RECEIVING:
        return "RECEIVING";
    case SPI_FLASH_OP_PARSING:
        return "PARSING";
    case SPI_FLASH_OP_SUCCESS:
        return "SUCCESS";
    case SPI_FLASH_OP_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

static const char *payload_result_name(uint32_t result)
{
    switch (result) {
    case SPI_FLASH_RESULT_NONE:
        return "NONE";
    case SPI_FLASH_RESULT_OK:
        return "OK";
    case SPI_FLASH_RESULT_TIMEOUT:
        return "TIMEOUT";
    case SPI_FLASH_RESULT_BAD_HEADER:
        return "BAD_HEADER";
    case SPI_FLASH_RESULT_BAD_SIZE:
        return "BAD_SIZE";
    case SPI_FLASH_RESULT_WRITE_FAIL:
        return "WRITE_FAIL";
    case SPI_FLASH_RESULT_CRC_MISMATCH:
        return "CRC_MISMATCH";
    case SPI_FLASH_RESULT_PARSE_FAIL:
        return "PARSE_FAIL";
    case SPI_FLASH_RESULT_FLASH_BLOB_MISSING:
        return "FLASH_BLOB_MISSING";
    case SPI_FLASH_RESULT_BUSY:
        return "BUSY";
    case SPI_FLASH_RESULT_BAD_OPERATION:
        return "BAD_OPERATION";
    case SPI_FLASH_RESULT_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
    case SPI_FLASH_RESULT_NO_MEMORY:
        return "NO_MEMORY";
    case SPI_FLASH_RESULT_IMX500_DOWNLOAD_FAIL:
        return "IMX500_DOWNLOAD_FAIL";
    default:
        return "UNKNOWN";
    }
}

static const char *payload_op_name(uint32_t op)
{
    switch (op) {
    case I2C_PAYLOAD_OP_ABORT:
        return "ABORT/IDLE";
    case I2C_PAYLOAD_OP_MODEL_TO_FLASH:
        return "MODEL_TO_FLASH";
    case I2C_PAYLOAD_OP_NN_INFO_TO_FLASH:
        return "NN_INFO_TO_FLASH";
    case I2C_PAYLOAD_OP_NN_INFO_TO_MEMORY:
        return "NN_INFO_TO_MEMORY";
    case I2C_PAYLOAD_OP_MODEL_TO_MEMORY:
        return "MODEL_TO_MEMORY";
    default:
        return "UNKNOWN";
    }
}

static bool dump_payload_status(const char *label)
{
    spi_flash_status_t status = {};
    if (!get_spi_flash_status(&status)) {
        std::printf("%s failed to read payload status\n", label);
        return false;
    }
    std::printf("%s status=%lu(%s) result=%lu(%s) bytes=%lu/%lu\n",
                label,
                (unsigned long)status.status,
                payload_status_name(status.status),
                (unsigned long)status.result,
                payload_result_name(status.result),
                (unsigned long)status.bytes_done,
                (unsigned long)status.bytes_total);
    return true;
}

static bool payload_status_is_terminal(uint32_t status)
{
    return status == SPI_FLASH_OP_IDLE ||
           status == SPI_FLASH_OP_SUCCESS ||
           status == SPI_FLASH_OP_FAILED;
}

static bool abort_stale_payload_if_needed(const char *stage)
{
    spi_flash_status_t status = {};
    if (!get_spi_flash_status(&status)) {
        std::printf("read payload status failed before stale-op cleanup (%s)\n",
                    stage);
        return false;
    }
    if (payload_status_is_terminal(status.status)) {
        return true;
    }

    std::printf("Abort stale I2C payload before %s: status=%lu(%s) result=%lu(%s) bytes=%lu/%lu\n",
                stage,
                (unsigned long)status.status,
                payload_status_name(status.status),
                (unsigned long)status.result,
                payload_result_name(status.result),
                (unsigned long)status.bytes_done,
                (unsigned long)status.bytes_total);
    if (!abort_i2c_payload_operation()) {
        std::printf("abort stale I2C payload failed before %s\n", stage);
        return false;
    }
    return true;
}

static bool read_reg(uint16_t reg, uint32_t *value, const char *name = nullptr)
{
    int ret = pivariety_i2c_bridge_read(reg, value, 4);
    if (ret < 0) {
        std::printf("[I2C READ] failed reg=0x%04x%s%s ret=%d\n",
                    reg,
                    name ? " " : "",
                    name ? name : "",
                    ret);
        return false;
    }
    return true;
}

static void dump_module_snapshot(const char *label)
{
    uint32_t device_id = 0;
    uint32_t fw_version = 0;
    uint32_t boot = 0;
    uint32_t op = 0;
    uint32_t max_write = 0;
    uint32_t accepted = 0;

    std::printf("\n---- %s ----\n", label);
    std::printf("i2c_device=%s target_addr=0x%02x\n",
                peripherals_i2c_device_path(),
                I2C_PAYLOAD_I2C_TARGET_ADDR);
    std::printf("assets: model=%s model_size=%lu bytes nninfo_size=%lu bytes\n",
                MODEL_NAME,
                (unsigned long)MODEL_SIZE,
                (unsigned long)NNINFO_SIZE);

    if (read_reg(DEVICE_ID_REG, &device_id, "DEVICE_ID_REG")) {
        std::printf("device_id=0x%08lx\n", (unsigned long)device_id);
    }
    if (read_reg(DEVICE_VERSION_REG, &fw_version, "DEVICE_VERSION_REG")) {
        std::printf("firmware_version=0x%08lx\n", (unsigned long)fw_version);
    }
    if (read_reg(BOOT_STATUS_REG, &boot, "BOOT_STATUS_REG")) {
        std::printf("boot_status=%lu\n", (unsigned long)boot);
    }
    if (read_reg(I2C_PAYLOAD_OP_REG, &op, "I2C_PAYLOAD_OP_REG")) {
        std::printf("i2c_payload_op=%lu(%s)\n",
                    (unsigned long)op,
                    payload_op_name(op));
    }
    if (read_reg(I2C_PAYLOAD_MAX_WRITE_REG,
                 &max_write,
                 "I2C_PAYLOAD_MAX_WRITE_REG")) {
        std::printf("i2c_payload_max_write=%lu\n", (unsigned long)max_write);
    }
    if (read_reg(I2C_PAYLOAD_ACCEPTED_REG,
                 &accepted,
                 "I2C_PAYLOAD_ACCEPTED_REG")) {
        std::printf("i2c_payload_last_accepted=%lu\n",
                    (unsigned long)accepted);
    }
    dump_payload_status("[PAYLOAD]");
    std::printf("--------------------\n\n");
}

static bool wait_boot_status(uint32_t target, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        uint32_t boot = 0;
        if (!read_reg(BOOT_STATUS_REG, &boot, "BOOT_STATUS_REG")) {
            return false;
        }
        if (boot >= target) {
            return true;
        }
        std::printf("wait boot_status >= %lu, current=%lu\n",
                    (unsigned long)target,
                    (unsigned long)boot);
        g_i2c_driver.slp_ms(500);
        elapsed += 500;
    }
    std::printf("wait boot_status >= %lu timeout after %lu ms\n",
                (unsigned long)target,
                (unsigned long)timeout_ms);
    dump_module_snapshot("timeout snapshot");
    return false;
}

static bool request_load_flash(void)
{
    std::printf("Request model/nninfo load from module flash...\n");
    if (pivariety_i2c_bridge_write(LOAD_MODEL_FROM_FLASH, 1, 4) < 0) {
        std::printf("request LOAD_MODEL_FROM_FLASH failed\n");
        return false;
    }
    if (!wait_boot_status(2, 30000)) {
        std::printf("load flash timeout\n");
        return false;
    }
    std::printf("load flash completed\n");
    return true;
}

static bool reset_module_for_direct_load(const char *reason)
{
    std::printf("Reset module through SDK before %s...\n", reason);
    if (!abort_stale_payload_if_needed("module reset")) {
        dump_module_snapshot("stale payload abort failed before reset");
        return false;
    }
    if (!reset_imx500_module()) {
        std::printf("SDK reset failed before %s\n", reason);
        dump_module_snapshot("reset failed");
        return false;
    }
    if (!abort_stale_payload_if_needed("direct transfer")) {
        dump_module_snapshot("stale payload abort failed after reset");
        return false;
    }
    dump_module_snapshot("after reset");
    return true;
}

static bool run_action(const char *action)
{
    if (std::strcmp(action, "status") == 0) {
        dump_module_snapshot("status");
        return true;
    }

    if (std::strcmp(action, "reset") == 0) {
        return reset_module_for_direct_load("manual reset");
    }

    if (std::strcmp(action, "model-flash") == 0) {
        dump_module_snapshot("before model-flash");
        std::printf("Write model to module flash over I2C, size=%lu\n",
                    (unsigned long)MODEL_SIZE);
        bool ok = write_model_to_cam_flash_i2c(MODEL_DATA, MODEL_SIZE);
        dump_payload_status("[MODEL FLASH I2C]");
        if (!ok) {
            dump_module_snapshot("model-flash failed");
        }
        return ok;
    }

    if (std::strcmp(action, "nninfo-flash") == 0) {
        dump_module_snapshot("before nninfo-flash");
        std::printf("Write nninfo to module flash over I2C, size=%lu\n",
                    (unsigned long)NNINFO_SIZE);
        bool ok = write_nn_info_to_cam_flash_i2c(NNINFO_DATA,
                                                 NNINFO_SIZE);
        dump_payload_status("[NNINFO FLASH I2C]");
        if (!ok) {
            dump_module_snapshot("nninfo-flash failed");
        }
        return ok;
    }

    if (std::strcmp(action, "nninfo-direct") == 0) {
        dump_module_snapshot("before nninfo-direct");
        std::printf("Direct-load nninfo to module memory over I2C, size=%lu\n",
                    (unsigned long)NNINFO_SIZE);
        bool ok = load_nn_info_to_cam_memory_i2c(NNINFO_DATA,
                                                 NNINFO_SIZE);
        dump_payload_status("[NNINFO DIRECT I2C]");
        if (ok) {
            load_nn_info_to_sdk_cache(NNINFO_DATA, NNINFO_SIZE);
            dump_network_info_list();
        } else {
            dump_module_snapshot("nninfo-direct failed");
        }
        return ok;
    }

    if (std::strcmp(action, "model-direct") == 0) {
        dump_module_snapshot("before model-direct");
        if (!reset_module_for_direct_load("model-direct")) {
            return false;
        }
        std::printf("Direct-load model to IMX500 over I2C, size=%lu\n",
                    (unsigned long)MODEL_SIZE);
        bool ok = load_model_to_cam_memory_i2c(MODEL_DATA, MODEL_SIZE);
        dump_payload_status("[MODEL DIRECT I2C]");
        if (!ok) {
            dump_module_snapshot("model-direct failed");
        }
        return ok;
    }

    if (std::strcmp(action, "all-flash") == 0) {
        return run_action("model-flash") && run_action("nninfo-flash");
    }

    if (std::strcmp(action, "all-direct") == 0) {
        return run_action("model-direct") && run_action("nninfo-direct");
    }

    if (std::strcmp(action, "all") == 0) {
        return run_action("model-flash") &&
               run_action("nninfo-flash") &&
               run_action("model-direct") &&
               run_action("nninfo-direct");
    }

    if (std::strcmp(action, "load-flash") == 0) {
        return request_load_flash();
    }

    return false;
}

int main(int argc, char **argv)
{
    const char *action = argc > 1 ? argv[1] : "all-flash";

    if (std::strcmp(action, "--help") == 0 || std::strcmp(action, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (!init_i2c_peripheral()) {
        return 1;
    }
    std::printf("Using I2C device: %s\n", peripherals_i2c_device_path());
    bind_i2c_peripherals_api();
    dump_module_snapshot("initial module state");
    std::printf("Requested action: %s\n", action);

    bool ok = run_action(action);
    if (!ok) {
        std::printf("Action failed: %s\n", action);
        print_usage(argv[0]);
        close_peripherals();
        return 1;
    }

    dump_module_snapshot("final module state");
    std::printf("Action completed: %s\n", action);
    close_peripherals();
    return 0;
}
