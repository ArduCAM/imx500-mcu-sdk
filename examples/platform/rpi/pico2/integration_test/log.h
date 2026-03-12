#ifndef TOOLS_LOG_H_
#define TOOLS_LOG_H_

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_WARN  3
#define LOG_LEVEL_ERROR 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifndef LOG_SHOW_FILE
#define LOG_SHOW_FILE 0
#endif

#ifndef LOG_SHOW_LINE
#define LOG_SHOW_LINE 0
#endif

#ifndef LOG_AUTO_NEWLINE
#define LOG_AUTO_NEWLINE 0
#endif

static inline int32_t print_buf_hex(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        printf("0x%02x ", (unsigned)buf[i]);
    }
#if LOG_AUTO_NEWLINE
    printf("\n");
#endif
    return 0;
}

#if LOG_SHOW_FILE && LOG_SHOW_LINE
#define LOG_PRINT(level_str, fmt, ...)                                      \
    do {                                                                    \
        printf("[%s] %s:%d: ", (level_str), __FILE__, __LINE__);            \
        printf((fmt), ##__VA_ARGS__);                                       \
        if (LOG_AUTO_NEWLINE) printf("\n");                                 \
    } while (0)

#elif LOG_SHOW_FILE
#define LOG_PRINT(level_str, fmt, ...)                                      \
    do {                                                                    \
        printf("[%s] %s: ", (level_str), __FILE__);                         \
        printf((fmt), ##__VA_ARGS__);                                       \
        if (LOG_AUTO_NEWLINE) printf("\n");                                 \
    } while (0)

#elif LOG_SHOW_LINE
#define LOG_PRINT(level_str, fmt, ...)                                      \
    do {                                                                    \
        printf("[%s] line %d: ", (level_str), __LINE__);                    \
        printf((fmt), ##__VA_ARGS__);                                       \
        if (LOG_AUTO_NEWLINE) printf("\n");                                 \
    } while (0)

#else
#define LOG_PRINT(level_str, fmt, ...)                                      \
    do {                                                                    \
        printf("[%s] ", (level_str));                                       \
        printf((fmt), ##__VA_ARGS__);                                       \
        if (LOG_AUTO_NEWLINE) printf("\n");                                 \
    } while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...)            LOG_PRINT("DEBUG", (fmt), ##__VA_ARGS__)
#define LOG_DEBUG_NOTAG(fmt, ...)      do { printf((fmt), ##__VA_ARGS__); if (LOG_AUTO_NEWLINE) printf("\n"); } while (0)
#define LOG_BUF_HEX_DEBUG(buf, len)   do { print_buf_hex((const uint8_t *)(buf), (uint32_t)(len)); } while (0)
#else
#define LOG_DEBUG(fmt, ...)            do {} while (0)
#define LOG_DEBUG_NOTAG(fmt, ...)      do {} while (0)
#define LOG_BUF_HEX_DEBUG(buf, len)   do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...)             LOG_PRINT("INFO", (fmt), ##__VA_ARGS__)
#define LOG_INFO_NOTAG(fmt, ...)       do { printf((fmt), ##__VA_ARGS__); if (LOG_AUTO_NEWLINE) printf("\n"); } while (0)
#define LOG_BUF_HEX_INFO(buf, len)    do { print_buf_hex((const uint8_t *)(buf), (uint32_t)(len)); } while (0)
#else
#define LOG_INFO(fmt, ...)             do {} while (0)
#define LOG_INFO_NOTAG(fmt, ...)       do {} while (0)
#define LOG_BUF_HEX_INFO(buf, len)    do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...)             LOG_PRINT("WARN", (fmt), ##__VA_ARGS__)
#define LOG_WARN_NOTAG(fmt, ...)       do { printf((fmt), ##__VA_ARGS__); if (LOG_AUTO_NEWLINE) printf("\n"); } while (0)
#define LOG_BUF_HEX_WARN(buf, len)    do { print_buf_hex((const uint8_t *)(buf), (uint32_t)(len)); } while (0)
#else
#define LOG_WARN(fmt, ...)             do {} while (0)
#define LOG_WARN_NOTAG(fmt, ...)       do {} while (0)
#define LOG_BUF_HEX_WARN(buf, len)    do {} while (0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...)            LOG_PRINT("ERROR", (fmt), ##__VA_ARGS__)
#define LOG_ERROR_NOTAG(fmt, ...)      do { printf((fmt), ##__VA_ARGS__); if (LOG_AUTO_NEWLINE) printf("\n"); } while (0)
#define LOG_BUF_HEX_ERROR(buf, len)   do { print_buf_hex((const uint8_t *)(buf), (uint32_t)(len)); } while (0)
#else
#define LOG_ERROR(fmt, ...)            do {} while (0)
#define LOG_ERROR_NOTAG(fmt, ...)      do {} while (0)
#define LOG_BUF_HEX_ERROR(buf, len)   do {} while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif // TOOLS_LOG_H_
