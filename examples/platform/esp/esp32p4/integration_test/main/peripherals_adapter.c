#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "ArducamIMX500SDK.h"
#include "example_video_common.h"
#include "peripherals_adapter.h"

#define PIVARIETY_ADDR 0x0C
#define IMX500_SPI_HOST SPI2_HOST
#define IMX500_SPI_MAX_TRANSFER_SZ (284 * 1024)

static const char *TAG = "peripherals_adapter";
static const int s_imx500_spi_clock_hz = 5000000;
static const int s_imx500_spi_sck_pin = 5;
static const int s_imx500_spi_mosi_pin = 3;
static const int s_imx500_spi_miso_pin = 2;
static const int s_imx500_spi_cs_pin = 4;
static i2c_master_dev_handle_t s_camera_i2c_dev_handle;
static spi_device_handle_t s_imx500_spi_handle;
static bool s_bound;

static void sleep_ms_adapter(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void sleep_us_adapter(uint64_t us)
{
    esp_rom_delay_us((uint32_t)us);
}

static esp_err_t ensure_i2c_device(void)
{
    if (s_camera_i2c_dev_handle != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus_handle = example_video_get_i2c_bus_handle();
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "camera i2c bus is not initialized");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PIVARIETY_ADDR,
        .scl_speed_hz = EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_camera_i2c_dev_handle),
                        TAG, "failed to add imx500 i2c device");
    return ESP_OK;
}

static esp_err_t ensure_spi_device(void)
{
    if (s_imx500_spi_handle != NULL) {
        return ESP_OK;
    }

    /* The config pins are named from the IMX500 module side:
     * host MOSI/TX must connect to module RX, and host MISO/RX must connect to module TX. */
    spi_bus_config_t buscfg = {
        .mosi_io_num = s_imx500_spi_mosi_pin,
        .miso_io_num = s_imx500_spi_miso_pin,
        .sclk_io_num = s_imx500_spi_sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = IMX500_SPI_MAX_TRANSFER_SZ,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = s_imx500_spi_clock_hz,
        .mode = 3,
        .spics_io_num = s_imx500_spi_cs_pin,
        .queue_size = 1,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(IMX500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "failed to init spi bus");
    ESP_RETURN_ON_ERROR(spi_bus_add_device(IMX500_SPI_HOST, &devcfg, &s_imx500_spi_handle), TAG, "failed to add spi device");

    ESP_LOGI(TAG, "initialized IMX500 SPI host=%d sck=%d mosi(host->module_rx)=%d miso(host<-module_tx)=%d cs=%d hz=%d",
             IMX500_SPI_HOST,
             s_imx500_spi_sck_pin,
             s_imx500_spi_mosi_pin,
             s_imx500_spi_miso_pin,
             s_imx500_spi_cs_pin,
             s_imx500_spi_clock_hz);
    return ESP_OK;
}

int32_t i2c0_w_blocking(const uint8_t addr, uint8_t *reg, const uint8_t reg_num,
                        uint8_t *buf, const uint8_t nbytes)
{
    if (reg_num < 1 || nbytes < 1 || addr != PIVARIETY_ADDR) {
        return -1;
    }
    if (ensure_i2c_device() != ESP_OK) {
        return -1;
    }

    uint8_t *msg = (uint8_t *)malloc((size_t)reg_num + (size_t)nbytes);
    if (msg == NULL) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        msg[i] = reg[reg_num - 1U - i];
    }
    for (uint8_t i = 0; i < nbytes; i++) {
        msg[reg_num + i] = buf[nbytes - 1U - i];
    }

    esp_err_t ret = i2c_master_transmit(s_camera_i2c_dev_handle, msg, (size_t)reg_num + (size_t)nbytes, -1);
    free(msg);
    return (ret == ESP_OK) ? nbytes : -1;
}

int32_t i2c0_r_blocking(const uint8_t addr, uint8_t *reg, const uint8_t reg_num,
                        uint8_t *buf, const uint8_t nbytes)
{
    if (reg_num < 1 || nbytes < 1 || addr != PIVARIETY_ADDR) {
        return -1;
    }
    if (ensure_i2c_device() != ESP_OK) {
        return -1;
    }

    uint8_t *reg_buf = (uint8_t *)malloc(reg_num);
    if (reg_buf == NULL) {
        return -1;
    }

    for (uint8_t i = 0; i < reg_num; i++) {
        reg_buf[i] = reg[reg_num - 1U - i];
    }

    esp_err_t ret = i2c_master_transmit_receive(s_camera_i2c_dev_handle, reg_buf, reg_num, buf, nbytes, -1);
    free(reg_buf);
    return (ret == ESP_OK) ? nbytes : -1;
}

int32_t i2c0_w(uint8_t addr, uint16_t reg, uint32_t data, uint32_t mode)
{
    uint8_t reg_buf[2];
    uint8_t buf[4];

    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    buf[0] = (uint8_t)(data & 0xFF);
    buf[1] = (uint8_t)((data >> 8) & 0xFF);
    buf[2] = (uint8_t)((data >> 16) & 0xFF);
    buf[3] = (uint8_t)((data >> 24) & 0xFF);

    switch (mode) {
    case 1:
        return i2c0_w_blocking(addr, reg_buf, 2, buf, 1);
    case 2:
        return i2c0_w_blocking(addr, reg_buf, 2, buf, 2);
    case 4:
        return i2c0_w_blocking(addr, reg_buf, 2, buf, 4);
    default:
        ESP_LOGE(TAG, "unknown i2c write mode=%" PRIu32, mode);
        return -1;
    }
}

int32_t i2c0_r(const uint8_t addr, uint16_t reg, uint32_t *data, uint32_t mode)
{
    uint8_t reg_buf[2];
    uint8_t buf[4] = {0};
    int32_t ret;

    if (data == NULL) {
        return -1;
    }

    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    switch (mode) {
    case 1:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 1);
        *data = (uint32_t)buf[0];
        break;
    case 2:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 2);
        *data = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        break;
    case 4:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 4);
        *data = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
        break;
    default:
        ESP_LOGE(TAG, "unknown i2c read mode=%" PRIu32, mode);
        ret = -1;
        break;
    }

    return ret;
}

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size)
{
    return i2c0_w(PIVARIETY_ADDR, addr, val, size);
}

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size)
{
    return i2c0_r(PIVARIETY_ADDR, addr, val, size);
}

static int pivariety_spi_bridge_write(uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    if (ensure_spi_device() != ESP_OK) {
        return -1;
    }

    uint8_t *dma_tx = heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (dma_tx == NULL) {
        return -1;
    }
    memcpy(dma_tx, data, len);

    spi_transaction_t trans = {
        .length = len * 8U,
        .tx_buffer = dma_tx,
    };
    esp_err_t ret = spi_device_polling_transmit(s_imx500_spi_handle, &trans);
    free(dma_tx);
    return (ret == ESP_OK) ? (int)len : -1;
}

static int pivariety_spi_bridge_read(uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    if (ensure_spi_device() != ESP_OK) {
        return -1;
    }

    uint8_t *dma_tx = heap_caps_calloc(len, 1, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    uint8_t *dma_rx = heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (dma_tx == NULL || dma_rx == NULL) {
        free(dma_tx);
        free(dma_rx);
        return -1;
    }

    spi_transaction_t trans = {
        .length = len * 8U,
        .rxlength = len * 8U,
        .tx_buffer = dma_tx,
        .rx_buffer = dma_rx,
    };
    esp_err_t ret = spi_device_polling_transmit(s_imx500_spi_handle, &trans);
    if (ret == ESP_OK) {
        memcpy(data, dma_rx, len);
    }

    free(dma_tx);
    free(dma_rx);
    return (ret == ESP_OK) ? (int)len : -1;
}

bool bind_peripherals_api(void)
{
    if (s_bound) {
        return true;
    }
    if (ensure_i2c_device() != ESP_OK || ensure_spi_device() != ESP_OK) {
        return false;
    }

    i2c_driver i2c_drv = {
        .write = pivariety_i2c_bridge_write,
        .read = pivariety_i2c_bridge_read,
        .slp_ms = sleep_ms_adapter,
        .slp_us = sleep_us_adapter,
    };
    spi_driver spi_drv = {
        .write = pivariety_spi_bridge_write,
        .read = pivariety_spi_bridge_read,
    };

    register_i2c_driver(i2c_drv);
    register_spi_driver(spi_drv);
    s_bound = true;
    return true;
}
