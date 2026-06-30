#include "peripherals_adapter.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "ai_driver.h"
#include "g_config.h"
#include "log.h"

#define I2C_XFER_RETRY_COUNT 12
#define I2C_XFER_RETRY_DELAY_US 1000

static int s_i2c_fd = -1;
static int s_spi_fd = -1;
static uint32_t s_i2c_recovered_log_count = 0;

static int rpi5_open_device(const char *path, int flags)
{
    /*
     * Use openat through syscall so this Linux adapter always resolves device
     * file opens to the POSIX API.
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

static void i2c_log_recovered(const char *op, uint8_t addr, uint16_t reg, uint32_t attempt)
{
    if (attempt == 0 || s_i2c_recovered_log_count >= 12) {
        return;
    }
    ++s_i2c_recovered_log_count;
    printf("[I2C] %s recovered addr=0x%02x reg=0x%04x attempt=%lu\n",
           op,
           addr,
           reg,
           (unsigned long)(attempt + 1u));
}

static int configure_spi_device(void)
{
    uint8_t mode = RPI5_SPI_MODE;
    uint8_t bits = RPI5_SPI_BITS_PER_WORD;
    uint32_t speed = RPI5_SPI_SPEED_HZ;

    if (ioctl(s_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(s_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(s_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        printf("[SPI] configure %s failed: %s\n", RPI5_SPI_DEVICE, strerror(errno));
        return -1;
    }

    printf("SPI initialized device=%s mode=%u bits=%u speed=%lu Hz\n",
           RPI5_SPI_DEVICE,
           (unsigned)mode,
           (unsigned)bits,
           (unsigned long)speed);
    return 0;
}

bool init_peripherals(void)
{
    if (!init_i2c_peripheral()) {
        return false;
    }

    s_spi_fd = rpi5_open_device(RPI5_SPI_DEVICE, O_RDWR | O_CLOEXEC);
    if (s_spi_fd < 0) {
        printf("[SPI] open %s failed: %s\n", RPI5_SPI_DEVICE, strerror(errno));
        close_peripherals();
        return false;
    }
    if (configure_spi_device() < 0) {
        close_peripherals();
        return false;
    }

    return true;
}

bool init_i2c_peripheral(void)
{
    if (s_i2c_fd >= 0) {
        return true;
    }
    s_i2c_fd = rpi5_open_device(RPI5_I2C_DEVICE, O_RDWR | O_CLOEXEC);
    if (s_i2c_fd < 0) {
        printf("[I2C] open %s failed: %s\n", RPI5_I2C_DEVICE, strerror(errno));
        return false;
    }
    printf("I2C initialized device=%s addr=0x%02x\n", RPI5_I2C_DEVICE, RPI5_I2C_TARGET_ADDR);
    return true;
}

void close_peripherals(void)
{
    if (s_spi_fd >= 0) {
        close(s_spi_fd);
        s_spi_fd = -1;
    }
    if (s_i2c_fd >= 0) {
        close(s_i2c_fd);
        s_i2c_fd = -1;
    }
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

    printf("[I2C] write failed addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
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

    printf("[I2C] read failed addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
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
        printf("[I2C] unsupported write size=%lu\n", (unsigned long)mode);
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

    printf("[I2C] write-block failed addr=0x%02x reg=0x%04x len=%lu errno=%s\n",
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
        printf("[I2C] unsupported read size=%lu\n", (unsigned long)mode);
        ret = -1;
        break;
    }

    return ret;
}

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size)
{
    int ret = i2c_w(RPI5_I2C_TARGET_ADDR, addr, val, size);
    LOG_DEBUG("[pivariety_i2c_bridge_write], addr: 0x%X, val: 0x%X, size: %lu ec: %d\n",
              addr,
              val,
              (unsigned long)size,
              ret);
    return ret;
}

int pivariety_i2c_bridge_write_block(uint16_t addr, const uint8_t *data, uint32_t len)
{
    int ret = i2c_w_raw_blocking(RPI5_I2C_TARGET_ADDR, addr, data, len);
    LOG_DEBUG("[pivariety_i2c_bridge_write_block], addr: 0x%X, len: %lu ec: %d\n",
              addr,
              (unsigned long)len,
              ret);
    return ret;
}

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size)
{
    int ret = i2c_r(RPI5_I2C_TARGET_ADDR, addr, val, size);
    LOG_DEBUG("[pivariety_i2c_bridge_read], addr: 0x%X, val: 0x%X, size: %lu ec: %d\n",
              addr,
              val ? *val : 0,
              (unsigned long)size,
              ret);
    return ret;
}

static int spi_transfer_bytes(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (s_spi_fd < 0 || len == 0 || (!tx && !rx)) {
        return -1;
    }

    const uint32_t max_chunk = RPI5_SPI_MAX_TRANSFER_BYTES;
    const uint32_t transfer_count = (len + max_chunk - 1u) / max_chunk;
    struct spi_ioc_transfer *xfers =
        (struct spi_ioc_transfer *)calloc(transfer_count, sizeof(*xfers));
    uint8_t *zero_tx = NULL;

    if (!xfers) {
        return -1;
    }

    if (!tx) {
        zero_tx = (uint8_t *)calloc(max_chunk, 1);
        if (!zero_tx) {
            free(xfers);
            return -1;
        }
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < transfer_count; ++i) {
        uint32_t chunk = len - offset;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }

        xfers[i].tx_buf = (uintptr_t)(tx ? (tx + offset) : zero_tx);
        xfers[i].rx_buf = (uintptr_t)(rx ? (rx + offset) : NULL);
        xfers[i].len = chunk;
        xfers[i].speed_hz = RPI5_SPI_SPEED_HZ;
        xfers[i].delay_usecs = RPI5_SPI_DELAY_USECS;
        xfers[i].bits_per_word = RPI5_SPI_BITS_PER_WORD;

        offset += chunk;
    }

    int ret = ioctl(s_spi_fd, SPI_IOC_MESSAGE(transfer_count), xfers);
    free(zero_tx);
    free(xfers);

    if (ret < 1) {
        printf("[SPI] transfer failed len=%lu chunks=%lu errno=%s\n",
               (unsigned long)len,
               (unsigned long)transfer_count,
               strerror(errno));
        return -1;
    }

    return (int)len;
}

int pivariety_spi_bridge_write(uint8_t *data, uint32_t len)
{
    if (!data || len == 0) {
        return -1;
    }
    return spi_transfer_bytes(data, NULL, len);
}

int pivariety_spi_bridge_read(uint8_t *data, uint32_t len)
{
    if (!data || len == 0) {
        return -1;
    }
    return spi_transfer_bytes(NULL, data, len);
}

bool bind_i2c_peripherals_api(void)
{
    i2c_driver i2c_drv = {};
    i2c_drv.write = pivariety_i2c_bridge_write;
    i2c_drv.read = pivariety_i2c_bridge_read;
    i2c_drv.write_block = pivariety_i2c_bridge_write_block;
    i2c_drv.slp_ms = rpi5_sleep_ms;
    i2c_drv.slp_us = rpi5_sleep_us;
    register_i2c_driver(i2c_drv);
    return true;
}

bool bind_peripherals_api(void)
{
    bind_i2c_peripherals_api();

    spi_driver spi_drv = {};
    spi_drv.write = pivariety_spi_bridge_write;
    spi_drv.read = pivariety_spi_bridge_read;
    register_spi_driver(spi_drv);

    return true;
}
