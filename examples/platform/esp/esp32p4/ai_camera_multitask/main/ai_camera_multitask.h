#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CAMERA_MULTITASK_BOOT_MODE_DIRECT 0
#define AI_CAMERA_MULTITASK_BOOT_MODE_FLASH  1

#ifndef AI_CAMERA_MULTITASK_BOOT_MODE
#define AI_CAMERA_MULTITASK_BOOT_MODE AI_CAMERA_MULTITASK_BOOT_MODE_FLASH
#endif

esp_err_t ai_camera_multitask_run(void);

#ifdef __cplusplus
}
#endif
