#ifndef TOOLS_LOG_H_
#define TOOLS_LOG_H_

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int32_t print_buf_hex(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        printf("0x%02x ", (unsigned)buf[i]);
    }
    return 0;
}

#define LOG_DEBUG(fmt, ...) do {} while (0)
#define LOG_INFO(fmt, ...)  do { printf("[INFO] " fmt, ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { printf("[WARN] " fmt, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...) do { printf("[ERROR] " fmt, ##__VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif // TOOLS_LOG_H_
