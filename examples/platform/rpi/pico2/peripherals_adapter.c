#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "peripherals_adapter.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "g_config.h"
#include "ArducamIMX500SDK.h"
#include "log.h"

#define PIVARIETY_ADDR 0x0C
#define I2C_XFER_TIMEOUT_US (50 * 1000)
#define I2C_XFER_RETRY_COUNT 12
#define I2C_XFER_RETRY_DELAY_US 1000
#define SPI_CS_SETUP_US 20
#define SPI_CS_HOLD_US 20
#define SPI_POST_XFER_US 50

static uint32_t s_i2c_recovered_log_count = 0;

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
    printf("[I2C] %s recovered addr=0x%02x reg=0x%04x attempt=%lu\n",
           op,
           addr,
           reg,
           (unsigned long)(attempt + 1u));
}

int32_t i2c_w_blocking(const uint8_t addr,
                        uint8_t *reg,
                        const uint8_t reg_num,
                        uint8_t *buf,
                        const uint8_t nbytes)
{
    if (reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t msg[6];
    if ((uint32_t)reg_num + (uint32_t)nbytes > sizeof(msg)) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1 - i] = reg[i];
    }

    for (uint8_t i = 0; i < nbytes; i++) {
        msg[reg_num + i] = buf[nbytes - 1 - i];
    }

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    const uint32_t msg_len = (uint32_t)reg_num + (uint32_t)nbytes;
    int32_t last_ret = -1;

    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        int32_t ret = i2c_write_timeout_us(
            I2C_HW_ADDR,
            addr,
            msg,
            msg_len,
            false,
            I2C_XFER_TIMEOUT_US
        );
        if (ret == (int32_t)msg_len) {
            i2c_log_recovered("write", addr, reg_value, attempt);
            return ret;
        }
        last_ret = ret;
        sleep_us(I2C_XFER_RETRY_DELAY_US * (attempt + 1u));
    }

    printf("[I2C] write failed addr=0x%02x reg=0x%04x len=%lu ret=%ld\n",
           addr,
           reg_value,
           (unsigned long)nbytes,
           (long)last_ret);
    return -1;
}

int32_t i2c_r_blocking(const uint8_t addr,
                        uint8_t *reg,
                        const uint8_t reg_num,
                        uint8_t *buf,
                        const uint8_t nbytes)
{
    if (reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t msg[2];
    if (reg_num > sizeof(msg)) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1 - i] = reg[i];
    }

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    int32_t last_ret = -1;

    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        int32_t ret = i2c_write_timeout_us(
            I2C_HW_ADDR,
            addr,
            msg,
            reg_num,
            true,
            I2C_XFER_TIMEOUT_US
        );
        if (ret == (int32_t)reg_num) {
            ret = i2c_read_timeout_us(
                I2C_HW_ADDR,
                addr,
                buf,
                nbytes,
                false,
                I2C_XFER_TIMEOUT_US
            );
            if (ret == (int32_t)nbytes) {
                i2c_log_recovered("read", addr, reg_value, attempt);
                return ret;
            }
        }
        last_ret = ret;
        sleep_us(I2C_XFER_RETRY_DELAY_US * (attempt + 1u));
    }

    printf("[I2C] read failed addr=0x%02x reg=0x%04x len=%lu ret=%ld\n",
           addr,
           reg_value,
           (unsigned long)nbytes,
           (long)last_ret);
    return -1;
}

int32_t i2c_w(uint8_t addr,
               uint16_t reg,
               uint32_t data,
               uint32_t mode)
{
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    uint8_t buf[4];
    buf[0] = (uint8_t)(data & 0xFF);
    buf[1] = (uint8_t)((data >> 8) & 0xFF);
    buf[2] = (uint8_t)((data >> 16) & 0xFF);
    buf[3] = (uint8_t)((data >> 24) & 0xFF);

    int32_t ret;

    switch (mode) {
    case 1:
        ret = i2c_w_blocking(addr, reg_buf, 2, buf, 1);
        break;
    case 2:
        ret = i2c_w_blocking(addr, reg_buf, 2, buf, 2);
        break;
    case 4:
        ret = i2c_w_blocking(addr, reg_buf, 2, buf, 4);
        break;
    default:
        printf("[Error] Unknown I2C mode\n");
        ret = -1;
        break;
    }

    return ret;
}

int32_t i2c_r(const uint8_t addr,
               uint16_t reg,
               uint32_t *data,
               uint32_t mode)
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
        *data = ((uint32_t)buf[0] << 8) |
                ((uint32_t)buf[1]);
        break;

    case 4:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 4);
        *data = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8)  |
                ((uint32_t)buf[3]);
        break;

    default:
        printf("[Error] Unknown I2C mode\n");
        ret = -1;
        break;
    }

    return ret;
}

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size) {
  int ret = i2c_w(PIVARIETY_ADDR, addr, val, size);
  LOG_DEBUG("[pivariety_i2c_bridge_write], addr: 0x%X, val: 0x%X, size: %d ec: %d\n", addr, val, size, ret);
  return ret;
}

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t* val, uint32_t size) {
  int ret = i2c_r(PIVARIETY_ADDR, addr, val, size);
  LOG_DEBUG("[pivariety_i2c_bridge_read], addr: 0x%X, val: 0x%X, size: %d ec: %d\n", addr, *val, size, ret);
  return ret;
}

int pivariety_spi_bridge_write(uint8_t *data, uint32_t len) {
  if (!data || len == 0) {
      return -1;
  }
  gpio_put(SPI_CSN_PIN, 0);
  sleep_us(SPI_CS_SETUP_US);
  int ret = spi_write_blocking(SPI_HW_ADDR, data, len);
  sleep_us(SPI_CS_HOLD_US);
  gpio_put(SPI_CSN_PIN, 1);
  sleep_us(SPI_POST_XFER_US);
  if (ret < 0 || (uint32_t)ret != len) {
      printf("[SPI] write failed len=%lu ret=%d\n",
             (unsigned long)len,
             ret);
      return -1;
  }
  return ret;
}

int pivariety_spi_bridge_read(uint8_t *data, uint32_t len) {
    if (!data || len == 0) {
        return -1;
    }
    gpio_put(SPI_CSN_PIN, 0);
    sleep_us(SPI_CS_SETUP_US);
    spi_read_blocking(SPI_HW_ADDR, 0x00, data, len);
    sleep_us(SPI_CS_HOLD_US);
    gpio_put(SPI_CSN_PIN, 1);
    sleep_us(SPI_POST_XFER_US);

    return len;
}

bool bind_peripherals_api() {
  i2c_driver i2c_drv;
  i2c_drv.write = pivariety_i2c_bridge_write;
  i2c_drv.read = pivariety_i2c_bridge_read;
  i2c_drv.slp_ms = sleep_ms;
  i2c_drv.slp_us = sleep_us;
  register_i2c_driver(i2c_drv);
  spi_driver spi_drv;
  spi_drv.write = pivariety_spi_bridge_write;
  spi_drv.read = pivariety_spi_bridge_read;
  register_spi_driver(spi_drv);
  return true;
}
