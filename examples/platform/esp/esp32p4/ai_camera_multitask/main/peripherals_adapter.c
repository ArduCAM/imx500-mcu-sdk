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
#include "esp_task_wdt.h"

#include "ArducamIMX500SDK.h"
#include "example_video_common.h"
#include "peripherals_adapter.h"

#define PIVARIETY_ADDR 0x0C
#define IMX500_SPI_HOST SPI2_HOST
#define IMX500_SPI_MAX_TRANSFER_SZ (284 * 1024)
#define IMX500_SPI_DMA_HW_MAX_BITS 0x0003FFFFU
#define IMX500_SPI_DMA_SAFE_CHUNK_MAX_BYTES (IMX500_SPI_DMA_HW_MAX_BITS / 8U)
#define IMX500_COOPERATIVE_BUSY_WAIT_US (16 * 1000)
#define IMX500_I2C_SCL_WAIT_US (200 * 1000)
#define IMX500_I2C_RETRY_COUNT 3
#define IMX500_I2C_RETRY_DELAY_MS 2

static const char *TAG = "peripherals_adapter";
static const int s_imx500_spi_clock_hz = 5000000;
static const int s_imx500_spi_sck_pin = 3;
static const int s_imx500_spi_mosi_pin = 5;
static const int s_imx500_spi_miso_pin = 4;
static const int s_imx500_spi_cs_pin = 6;
static i2c_master_dev_handle_t s_camera_i2c_dev_handle;
static spi_device_handle_t s_imx500_spi_handle;
static bool s_bound;
static uint32_t s_busy_wait_budget_us;
static uint8_t *s_imx500_spi_dma_tx_zero;
static uint8_t *s_imx500_spi_dma_rx_chunk;
static size_t s_imx500_spi_dma_chunk_size;
static size_t s_imx500_spi_max_read_chunk_len;

static void sleep_ms_adapter(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void maybe_reset_current_task_wdt(void)
{
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        (void)esp_task_wdt_reset();
    }
}

static void cooperative_yield_after_busy_wait(void)
{
    maybe_reset_current_task_wdt();
    vTaskDelay(1);
}

static void sleep_us_adapter(uint64_t us)
{
    while (us > 0) {
        uint32_t slice_us = (us > 1000U) ? 1000U : (uint32_t)us;
        esp_rom_delay_us(slice_us);
        us -= slice_us;
        s_busy_wait_budget_us += slice_us;

        if (s_busy_wait_budget_us >= IMX500_COOPERATIVE_BUSY_WAIT_US) {
            s_busy_wait_budget_us = 0;
            cooperative_yield_after_busy_wait();
        }
    }
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
        .scl_wait_us = IMX500_I2C_SCL_WAIT_US,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_camera_i2c_dev_handle),
                        TAG, "failed to add imx500 i2c device");
    ESP_LOGI(TAG, "added IMX500 I2C device addr=0x%02x freq=%u scl_wait_us=%u",
             PIVARIETY_ADDR,
             (unsigned)EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
             (unsigned)IMX500_I2C_SCL_WAIT_US);
    return ESP_OK;
}

static void recover_i2c_bus_after_error(void)
{
    i2c_master_bus_handle_t bus_handle = example_video_get_i2c_bus_handle();
    if (bus_handle != NULL) {
        (void)i2c_master_bus_reset(bus_handle);
    }
}

static esp_err_t imx500_i2c_transmit_with_retry(const uint8_t *buffer, size_t buffer_size)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < IMX500_I2C_RETRY_COUNT; ++attempt) {
        ret = i2c_master_transmit(s_camera_i2c_dev_handle, buffer, buffer_size, -1);
        if (ret == ESP_OK) {
            return ret;
        }
        ESP_LOGW(TAG, "i2c transmit retry %d/%d ret=%s", attempt + 1, IMX500_I2C_RETRY_COUNT, esp_err_to_name(ret));
        recover_i2c_bus_after_error();
        vTaskDelay(pdMS_TO_TICKS(IMX500_I2C_RETRY_DELAY_MS));
    }
    return ret;
}

static esp_err_t imx500_i2c_transmit_receive_with_retry(const uint8_t *tx_buffer,
                                                        size_t tx_size,
                                                        uint8_t *rx_buffer,
                                                        size_t rx_size)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < IMX500_I2C_RETRY_COUNT; ++attempt) {
        ret = i2c_master_transmit_receive(s_camera_i2c_dev_handle, tx_buffer, tx_size, rx_buffer, rx_size, -1);
        if (ret == ESP_OK) {
            return ret;
        }
        ESP_LOGW(TAG, "i2c transmit_receive retry %d/%d ret=%s", attempt + 1, IMX500_I2C_RETRY_COUNT, esp_err_to_name(ret));
        recover_i2c_bus_after_error();
        vTaskDelay(pdMS_TO_TICKS(IMX500_I2C_RETRY_DELAY_MS));
    }
    return ret;
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
    ESP_LOGI(TAG, "SPI initialized at %d Hz", s_imx500_spi_clock_hz);

    size_t queried_max_read_chunk_len = 0;
    ESP_RETURN_ON_ERROR(spi_bus_get_max_transaction_len(IMX500_SPI_HOST, &queried_max_read_chunk_len),
                        TAG, "failed to query spi max transaction len");
    s_imx500_spi_max_read_chunk_len = queried_max_read_chunk_len;
    if (s_imx500_spi_max_read_chunk_len > IMX500_SPI_DMA_SAFE_CHUNK_MAX_BYTES) {
        ESP_LOGW(TAG, "spi max transaction len=%u exceeds P4 safe DMA chunk=%u, clamping",
                 (unsigned)s_imx500_spi_max_read_chunk_len,
                 (unsigned)IMX500_SPI_DMA_SAFE_CHUNK_MAX_BYTES);
        s_imx500_spi_max_read_chunk_len = IMX500_SPI_DMA_SAFE_CHUNK_MAX_BYTES;
    }
    ESP_RETURN_ON_FALSE(s_imx500_spi_max_read_chunk_len > 0, ESP_ERR_INVALID_STATE, TAG,
                        "invalid spi max transaction len");

    ESP_LOGI(TAG, "initialized IMX500 SPI host=%d sck=%d mosi(host->module_rx)=%d miso(host<-module_tx)=%d cs=%d hz=%d max_chunk=%u",
             IMX500_SPI_HOST,
             s_imx500_spi_sck_pin,
             s_imx500_spi_mosi_pin,
             s_imx500_spi_miso_pin,
             s_imx500_spi_cs_pin,
             s_imx500_spi_clock_hz,
             (unsigned)s_imx500_spi_max_read_chunk_len);
    return ESP_OK;
}

static esp_err_t ensure_spi_dma_chunk_buffers(void)
{
    if (s_imx500_spi_max_read_chunk_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_imx500_spi_dma_chunk_size == s_imx500_spi_max_read_chunk_len &&
        s_imx500_spi_dma_tx_zero != NULL && s_imx500_spi_dma_rx_chunk != NULL) {
        return ESP_OK;
    }

    free(s_imx500_spi_dma_tx_zero);
    free(s_imx500_spi_dma_rx_chunk);
    s_imx500_spi_dma_tx_zero = heap_caps_calloc(s_imx500_spi_max_read_chunk_len, 1, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_imx500_spi_dma_rx_chunk = heap_caps_malloc(s_imx500_spi_max_read_chunk_len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_imx500_spi_dma_tx_zero == NULL || s_imx500_spi_dma_rx_chunk == NULL) {
        free(s_imx500_spi_dma_tx_zero);
        free(s_imx500_spi_dma_rx_chunk);
        s_imx500_spi_dma_tx_zero = NULL;
        s_imx500_spi_dma_rx_chunk = NULL;
        s_imx500_spi_dma_chunk_size = 0;
        return ESP_ERR_NO_MEM;
    }
    s_imx500_spi_dma_chunk_size = s_imx500_spi_max_read_chunk_len;
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

    esp_err_t ret = imx500_i2c_transmit_with_retry(msg, (size_t)reg_num + (size_t)nbytes);
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

    esp_err_t ret = imx500_i2c_transmit_receive_with_retry(reg_buf, reg_num, buf, nbytes);
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
    uint32_t value = 0;
    int32_t ret;

    if (data == NULL) {
        return -1;
    }

    reg_buf[0] = (uint8_t)(reg & 0xFF);
    reg_buf[1] = (uint8_t)((reg >> 8) & 0xFF);

    switch (mode) {
    case 1:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 1);
        value = (uint32_t)buf[0];
        break;
    case 2:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 2);
        value = ((uint32_t)buf[0] << 8) | (uint32_t)buf[1];
        break;
    case 4:
        ret = i2c0_r_blocking(addr, reg_buf, 2, buf, 4);
        value = ((uint32_t)buf[0] << 24) |
                ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) |
                (uint32_t)buf[3];
        break;
    default:
        ESP_LOGE(TAG, "unknown i2c read mode=%" PRIu32, mode);
        ret = -1;
        break;
    }

    if (ret >= 0) {
        *data = value;
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
    maybe_reset_current_task_wdt();

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
    if (ensure_spi_dma_chunk_buffers() != ESP_OK) {
        ESP_LOGE(TAG, "failed to allocate SPI DMA chunk buffers");
        return -1;
    }

    esp_err_t ret = spi_device_acquire_bus(s_imx500_spi_handle, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to acquire SPI bus for metadata read: %s", esp_err_to_name(ret));
        return -1;
    }

    int result = -1;
    uint32_t offset = 0;
    while (offset < len) {
        const uint32_t chunk_len = (len - offset > s_imx500_spi_max_read_chunk_len)
                                       ? (uint32_t)s_imx500_spi_max_read_chunk_len
                                       : (len - offset);
        spi_transaction_t trans = {
            .flags = ((offset + chunk_len) < len) ? SPI_TRANS_CS_KEEP_ACTIVE : 0,
            .length = chunk_len * 8U,
            .rxlength = chunk_len * 8U,
            .tx_buffer = s_imx500_spi_dma_tx_zero,
            .rx_buffer = s_imx500_spi_dma_rx_chunk,
        };

        ret = spi_device_polling_transmit(s_imx500_spi_handle, &trans);
        maybe_reset_current_task_wdt();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI metadata chunk read failed off=%" PRIu32 " len=%" PRIu32 " err=%s",
                     offset, chunk_len, esp_err_to_name(ret));
            goto done;
        }
        if ((trans.flags & SPI_TRANS_DMA_RX_FAIL) != 0 || (trans.flags & SPI_TRANS_DMA_TX_FAIL) != 0) {
            ESP_LOGE(TAG, "SPI metadata DMA failure off=%" PRIu32 " len=%" PRIu32 " flags=0x%08" PRIx32,
                     offset, chunk_len, trans.flags);
            goto done;
        }

        memcpy(data + offset, s_imx500_spi_dma_rx_chunk, chunk_len);
        offset += chunk_len;
    }

    result = (int)len;

done:
    spi_device_release_bus(s_imx500_spi_handle);
    return result;
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
