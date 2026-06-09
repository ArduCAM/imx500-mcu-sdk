#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// File size
extern const size_t firmware_size;

// File content
extern const uint8_t firmware_data[321488];

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_H
