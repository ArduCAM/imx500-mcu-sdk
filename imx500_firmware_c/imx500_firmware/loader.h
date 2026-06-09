#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// File size
extern const size_t loader_size;

// File content
extern const uint8_t loader_data[29328];

#ifdef __cplusplus
}
#endif

#endif // LOADER_H
