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

int32_t i2c_w_blocking(const uint8_t addr,
                        uint8_t *reg,
                        const uint8_t reg_num,
                        uint8_t *buf,
                        const uint8_t nbytes)
{
    if (reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t *msg = (uint8_t *)malloc((nbytes + reg_num) * sizeof(uint8_t));
    if (msg == NULL) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1 - i] = reg[i];
    }

    for (uint8_t i = 0; i < nbytes; i++) {
        msg[reg_num + i] = buf[nbytes - 1 - i];
    }

    int32_t ret = i2c_write_blocking(
        I2C_HW_ADDR,
        addr,
        msg,
        reg_num + nbytes,
        false
    );

    free(msg);
    return ret;
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

    uint8_t *msg = (uint8_t *)malloc(reg_num * sizeof(uint8_t));
    if (msg == NULL) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1 - i] = reg[i];
    }

    i2c_write_blocking(I2C_HW_ADDR, addr, msg, reg_num, true);
    int32_t ret = i2c_read_blocking(I2C_HW_ADDR, addr, buf, nbytes, false);

    free(msg);
    return ret;
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
  int ret = spi_write_blocking(SPI_HW_ADDR, data, len);
  gpio_put(SPI_CSN_PIN, 1);
  if (ret < 0 || (uint32_t)ret != len) {
      return -1;
  }
  return ret;
}

int pivariety_spi_bridge_read(uint8_t *data, uint32_t len) {
    if (!data || len == 0) {
        return -1;
    }
    gpio_put(SPI_CSN_PIN, 0);
    spi_read_blocking(SPI_HW_ADDR, 0x00, data, len);
    gpio_put(SPI_CSN_PIN, 1);

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
