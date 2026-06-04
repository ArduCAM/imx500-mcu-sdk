#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#define LOG_INFO(...)  printf("[INFO] " __VA_ARGS__)
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) printf("[ERROR] " __VA_ARGS__)

#endif  // LOG_H
