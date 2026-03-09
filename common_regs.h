#ifndef COMMON_REGS_H_
#define COMMON_REGS_H_

#define DEVICE_VERSION_REG          0x0101
#define DEVICE_ID_REG               0x0103

#define SENSOR_REG_BASE             0x0500
#define SENSOR_RD_REG               (SENSOR_REG_BASE | 0x0001)
#define SENSOR_WR_REG               (SENSOR_REG_BASE | 0x0002)

// pivariety passthrough format: [31:16]=sensor register addr, [7:0]=sensor data.
#define SENSOR_I2C_16_8_PACK(addr, data) \
    ((((uint32_t)(addr) & 0xFFFFu) << 16) | ((uint32_t)(data) & 0x00FFu))
#define SENSOR_I2C_16_8_ADDR(addr) SENSOR_I2C_16_8_PACK((addr), 0u)
#define SENSOR_I2C_16_8_DATA(v)     ((uint8_t)((v) & 0xFFu))

#endif  // COMMON_REGS_H_
