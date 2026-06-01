#ifndef G_CONFIG_H
#define G_CONFIG_H

/*
 * Raspberry Pi 5 40-pin header, SPI0 CE0:
 *   I2C1 SDA  GPIO2  physical pin 3
 *   I2C1 SCL  GPIO3  physical pin 5
 *   SPI0 MOSI GPIO10 physical pin 19
 *   SPI0 MISO GPIO9  physical pin 21
 *   SPI0 SCLK GPIO11 physical pin 23
 *   SPI0 CE0  GPIO8  physical pin 24
 */
#ifndef RPI5_I2C_DEVICE
#define RPI5_I2C_DEVICE "/dev/i2c-1"
#endif

#ifndef RPI5_SPI_DEVICE
#define RPI5_SPI_DEVICE "/dev/spidev0.0"
#endif

#ifndef RPI5_I2C_TARGET_ADDR
#define RPI5_I2C_TARGET_ADDR 0x0C
#endif

#ifndef RPI5_SPI_MODE
#define RPI5_SPI_MODE 3
#endif

#ifndef RPI5_SPI_BITS_PER_WORD
#define RPI5_SPI_BITS_PER_WORD 8
#endif

#ifndef RPI5_SPI_SPEED_HZ
#define RPI5_SPI_SPEED_HZ 5000000
#endif

#ifndef RPI5_SPI_DELAY_USECS
#define RPI5_SPI_DELAY_USECS 50
#endif

#ifndef RPI5_SPI_MAX_TRANSFER_BYTES
#define RPI5_SPI_MAX_TRANSFER_BYTES 4096
#endif

#endif  // G_CONFIG_H
