#ifndef I2C_PAYLOAD_FLASH_TEST_G_CONFIG_H_
#define I2C_PAYLOAD_FLASH_TEST_G_CONFIG_H_

/*
 * This example talks to the module MCU through the I2C lines in the Raspberry
 * Pi 5 camera connection. Override I2C_PAYLOAD_I2C_DEVICE when the desired bus
 * is known.
 */

#ifndef I2C_PAYLOAD_I2C_DEVICE
#define I2C_PAYLOAD_I2C_DEVICE ""
#endif

#ifndef I2C_PAYLOAD_I2C_CANDIDATES
#define I2C_PAYLOAD_I2C_CANDIDATES \
    "/dev/i2c-10:/dev/i2c-11:/dev/i2c-12:/dev/i2c-13:/dev/i2c-4:/dev/i2c-6:/dev/i2c-1:/dev/i2c-0"
#endif

#ifndef I2C_PAYLOAD_I2C_TARGET_ADDR
#define I2C_PAYLOAD_I2C_TARGET_ADDR 0x0C
#endif

#endif  // I2C_PAYLOAD_FLASH_TEST_G_CONFIG_H_
