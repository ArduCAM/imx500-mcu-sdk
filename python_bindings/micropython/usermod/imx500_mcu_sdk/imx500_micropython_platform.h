#ifndef IMX500_MICROPYTHON_PLATFORM_H
#define IMX500_MICROPYTHON_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool imx500_micropython_platform_hardware_init(uint32_t i2c_baudrate, uint32_t spi_baudrate);
void imx500_micropython_platform_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif  // IMX500_MICROPYTHON_PLATFORM_H
