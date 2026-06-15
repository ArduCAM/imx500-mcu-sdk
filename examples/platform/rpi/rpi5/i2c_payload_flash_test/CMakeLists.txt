cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rpi5_i2c_payload_flash_test C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This example uses Linux i2c-dev headers and is intended to build on Raspberry Pi OS.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)

set(I2C_PAYLOAD_I2C_DEVICE "" CACHE STRING "I2C device override, for example /dev/i2c-10. Empty means auto-scan candidates.")
set(I2C_PAYLOAD_I2C_CANDIDATES
    "/dev/i2c-10;/dev/i2c-11;/dev/i2c-12;/dev/i2c-13;/dev/i2c-4;/dev/i2c-6;/dev/i2c-1;/dev/i2c-0"
    CACHE STRING
    "I2C device candidates to probe for the module MCU.")
string(REPLACE ";" ":" I2C_PAYLOAD_I2C_CANDIDATES_STR "${I2C_PAYLOAD_I2C_CANDIDATES}")

add_executable(i2c_payload_flash_test
    ${IMX500_FIRMWARE_CPP_FILES}
    ${IMX500_MCU_SDK_SRC_FILES}
    peripherals_adapter.c
    main.cpp
)

target_include_directories(i2c_payload_flash_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/imx500_firmware
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

imx500_mcu_sdk_apply_config(i2c_payload_flash_test)

target_compile_definitions(i2c_payload_flash_test PRIVATE
    I2C_PAYLOAD_I2C_DEVICE="${I2C_PAYLOAD_I2C_DEVICE}"
    I2C_PAYLOAD_I2C_CANDIDATES="${I2C_PAYLOAD_I2C_CANDIDATES_STR}"
)
