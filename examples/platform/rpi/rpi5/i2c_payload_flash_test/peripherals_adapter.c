#include "peripherals_adapter.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "ai_driver.h"
#include "common_regs.h"
#include "g_config.h"

#define I2C_XFER_RETRY_COUNT 12
#define I2C_XFER_RETRY_DELAY_US 1000

static int s_i2c_fd = -1;
static char s_i2c_device_path[64];
static uint32_t s_i2c_recovered_log_count = 0;

static int rpi5_open_device(const char *path, int flags)
{
    /*
     * The SDK exports a public function named open(...). Use openat through
     * syscall so this Linux adapter cannot resolve to the SDK symbol.
     */
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags, 0);
}

static void rpi5_sleep_us(uint64_t us)
{
    struct timespec req;
    req.tv_sec = (time_t)(us / 1000000u);
    req.tv_nsec = (long)((us % 1000000u) * 1000u);

    while (nanosleep(&req, &req) < 0 && errno == EINTR) {
    }
}

static void rpi5_sleep_ms(uint32_t ms)
{
    rpi5_sleep_us((uint64_t)ms * 1000u);
}

static uint16_t i2c_reg_value(const uint8_t *reg, uint8_t reg_num)
{
    uint16_t value = 0;
    for (uint8_t i = 0; i < reg_num && i < 2; ++i) {
        value |= (uint16_t)reg[i] << (8u * i);
    }
    return value;
}

static void i2c_log_recovered(const char *op,
                              uint8_t addr,
                              uint16_t reg,
                              uint32_t attempt)
{
    if (attempt == 0 || s_i2c_recovered_log_count >= 12) {
        return;
    }
    ++s_i2c_recovered_log_count;
    printf("[CSI-I2C] %s recovered addr=0x%02x reg=0x%04x attempt=%lu\n",
           op,
           addr,
           reg,
           (unsigned long)(attempt + 1u));
}

static int32_t i2c_w_blocking(uint8_t addr,
                              uint8_t *reg,
                              uint8_t reg_num,
                              uint8_t *buf,
                              uint8_t nbytes)
{
    if (s_i2c_fd < 0 || reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t msg[6];
    if ((uint32_t)reg_num + (uint32_t)nbytes > sizeof(msg)) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1u - i] = reg[i];
    }
    for (uint8_t i = 0; i < nbytes; i++) {
        msg[reg_num + i] = buf[nbytes - 1u - i];
    }

    struct i2c_msg i2c_msg = {
        .addr = addr,
        .flags = 0,
        .len = (uint16_t)(reg_num + nbytes),
        .buf = msg,
    };
    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs = &i2c_msg,
        .nmsgs = 1,
    };

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    int last_errno = 0;

    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        int ret = ioctl(s_i2c_fd, I2C_RDWR, &ioctl_data);
        if (ret == 1) {
            i2c_log_recovered("write", addr, reg_value, attempt);
            return i2c_msg.len;
        }
        last_errno = errno;
        rpi5_sleep_us(I2C_XFER_RETRY_DELAY_US * (attempt + 1u));
    }

    printf("[CSI-I2C] write failed dev=%s addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
           s_i2c_device_path,
           addr,
           reg_value,
           (unsigned long)nbytes,
           strerror(last_errno));
    return -1;
}

static int32_t i2c_r_blocking(uint8_t addr,
                              uint8_t *reg,
                              uint8_t reg_num,
                              uint8_t *buf,
                              uint8_t nbytes)
{
    if (s_i2c_fd < 0 || reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t reg_msg[2];
    if (reg_num > sizeof(reg_msg)) {
        return -1;
    }
    for (uint8_t i = 0; i < reg_num; i++) {
        reg_msg[reg_num - 1u - i] = reg[i];
    }

    struct i2c_msg msgs[2] = {
        {
            .addr = addr,
            .flags = 0,
            .len = reg_num,
            .buf = reg_msg,
        },
        {
            .addr = addr,
            .flags = I2C_M_RD,
            .len = nbytes,
            .buf = buf,
        },
    };
    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs = msgs,
        .nmsgs = 2,
    };

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    int last_errno = 0;

    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        int ret = ioctl(s_i2c_fd, I2C_RDWR, &ioctl_data);
        if (ret == 2) {
            i2c_log_recovered("read", addr, reg_value, attempt);
            return nbytes;
        }
        last_errno = errno;
        rpi5_sleep_us(I2C_XFER_RETRY_DELAY_US * (attempt + 1u));
    }

    printf("[CSI-I2C] read failed dev=%s addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
           s_i2c_device_path,
           addr,
           reg_value,
           (unsigned long)nbytes,
           strerror(last_errno));
    return -1;
}

static int32_t i2c_w(uint8_t addr, uint16_t reg, uint32_t data, uint32_t mode)
{
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    uint8_t buf[4];
    buf[0] = (uint8_t)(data & 0xFF);
    buf[1] = (uint8_t)((data >> 8) & 0xFF);
    buf[2] = (uint8_t)((data >> 16) & 0xFF);
    buf[3] = (uint8_t)((data >> 24) & 0xFF);

    switch (mode) {
    case 1:
        return i2c_w_blocking(addr, reg_buf, 2, buf, 1);
    case 2:
        return i2c_w_blocking(addr, reg_buf, 2, buf, 2);
    case 4:
        return i2c_w_blocking(addr, reg_buf, 2, buf, 4);
    default:
        printf("[CSI-I2C] unsupported write size=%lu\n", (unsigned long)mode);
        return -1;
    }
}

static int32_t i2c_w_raw_blocking(uint8_t addr,
                                  uint16_t reg,
                                  const uint8_t *buf,
                                  uint32_t nbytes)
{
    if (s_i2c_fd < 0 || !buf || nbytes == 0) {
        return -1;
    }

    uint8_t *msg = (uint8_t *)malloc(nbytes + 2u);
    if (!msg) {
        return -1;
    }
    msg[0] = (uint8_t)((reg >> 8) & 0xFF);
    msg[1] = (uint8_t)(reg & 0xFF);
    memcpy(msg + 2, buf, nbytes);

    struct i2c_msg i2c_msg = {
        .addr = addr,
        .flags = 0,
        .len = (uint16_t)(nbytes + 2u),
        .buf = msg,
    };
    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs = &i2c_msg,
        .nmsgs = 1,
    };

    int last_errno = 0;
    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        int ret = ioctl(s_i2c_fd, I2C_RDWR, &ioctl_data);
        if (ret == 1) {
            i2c_log_recovered("write-block", addr, reg, attempt);
            free(msg);
            return (int32_t)nbytes;
        }
        last_errno = errno;
        rpi5_sleep_us(I2C_XFER_RETRY_DELAY_US * (attempt + 1u));
    }

    printf("[CSI-I2C] write-block failed dev=%s addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
           s_i2c_device_path,
           addr,
           reg,
           (unsigned long)nbytes,
           strerror(last_errno));
    free(msg);
    return -1;
}

static int32_t i2c_r(uint8_t addr, uint16_t reg, uint32_t *data, uint32_t mode)
{
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    uint8_t buf[4] = {0};
    int32_t ret;

    switch (mode) {
    case 1:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 1);
        *data = (uint32_t)buf[0];
        break;
    case 2:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 2);
        *data = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        break;
    case 4:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 4);
        *data = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
        break;
    default:
        printf("[CSI-I2C] unsupported read size=%lu\n", (unsigned long)mode);
        ret = -1;
        break;
    }

    return ret;
}

static bool probe_current_i2c_device(void)
{
    uint32_t version = 0;
    uint32_t device_id = 0;
    if (i2c_r(I2C_PAYLOAD_I2C_TARGET_ADDR, DEVICE_VERSION_REG, &version, 4) < 0) {
        printf("[CSI-I2C] probe failed dev=%s addr=0x%02x reg=DEVICE_VERSION_REG\n",
               s_i2c_device_path,
               I2C_PAYLOAD_I2C_TARGET_ADDR);
        return false;
    }
    if (i2c_r(I2C_PAYLOAD_I2C_TARGET_ADDR, DEVICE_ID_REG, &device_id, 4) < 0) {
        printf("[CSI-I2C] probe failed dev=%s addr=0x%02x reg=DEVICE_ID_REG\n",
               s_i2c_device_path,
               I2C_PAYLOAD_I2C_TARGET_ADDR);
        return false;
    }
    printf("[CSI-I2C] found module dev=%s addr=0x%02x device_id=0x%08lx version=0x%08lx\n",
           s_i2c_device_path,
           I2C_PAYLOAD_I2C_TARGET_ADDR,
           (unsigned long)device_id,
           (unsigned long)version);
    return true;
}

static bool try_open_i2c_device(const char *path, bool require_probe)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    printf("[CSI-I2C] try device %s addr=0x%02x\n",
           path,
           I2C_PAYLOAD_I2C_TARGET_ADDR);
    int fd = rpi5_open_device(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("[CSI-I2C] open %s failed: %s\n", path, strerror(errno));
        return false;
    }

    s_i2c_fd = fd;
    snprintf(s_i2c_device_path, sizeof(s_i2c_device_path), "%s", path);
    if (!require_probe) {
        printf("[CSI-I2C] opened configured device %s, probing module...\n",
               s_i2c_device_path);
        return true;
    }
    printf("[CSI-I2C] opened %s, probing module...\n", s_i2c_device_path);
    if (probe_current_i2c_device()) {
        return true;
    }

    printf("[CSI-I2C] no module response on %s, trying next candidate\n",
           s_i2c_device_path);
    close(s_i2c_fd);
    s_i2c_fd = -1;
    s_i2c_device_path[0] = '\0';
    return false;
}

bool init_i2c_peripheral(void)
{
    if (s_i2c_fd >= 0) {
        return true;
    }

    if (I2C_PAYLOAD_I2C_DEVICE[0] != '\0') {
        if (!try_open_i2c_device(I2C_PAYLOAD_I2C_DEVICE, false)) {
            printf("[CSI-I2C] open configured device %s failed: %s\n",
                   I2C_PAYLOAD_I2C_DEVICE,
                   strerror(errno));
            return false;
        }
        if (!probe_current_i2c_device()) {
            printf("[CSI-I2C] configured device %s opened, but module addr=0x%02x did not respond\n",
                   I2C_PAYLOAD_I2C_DEVICE,
                   I2C_PAYLOAD_I2C_TARGET_ADDR);
            close_peripherals();
            return false;
        }
        return true;
    }

    printf("[CSI-I2C] auto scan candidates: %s\n", I2C_PAYLOAD_I2C_CANDIDATES);
    char candidates[512];
    snprintf(candidates, sizeof(candidates), "%s", I2C_PAYLOAD_I2C_CANDIDATES);

    char *save = NULL;
    for (char *path = strtok_r(candidates, ":", &save);
         path != NULL;
         path = strtok_r(NULL, ":", &save)) {
        if (try_open_i2c_device(path, true)) {
            return true;
        }
    }

    printf("[CSI-I2C] no module found at addr=0x%02x on I2C candidates: %s\n",
           I2C_PAYLOAD_I2C_TARGET_ADDR,
           I2C_PAYLOAD_I2C_CANDIDATES);
    printf("[CSI-I2C] specify the bus with: cmake -S . -B build -DI2C_PAYLOAD_I2C_DEVICE=/dev/i2c-X\n");
    return false;
}

void close_peripherals(void)
{
    if (s_i2c_fd >= 0) {
        close(s_i2c_fd);
        s_i2c_fd = -1;
    }
    s_i2c_device_path[0] = '\0';
}

const char *peripherals_i2c_device_path(void)
{
    return s_i2c_device_path;
}

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size)
{
    return i2c_w(I2C_PAYLOAD_I2C_TARGET_ADDR, addr, val, size);
}

int pivariety_i2c_bridge_write_block(uint16_t addr,
                                     const uint8_t *data,
                                     uint32_t len)
{
    return i2c_w_raw_blocking(I2C_PAYLOAD_I2C_TARGET_ADDR, addr, data, len);
}

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size)
{
    return i2c_r(I2C_PAYLOAD_I2C_TARGET_ADDR, addr, val, size);
}

bool bind_i2c_peripherals_api(void)
{
    i2c_driver i2c_drv = {0};
    i2c_drv.write = pivariety_i2c_bridge_write;
    i2c_drv.read = pivariety_i2c_bridge_read;
    i2c_drv.write_block = pivariety_i2c_bridge_write_block;
    i2c_drv.slp_ms = rpi5_sleep_ms;
    i2c_drv.slp_us = rpi5_sleep_us;
    register_i2c_driver(i2c_drv);
    return true;
}
