#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "log.h"
#include "g_config.h"
#include "ArducamIMX500SDK.h"
#include "peripherals_adapter.h"

#define MAX_FRAME_SIZE          (384 * 1024)
#define PACKET_MAGIC0           0x49u  // I
#define PACKET_MAGIC1           0x4Du  // M
#define PACKET_MAGIC2           0x58u  // X
#define PACKET_MAGIC3           0x35u  // 5
#define PACKET_VERSION          1u
#define PACKET_TYPE_FRAME       1u

static uint8_t frame_buf[MAX_FRAME_SIZE];

#pragma pack(push, 1)
typedef struct {
    uint8_t magic[4];
    uint8_t version;
    uint8_t packet_type;
    uint16_t header_len;
    uint32_t sequence;
    int32_t payload_len;
    uint32_t checksum;
} serial_packet_header_t;
#pragma pack(pop)

static void i2c_master_init(uint32_t baudrate) {
    i2c_init(i2c_default, baudrate);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

static void spi_master_init(uint32_t baudrate) {
    spi_init(spi_default, baudrate);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CSN_PIN);
    gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
    gpio_put(SPI_CSN_PIN, 1);

    spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

static uint32_t checksum32(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return sum;
}

static void serial_write_all(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        putchar_raw((char)data[i]);
    }
}

static void send_packet(uint32_t seq, const uint8_t *payload, int32_t payload_len) {
    serial_packet_header_t hdr;
    hdr.magic[0] = PACKET_MAGIC0;
    hdr.magic[1] = PACKET_MAGIC1;
    hdr.magic[2] = PACKET_MAGIC2;
    hdr.magic[3] = PACKET_MAGIC3;
    hdr.version = PACKET_VERSION;
    hdr.packet_type = PACKET_TYPE_FRAME;
    hdr.header_len = (uint16_t)sizeof(serial_packet_header_t);
    hdr.sequence = seq;
    hdr.payload_len = payload_len;
    hdr.checksum = 0;

    if (payload_len > 0) {
        hdr.checksum = checksum32(payload, (uint32_t)payload_len);
    }

    serial_write_all((const uint8_t *)&hdr, (uint32_t)sizeof(hdr));
    if (payload_len > 0) {
        serial_write_all(payload, (uint32_t)payload_len);
    }
    fflush(stdout);
}

int main() {
    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(1000);

    i2c_master_init(100 * 1000);
    spi_master_init(1000 * 1000 * 5);
    bind_peripherals_api();

    bool open_ret = open(
        nullptr,
        0,
        nullptr,
        0,
        MIPI_DATA_IMAGE,
        SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR
    );
    if (!open_ret) {
        send_packet(0, NULL, -1);
        while (1) {
            sleep_ms(1000);
        }
    }

    stream_on();

    uint32_t seq = 0;
    while (1) {
        int32_t bytes_read = read_metadata(frame_buf, MAX_FRAME_SIZE);
        send_packet(seq, frame_buf, bytes_read > 0 ? bytes_read : -1);
        seq++;
    }

    return 0;
}