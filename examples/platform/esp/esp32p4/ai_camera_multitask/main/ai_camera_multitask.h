#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CAMERA_MULTITASK_BOOT_MODE_DIRECT 0
#define AI_CAMERA_MULTITASK_BOOT_MODE_FLASH  1

#ifndef AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK
#define AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK 1
#endif

#ifndef AI_CAMERA_MULTITASK_BOOT_MODE
#define AI_CAMERA_MULTITASK_BOOT_MODE AI_CAMERA_MULTITASK_BOOT_MODE_FLASH
#endif

esp_err_t ai_camera_multitask_run(void);
esp_err_t ai_camera_multitask_run_local_preview(void);

#ifdef __cplusplus
}
#endif
