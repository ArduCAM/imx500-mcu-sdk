#ifndef RPI5_PERIPHERALS_ADAPTER_H_
#define RPI5_PERIPHERALS_ADAPTER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool init_peripherals(void);
void close_peripherals(void);
bool bind_peripherals_api(void);

int pivariety_i2c_bridge_write(uint16_t addr, uint32_t val, uint32_t size);
int pivariety_i2c_bridge_read(uint16_t addr, uint32_t *val, uint32_t size);
int pivariety_spi_bridge_write(uint8_t *data, uint32_t len);
int pivariety_spi_bridge_read(uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif  // RPI5_PERIPHERALS_ADAPTER_H_
