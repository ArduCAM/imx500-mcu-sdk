# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(${CMAKE_CURRENT_LIST_DIR}/../integration_test/pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)

project(imx500_production_test C CXX ASM)

pico_sdk_init()

set(_production_test_boot_trigger_mode_default "OFF")
if(DEFINED PRODUCTION_TEST_CONTINUOUS_LED_MODE)
    set(_production_test_boot_trigger_mode_default "${PRODUCTION_TEST_CONTINUOUS_LED_MODE}")
endif()

set(PRODUCTION_TEST_BOOT_TRIGGER_MODE "${_production_test_boot_trigger_mode_default}" CACHE STRING "Enable reusable BOOT-button-triggered production test mode with builtin LED indication: ON or OFF")
set_property(CACHE PRODUCTION_TEST_BOOT_TRIGGER_MODE PROPERTY STRINGS OFF ON)

if(DEFINED PRODUCTION_TEST_CONTINUOUS_LED_MODE AND PRODUCTION_TEST_BOOT_TRIGGER_MODE STREQUAL PRODUCTION_TEST_CONTINUOUS_LED_MODE)
    message(WARNING "PRODUCTION_TEST_CONTINUOUS_LED_MODE is deprecated. Use PRODUCTION_TEST_BOOT_TRIGGER_MODE instead.")
    unset(PRODUCTION_TEST_CONTINUOUS_LED_MODE CACHE)
endif()

add_executable(imx500_production_test
        ${IMX500_FIRMWARE_CPP_FILES}
        ${IMX500_MCU_SDK_SRC_FILES}
        ${CMAKE_CURRENT_LIST_DIR}/../peripherals_adapter.c
        main.cpp
        )

target_include_directories(imx500_production_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/../integration_test
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

if(PRODUCTION_TEST_BOOT_TRIGGER_MODE STREQUAL "ON")
    target_compile_definitions(imx500_production_test PRIVATE
        PRODUCTION_TEST_BOOT_TRIGGER_MODE=1
    )
elseif(PRODUCTION_TEST_BOOT_TRIGGER_MODE STREQUAL "OFF")
    target_compile_definitions(imx500_production_test PRIVATE
        PRODUCTION_TEST_BOOT_TRIGGER_MODE=0
    )
else()
    message(FATAL_ERROR "Unsupported PRODUCTION_TEST_BOOT_TRIGGER_MODE=${PRODUCTION_TEST_BOOT_TRIGGER_MODE}, expected OFF or ON")
endif()

target_link_libraries(imx500_production_test pico_stdlib hardware_spi hardware_i2c hardware_dma)

pico_add_extra_outputs(imx500_production_test)
pico_enable_stdio_usb(imx500_production_test 1)
pico_enable_stdio_uart(imx500_production_test 0)
