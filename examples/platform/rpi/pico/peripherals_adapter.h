#ifndef PERIPHERALS_ADAPTER_H_
#define PERIPHERALS_ADAPTER_H_
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t i2c0_w_blocking(const uint8_t addr, uint8_t *reg, const uint8_t reg_num,
                        uint8_t *buf, const uint8_t nbytes);

int32_t i2c0_r_blocking(const uint8_t addr, uint8_t *reg, const uint8_t reg_num,
                        uint8_t *buf, const uint8_t nbytes);

int32_t i2c0_w(uint8_t addr, uint16_t reg, uint32_t data, uint32_t mode);

int32_t i2c0_r(const uint8_t addr, uint16_t reg, uint32_t *data, uint32_t mode);

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size);

int pivariety_i2c_bridge_read(uint16_t addr, uint32_t* val, uint32_t size);

bool bind_peripherals_api(void);

#ifdef __cplusplus
}
#endif

#endif // PERIPHERALS_ADAPTER_H_