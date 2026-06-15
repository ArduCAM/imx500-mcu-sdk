#ifndef I2C_PAYLOAD_FLASH_TEST_PERIPHERALS_ADAPTER_H_
#define I2C_PAYLOAD_FLASH_TEST_PERIPHERALS_ADAPTER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool init_i2c_peripheral(void);
bool bind_i2c_peripherals_api(void);
void close_peripherals(void);
const char *peripherals_i2c_device_path(void);

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size);
int pivariety_i2c_bridge_write_block(uint16_t addr,
                                     const uint8_t *data,
                                     uint32_t len);
int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif  // I2C_PAYLOAD_FLASH_TEST_PERIPHERALS_ADAPTER_H_
