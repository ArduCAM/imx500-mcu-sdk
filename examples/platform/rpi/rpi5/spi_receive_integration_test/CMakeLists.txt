cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rpi5_spi_receive_integration_test C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This example uses Linux i2c-dev/spidev headers and is intended to build on Raspberry Pi OS.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)

set(INTEGRATION_TEST_BOOT_MODE "FLASH" CACHE STRING "integration_test boot mode: FLASH or DIRECT")
set_property(CACHE INTEGRATION_TEST_BOOT_MODE PROPERTY STRINGS FLASH DIRECT)
set(INTEGRATION_TEST_SPI_BAUDRATE_HZ "5000000" CACHE STRING "integration_test SPI baudrate in Hz")
set(INTEGRATION_TEST_DUMP_FRAME_COUNT "2" CACHE STRING "number of successful pre-injection frames to dump; 0 disables frame dump")

if(NOT INTEGRATION_TEST_SPI_BAUDRATE_HZ MATCHES "^[0-9]+$")
    message(FATAL_ERROR "INTEGRATION_TEST_SPI_BAUDRATE_HZ must be an integer Hz value")
endif()

if(NOT INTEGRATION_TEST_DUMP_FRAME_COUNT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "INTEGRATION_TEST_DUMP_FRAME_COUNT must be a non-negative integer")
endif()

add_executable(spi_receive_integration_test
    ${IMX500_FIRMWARE_CPP_FILES}
    ${IMX500_MCU_SDK_SRC_FILES}
    peripherals_adapter.c
    main.cpp
)

target_include_directories(spi_receive_integration_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/imx500_firmware
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

imx500_mcu_sdk_apply_config(spi_receive_integration_test)

if(INTEGRATION_TEST_BOOT_MODE STREQUAL "DIRECT")
    target_compile_definitions(spi_receive_integration_test PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_DIRECT
    )
elseif(INTEGRATION_TEST_BOOT_MODE STREQUAL "FLASH")
    target_compile_definitions(spi_receive_integration_test PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_FLASH
    )
else()
    message(FATAL_ERROR "Unsupported INTEGRATION_TEST_BOOT_MODE=${INTEGRATION_TEST_BOOT_MODE}, expected FLASH or DIRECT")
endif()

target_compile_definitions(spi_receive_integration_test PRIVATE
    RPI5_SPI_SPEED_HZ=${INTEGRATION_TEST_SPI_BAUDRATE_HZ}
    INTEGRATION_TEST_DUMP_FRAME_COUNT=${INTEGRATION_TEST_DUMP_FRAME_COUNT}
)
