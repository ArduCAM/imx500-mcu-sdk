#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ai_driver.h"
#include "g_config.h"
#include "log.h"
#include "mphalport.h"
#include "nrf_gpio.h"
#include "nrfx_spim.h"
#include "nrfx_twi.h"
#include "peripherals_adapter.h"

#define PIVARIETY_ADDR 0x0C
#define I2C_XFER_RETRY_COUNT 12
#define I2C_XFER_RETRY_DELAY_MS 1
#define SPI_CS_SETUP_US 20
#define SPI_CS_HOLD_US 20
#define SPI_POST_XFER_US 50

static nrfx_twi_t s_twi = NRFX_TWI_INSTANCE(IMX500_NRF_TWI_INSTANCE);
static nrfx_spim_t s_spim = NRFX_SPIM_INSTANCE(IMX500_NRF_SPIM_INSTANCE);
static uint32_t s_i2c_recovered_log_count = 0;

static uint16_t i2c_reg_value(const uint8_t *reg, uint8_t reg_num) {
    uint16_t value = 0;
    for (uint8_t i = 0; i < reg_num && i < 2; ++i) {
        value |= (uint16_t)reg[i] << (8u * i);
    }
    return value;
}

static void i2c_log_recovered(const char *op, uint8_t addr, uint16_t reg, uint32_t attempt) {
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

static nrf_twi_frequency_t nrf_twi_frequency_from_hz(uint32_t baudrate) {
    if (baudrate <= 150000u) {
        return NRF_TWI_FREQ_100K;
    }
    if (baudrate < 320000u) {
        return NRF_TWI_FREQ_250K;
    }
    return NRF_TWI_FREQ_400K;
}

static uint32_t nrf_spim_frequency_from_hz(uint32_t baudrate) {
    if (baudrate <= 125000u) {
        return 125000u;
    }
    if (baudrate <= 250000u) {
        return 250000u;
    }
    if (baudrate <= 500000u) {
        return 500000u;
    }
    if (baudrate <= 1000000u) {
        return 1000000u;
    }
    if (baudrate <= 2000000u) {
        return 2000000u;
    }
    if (baudrate <= 4000000u) {
        return 4000000u;
    }
    return 8000000u;
}

bool imx500_micropython_platform_hardware_init(uint32_t i2c_baudrate, uint32_t spi_baudrate) {
    if (nrfx_twi_init_check(&s_twi)) {
        nrfx_twi_uninit(&s_twi);
    }
    nrfx_twi_config_t twi_config;
    memset(&twi_config, 0, sizeof(twi_config));
    twi_config.scl = I2C_SCL_PIN;
    twi_config.sda = I2C_SDA_PIN;
    twi_config.frequency = nrf_twi_frequency_from_hz(i2c_baudrate);
    twi_config.interrupt_priority = 6;
    twi_config.hold_bus_uninit = false;
    nrfx_err_t ret = nrfx_twi_init(&s_twi, &twi_config, NULL, NULL);
    if (ret != NRFX_SUCCESS && ret != NRFX_ERROR_ALREADY && ret != NRFX_ERROR_INVALID_STATE) {
        printf("[I2C] nrfx_twi_init failed: %ld\n", (long)ret);
        return false;
    }
    nrfx_twi_enable(&s_twi);

    if (nrfx_spim_init_check(&s_spim)) {
        nrfx_spim_uninit(&s_spim);
    }
    nrfx_spim_config_t spim_config;
    memset(&spim_config, 0, sizeof(spim_config));
    spim_config.sck_pin = SPI_SCK_PIN;
    spim_config.mosi_pin = SPI_TX_PIN;
    spim_config.miso_pin = SPI_RX_PIN;
    spim_config.ss_pin = NRF_SPIM_PIN_NOT_CONNECTED;
    spim_config.ss_active_high = false;
    spim_config.irq_priority = 6;
    spim_config.orc = 0xFF;
    spim_config.frequency = nrf_spim_frequency_from_hz(spi_baudrate);
    spim_config.mode = NRF_SPIM_MODE_3;
    spim_config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;
    spim_config.miso_pull = NRF_GPIO_PIN_PULLDOWN;
    ret = nrfx_spim_init(&s_spim, &spim_config, NULL, NULL);
    if (ret != NRFX_SUCCESS && ret != NRFX_ERROR_ALREADY && ret != NRFX_ERROR_INVALID_STATE) {
        printf("[SPI] nrfx_spim_init failed: %ld\n", (long)ret);
        return false;
    }

    nrf_gpio_pin_set(SPI_CSN_PIN);
    nrf_gpio_cfg_output(SPI_CSN_PIN);
    return bind_peripherals_api();
}

void imx500_micropython_platform_sleep_ms(uint32_t ms) {
    mp_hal_delay_ms(ms);
}

static void imx500_sleep_us(uint64_t us) {
    while (us > 0) {
        uint32_t chunk = us > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)us;
        mp_hal_delay_us(chunk);
        us -= chunk;
    }
}

static int32_t i2c_w_blocking(const uint8_t addr,
                              uint8_t *reg,
                              const uint8_t reg_num,
                              uint8_t *buf,
                              const uint8_t nbytes) {
    if (reg_num < 1 || nbytes < 1) {
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

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    const size_t msg_len = (size_t)reg_num + (size_t)nbytes;
    nrfx_err_t last_ret = NRFX_ERROR_INTERNAL;
    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        nrfx_twi_xfer_desc_t desc = NRFX_TWI_XFER_DESC_TX(addr, msg, msg_len);
        last_ret = nrfx_twi_xfer(&s_twi, &desc, 0);
        if (last_ret == NRFX_SUCCESS) {
            i2c_log_recovered("write", addr, reg_value, attempt);
            return (int32_t)msg_len;
        }
        mp_hal_delay_ms(I2C_XFER_RETRY_DELAY_MS * (attempt + 1u));
    }

    printf("[I2C] write failed addr=0x%02x reg=0x%04x len=%lu ret=%ld\n",
           addr,
           reg_value,
           (unsigned long)nbytes,
           (long)last_ret);
    return -1;
}

static int32_t i2c_r_blocking(const uint8_t addr,
                              uint8_t *reg,
                              const uint8_t reg_num,
                              uint8_t *buf,
                              const uint8_t nbytes) {
    if (reg_num < 1 || nbytes < 1) {
        return -1;
    }

    uint8_t msg[2];
    if (reg_num > sizeof(msg)) {
        return -1;
    }
    for (uint8_t i = 0; i < reg_num; i++) {
        msg[reg_num - 1u - i] = reg[i];
    }

    const uint16_t reg_value = i2c_reg_value(reg, reg_num);
    nrfx_err_t last_ret = NRFX_ERROR_INTERNAL;
    for (uint32_t attempt = 0; attempt < I2C_XFER_RETRY_COUNT; ++attempt) {
        nrfx_twi_xfer_desc_t tx = NRFX_TWI_XFER_DESC_TX(addr, msg, reg_num);
        last_ret = nrfx_twi_xfer(&s_twi, &tx, NRFX_TWI_FLAG_TX_NO_STOP);
        if (last_ret == NRFX_SUCCESS) {
            nrfx_twi_xfer_desc_t rx = NRFX_TWI_XFER_DESC_RX(addr, buf, nbytes);
            last_ret = nrfx_twi_xfer(&s_twi, &rx, 0);
            if (last_ret == NRFX_SUCCESS) {
                i2c_log_recovered("read", addr, reg_value, attempt);
                return nbytes;
            }
        }
        mp_hal_delay_ms(I2C_XFER_RETRY_DELAY_MS * (attempt + 1u));
    }

    printf("[I2C] read failed addr=0x%02x reg=0x%04x len=%lu ret=%ld\n",
           addr,
           reg_value,
           (unsigned long)nbytes,
           (long)last_ret);
    return -1;
}

static int32_t i2c_w(uint8_t addr, uint16_t reg, uint32_t data, uint32_t mode) {
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)(reg & 0xFFu);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFFu);

    uint8_t buf[4];
    buf[0] = (uint8_t)(data & 0xFFu);
    buf[1] = (uint8_t)((data >> 8) & 0xFFu);
    buf[2] = (uint8_t)((data >> 16) & 0xFFu);
    buf[3] = (uint8_t)((data >> 24) & 0xFFu);

    switch (mode) {
    case 1:
    case 2:
    case 4:
        return i2c_w_blocking(addr, reg_buf, 2, buf, (uint8_t)mode);
    default:
        printf("[Error] Unknown I2C mode\n");
        return -1;
    }
}

static int32_t i2c_r(const uint8_t addr, uint16_t reg, uint32_t *data, uint32_t mode) {
    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)(reg & 0xFFu);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFFu);

    uint8_t buf[4] = {0};
    int32_t ret;
    switch (mode) {
    case 1:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 1);
        *data = (uint32_t)buf[0];
        return ret;
    case 2:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 2);
        *data = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        return ret;
    case 4:
        ret = i2c_r_blocking(addr, reg_buf, 2, buf, 4);
        *data = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
        return ret;
    default:
        printf("[Error] Unknown I2C mode\n");
        return -1;
    }
}

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size) {
    int ret = i2c_w(PIVARIETY_ADDR, addr, val, size);
    LOG_DEBUG("[pivariety_i2c_bridge_write], addr: 0x%X, val: 0x%X, size: %lu ec: %d\n",
              addr,
              (unsigned int)val,
              (unsigned long)size,
              ret);
    return ret;
}

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size) {
    int ret = i2c_r(PIVARIETY_ADDR, addr, val, size);
    LOG_DEBUG("[pivariety_i2c_bridge_read], addr: 0x%X, val: 0x%X, size: %lu ec: %d\n",
              addr,
              (unsigned int)*val,
              (unsigned long)size,
              ret);
    return ret;
}

int pivariety_spi_bridge_write(uint8_t *data, uint32_t len) {
    if (!data || len == 0) {
        return -1;
    }
    nrf_gpio_pin_clear(SPI_CSN_PIN);
    mp_hal_delay_us(SPI_CS_SETUP_US);
    nrfx_spim_xfer_desc_t desc = NRFX_SPIM_XFER_TX(data, len);
    nrfx_err_t ret = nrfx_spim_xfer(&s_spim, &desc, 0);
    mp_hal_delay_us(SPI_CS_HOLD_US);
    nrf_gpio_pin_set(SPI_CSN_PIN);
    mp_hal_delay_us(SPI_POST_XFER_US);
    if (ret != NRFX_SUCCESS) {
        printf("[SPI] write failed len=%lu ret=%ld\n", (unsigned long)len, (long)ret);
        return -1;
    }
    return (int)len;
}

int pivariety_spi_bridge_read(uint8_t *data, uint32_t len) {
    if (!data || len == 0) {
        return -1;
    }
    nrf_gpio_pin_clear(SPI_CSN_PIN);
    mp_hal_delay_us(SPI_CS_SETUP_US);
    nrfx_spim_xfer_desc_t desc = NRFX_SPIM_XFER_RX(data, len);
    nrfx_err_t ret = nrfx_spim_xfer(&s_spim, &desc, 0);
    mp_hal_delay_us(SPI_CS_HOLD_US);
    nrf_gpio_pin_set(SPI_CSN_PIN);
    mp_hal_delay_us(SPI_POST_XFER_US);
    if (ret != NRFX_SUCCESS) {
        printf("[SPI] read failed len=%lu ret=%ld\n", (unsigned long)len, (long)ret);
        return -1;
    }
    return (int)len;
}

bool bind_peripherals_api(void) {
    i2c_driver i2c_drv;
    i2c_drv.write = pivariety_i2c_bridge_write;
    i2c_drv.read = pivariety_i2c_bridge_read;
    i2c_drv.slp_ms = imx500_micropython_platform_sleep_ms;
    i2c_drv.slp_us = imx500_sleep_us;
    register_i2c_driver(i2c_drv);

    spi_driver spi_drv;
    spi_drv.write = pivariety_spi_bridge_write;
    spi_drv.read = pivariety_spi_bridge_read;
    register_spi_driver(spi_drv);
    return true;
}
