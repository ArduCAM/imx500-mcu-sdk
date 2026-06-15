/**
 * @file ai_driver.h
 * @brief Host-side driver registration hooks used by the IMX500 MCU SDK.
 */

#ifndef AI_DRIVER_H 
#define AI_DRIVER_H 

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SPI transmit callback supplied by the platform. */
typedef int (*spi_write_fn)(uint8_t *data, uint32_t len);

/** @brief SPI receive callback supplied by the platform. */
typedef int (*spi_read_fn)(uint8_t *data, uint32_t len);

/** @brief Platform SPI hooks used by the SDK runtime. */
typedef struct spi_driver {
  spi_write_fn write;
  spi_read_fn read;
} spi_driver;

extern spi_driver g_spi_driver;

/** @brief Register the SPI implementation used by the SDK. */
void register_spi_driver(spi_driver driver);

/** @brief Internal wrapper that forwards one write transaction to the registered SPI driver. */
int _spi_write(uint8_t* data, uint32_t len);

/** @brief Internal wrapper that forwards one read transaction to the registered SPI driver. */
int _spi_read(uint8_t* data, uint32_t len);

/** @brief I2C write callback supplied by the platform. */
typedef int (*i2c_write_fn)(uint16_t addr, uint32_t val, uint32_t size);

/** @brief I2C read callback supplied by the platform. */
typedef int (*i2c_read_fn)(uint16_t addr, uint32_t *val, uint32_t size);

/** @brief Optional raw I2C block-write callback supplied by the platform. */
typedef int (*i2c_write_block_fn)(uint16_t addr, const uint8_t *data, uint32_t len);

/** @brief Millisecond delay callback supplied by the platform. */
typedef void (*sleep_ms_fn)(uint32_t ms);

/** @brief Microsecond delay callback supplied by the platform. */
typedef void (*sleep_us_fn)(uint64_t us);

/** @brief Optional log sink used instead of the SDK printf fallback. */
typedef void (*imx500_printf_fn)(const char *msg);

/** @brief Platform I2C and timing hooks used by the SDK runtime. */
typedef struct i2c_driver {
  i2c_write_fn write;
  i2c_read_fn read;
  i2c_write_block_fn write_block;
  sleep_ms_fn slp_ms;
  sleep_us_fn slp_us;
} i2c_driver;

extern i2c_driver g_i2c_driver;
extern imx500_printf_fn g_printf_fn;

/** @brief Register the I2C and delay implementation used by the SDK. */
void register_i2c_driver(i2c_driver driver);

/** @brief Register an optional logging callback for SDK messages. */
void register_printf(imx500_printf_fn fn);

/** @brief Internal wrapper that forwards one I2C write transaction. */
int _i2c_write(uint32_t addr, uint32_t val, uint32_t size);

/** @brief Internal wrapper that forwards one I2C read transaction. */
int _i2c_read(uint32_t addr, uint32_t *val, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif  // AI_DRIVER_H 
