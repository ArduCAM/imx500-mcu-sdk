#include "ai_driver.h"

spi_driver g_spi_driver;

void register_spi_driver(spi_driver driver) { g_spi_driver = driver; }

i2c_driver g_i2c_driver;

void register_i2c_driver(i2c_driver driver) { g_i2c_driver = driver; }

imx500_printf_fn g_printf_fn;

void register_printf(imx500_printf_fn fn) { g_printf_fn = fn; }
