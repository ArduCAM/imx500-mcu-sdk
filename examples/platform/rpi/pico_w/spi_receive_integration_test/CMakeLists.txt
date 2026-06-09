# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
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
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)


project(spi_receive_integration_test C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(INTEGRATION_TEST_BOOT_MODE "FLASH" CACHE STRING "integration_test boot mode: FLASH or DIRECT")
set_property(CACHE INTEGRATION_TEST_BOOT_MODE PROPERTY STRINGS FLASH DIRECT)
set(INTEGRATION_TEST_SPI_BAUDRATE_HZ "5000000" CACHE STRING "integration_test SPI baudrate in Hz")

if(NOT INTEGRATION_TEST_SPI_BAUDRATE_HZ MATCHES "^[0-9]+$")
    message(FATAL_ERROR "INTEGRATION_TEST_SPI_BAUDRATE_HZ must be an integer Hz value")
endif()

# Add executable. Default name is the project name, version 0.1

add_executable(spi_receive_integration_test
        ${IMX500_MCU_SDK_SRC_FILES}
        ${CMAKE_CURRENT_LIST_DIR}/../peripherals_adapter.c
        main.cpp
        )

target_include_directories(spi_receive_integration_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
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
    INTEGRATION_TEST_SPI_BAUDRATE_HZ=${INTEGRATION_TEST_SPI_BAUDRATE_HZ}
)

# Pull in basic dependencies
target_link_libraries(spi_receive_integration_test pico_stdlib hardware_spi hardware_i2c hardware_dma)

# create map/bin/hex file etc.
pico_add_extra_outputs(spi_receive_integration_test)
pico_enable_stdio_usb(spi_receive_integration_test 1)
pico_enable_stdio_uart(spi_receive_integration_test 0)
# add url via pico_set_program_url
