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

project(imx500_camera_serial_stream_jpeg C CXX ASM)

pico_sdk_init()

add_executable(imx500_camera_serial_stream_jpeg
        ${IMX500_FIRMWARE_CPP_FILES}
        ${IMX500_MCU_SDK_SRC_FILES}
        ${CMAKE_CURRENT_LIST_DIR}/../peripherals_adapter.c
        main.cpp
        )

target_include_directories(imx500_camera_serial_stream_jpeg PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

target_link_libraries(imx500_camera_serial_stream_jpeg pico_stdlib hardware_spi hardware_i2c hardware_dma)

pico_add_extra_outputs(imx500_camera_serial_stream_jpeg)
pico_enable_stdio_usb(imx500_camera_serial_stream_jpeg 1)
pico_enable_stdio_uart(imx500_camera_serial_stream_jpeg 0)