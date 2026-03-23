#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

#include "ArducamIMX500SDK.h"
#include "g_config.h"
#include "peripherals_adapter.h"

namespace {

// Sized for the largest payload used by the bundled segmentation host task.
constexpr uint32_t kMaxFrameSize = 384 * 1024;
constexpr uint8_t kPacketMagic0 = 0x49u;
constexpr uint8_t kPacketMagic1 = 0x4Du;
constexpr uint8_t kPacketMagic2 = 0x58u;
constexpr uint8_t kPacketMagic3 = 0x35u;
constexpr uint8_t kPacketVersion = 1u;
constexpr uint8_t kPacketTypeFrame = 1u;

uint8_t frame_buf[kMaxFrameSize];

#pragma pack(push, 1)
struct SerialPacketHeader {
    uint8_t magic[4];
    uint8_t version;
    uint8_t packet_type;
    uint16_t header_len;
    uint32_t sequence;
    int32_t payload_len;
    uint32_t checksum;
};
#pragma pack(pop)

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

uint32_t checksum32(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return sum;
}

void serial_write_all(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        putchar_raw(static_cast<char>(data[i]));
    }
}

void send_packet(uint32_t seq, const uint8_t *payload, int32_t payload_len) {
    SerialPacketHeader header = {};
    header.magic[0] = kPacketMagic0;
    header.magic[1] = kPacketMagic1;
    header.magic[2] = kPacketMagic2;
    header.magic[3] = kPacketMagic3;
    header.version = kPacketVersion;
    header.packet_type = kPacketTypeFrame;
    header.header_len = static_cast<uint16_t>(sizeof(SerialPacketHeader));
    header.sequence = seq;
    header.payload_len = payload_len;

    if (payload_len > 0) {
        header.checksum = checksum32(payload, static_cast<uint32_t>(payload_len));
    }

    serial_write_all(reinterpret_cast<const uint8_t *>(&header), static_cast<uint32_t>(sizeof(header)));
    if (payload_len > 0) {
        serial_write_all(payload, static_cast<uint32_t>(payload_len));
    }
    fflush(stdout);
}

}  // namespace

int main() {
    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    sleep_ms(1000);

    i2c_master_init(100 * 1000);
    spi_master_init(1000 * 1000 * 5);
    bind_peripherals_api();

    const bool open_ret = open(
        nullptr,
        0,
        nullptr,
        0,
        MIPI_DATA_IMAGE,
        SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR,
        10
    );
    if (!open_ret) {
        send_packet(0, nullptr, -1);
        while (1) {
            sleep_ms(1000);
        }
    }

    stream_on();

    uint32_t seq = 0;
    while (1) {
        const int32_t bytes_read = read_metadata(frame_buf, kMaxFrameSize);
        send_packet(seq, frame_buf, bytes_read > 0 ? bytes_read : -1);
        ++seq;
    }

    return 0;
}
