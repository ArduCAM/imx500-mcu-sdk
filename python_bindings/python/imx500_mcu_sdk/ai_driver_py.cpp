#include "ai_driver.h"

spi_driver g_spi_driver = {};
i2c_driver g_i2c_driver = {};
imx500_printf_fn g_printf_fn = nullptr;

void register_spi_driver(spi_driver driver) { g_spi_driver = driver; }

void register_i2c_driver(i2c_driver driver) { g_i2c_driver = driver; }

void register_printf(imx500_printf_fn fn) { g_printf_fn = fn; }

int _spi_write(uint8_t* data, uint32_t len) {
  if (!g_spi_driver.write) {
    return -1;
  }
  return g_spi_driver.write(data, len);
}

int _spi_read(uint8_t* data, uint32_t len) {
  if (!g_spi_driver.read) {
    return -1;
  }
  return g_spi_driver.read(data, len);
}

int _i2c_write(uint32_t addr, uint32_t val, uint32_t size) {
  if (!g_i2c_driver.write) {
    return -1;
  }
  return g_i2c_driver.write(static_cast<uint16_t>(addr), val, size);
}

int _i2c_read(uint32_t addr, uint32_t* val, uint32_t size) {
  if (!g_i2c_driver.read) {
    return -1;
  }
  return g_i2c_driver.read(static_cast<uint16_t>(addr), val, size);
}
