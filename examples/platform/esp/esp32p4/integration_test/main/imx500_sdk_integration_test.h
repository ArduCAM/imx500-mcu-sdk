#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INTEGRATION_TEST_BOOT_MODE_DIRECT 0
#define INTEGRATION_TEST_BOOT_MODE_FLASH  1

#ifndef INTEGRATION_TEST_BOOT_MODE
#define INTEGRATION_TEST_BOOT_MODE INTEGRATION_TEST_BOOT_MODE_FLASH
#endif

esp_err_t imx500_sdk_integration_test_run(void);

#ifdef __cplusplus
}
#endif
