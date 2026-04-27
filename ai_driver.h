#ifndef AI_DRIVER_H 
#define AI_DRIVER_H 

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*spi_write_fn)(uint8_t *data, uint32_t len);
typedef int (*spi_write_fn)(uint8_t *data, uint32_t len);

typedef struct spi_driver {
  spi_write_fn write;
  spi_write_fn read;
} spi_driver;

extern spi_driver g_spi_driver;

void register_spi_driver(spi_driver driver);
int _spi_write(uint8_t* data, uint32_t len);
int _spi_read(uint8_t* data, uint32_t len);

typedef int (*i2c_write_fn)(uint16_t addr, uint32_t val, uint32_t size);
typedef int (*i2c_read_fn)(uint16_t addr, uint32_t *val, uint32_t size);
typedef void (*sleep_ms_fn)(uint32_t ms);
typedef void (*sleep_us_fn)(uint64_t us);
typedef void (*imx500_printf_fn)(const char *msg);

typedef struct i2c_driver {
  i2c_write_fn write;
  i2c_read_fn read;
  sleep_ms_fn slp_ms;
  sleep_us_fn slp_us;
} i2c_driver;

extern i2c_driver g_i2c_driver;
extern imx500_printf_fn g_printf_fn;

void register_i2c_driver(i2c_driver driver);
void register_printf(imx500_printf_fn fn);
int _i2c_write(uint32_t addr, uint32_t val, uint32_t size);
int _i2c_read(uint32_t addr, uint32_t *val, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif  // AI_DRIVER_H 
