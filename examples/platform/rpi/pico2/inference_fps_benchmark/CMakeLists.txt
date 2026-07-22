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
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2 CACHE STRING "Board type")

include(${CMAKE_CURRENT_LIST_DIR}/../integration_test/pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)

project(imx500_inference_fps_benchmark C CXX ASM)

pico_sdk_init()

set(BENCHMARK_SPI_FORMAT "JPEG_INPUT_OUTPUT" CACHE STRING
    "SPI metadata benchmark format: OUTPUT_ONLY or JPEG_INPUT_OUTPUT")
set_property(CACHE BENCHMARK_SPI_FORMAT PROPERTY STRINGS OUTPUT_ONLY JPEG_INPUT_OUTPUT)
set(BENCHMARK_SPI_BAUDRATE_HZ "20000000" CACHE STRING "SPI clock in Hz")
set(BENCHMARK_SENSOR_FPS "30" CACHE STRING "Requested IMX500 sensor/inference FPS")
set(BENCHMARK_WARMUP_FRAMES "10" CACHE STRING "Frames discarded before measurement")
set(BENCHMARK_WARMUP_RETRIES "8" CACHE STRING "Failed warmup reads tolerated before aborting")
set(BENCHMARK_MEASURE_FRAMES "300" CACHE STRING "Frames included in measurement")
set(BENCHMARK_MAX_METADATA_BYTES "2097152" CACHE STRING "Reject metadata frames larger than this")

foreach(NUMERIC_OPTION
        BENCHMARK_SPI_BAUDRATE_HZ
        BENCHMARK_SENSOR_FPS
        BENCHMARK_WARMUP_FRAMES
        BENCHMARK_WARMUP_RETRIES
        BENCHMARK_MEASURE_FRAMES
        BENCHMARK_MAX_METADATA_BYTES)
    if(NOT ${NUMERIC_OPTION} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${NUMERIC_OPTION} must be a non-negative integer")
    endif()
endforeach()

if(BENCHMARK_SPI_BAUDRATE_HZ EQUAL 0 OR
   BENCHMARK_SENSOR_FPS EQUAL 0 OR
   BENCHMARK_WARMUP_RETRIES EQUAL 0 OR
   BENCHMARK_MEASURE_FRAMES EQUAL 0 OR
   BENCHMARK_MAX_METADATA_BYTES EQUAL 0)
    message(FATAL_ERROR
        "SPI baudrate, sensor FPS, warmup retries, measured frames, and max metadata bytes must be > 0")
endif()

add_executable(imx500_inference_fps_benchmark
    ${IMX500_MCU_SDK_SRC_FILES}
    ${CMAKE_CURRENT_LIST_DIR}/../peripherals_adapter.c
    main.cpp
)

target_include_directories(imx500_inference_fps_benchmark PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

imx500_mcu_sdk_apply_config(imx500_inference_fps_benchmark)

if(BENCHMARK_SPI_FORMAT STREQUAL "OUTPUT_ONLY")
    target_compile_definitions(imx500_inference_fps_benchmark PRIVATE
        BENCHMARK_SPI_MODE=BENCHMARK_SPI_MODE_OUTPUT_ONLY
    )
elseif(BENCHMARK_SPI_FORMAT STREQUAL "JPEG_INPUT_OUTPUT")
    target_compile_definitions(imx500_inference_fps_benchmark PRIVATE
        BENCHMARK_SPI_MODE=BENCHMARK_SPI_MODE_JPEG_INPUT_OUTPUT
    )
else()
    message(FATAL_ERROR
        "Unsupported BENCHMARK_SPI_FORMAT=${BENCHMARK_SPI_FORMAT}; "
        "expected OUTPUT_ONLY or JPEG_INPUT_OUTPUT")
endif()

target_compile_definitions(imx500_inference_fps_benchmark PRIVATE
    BENCHMARK_SPI_BAUDRATE_HZ=${BENCHMARK_SPI_BAUDRATE_HZ}
    BENCHMARK_SENSOR_FPS=${BENCHMARK_SENSOR_FPS}
    BENCHMARK_WARMUP_FRAMES=${BENCHMARK_WARMUP_FRAMES}
    BENCHMARK_WARMUP_RETRIES=${BENCHMARK_WARMUP_RETRIES}
    BENCHMARK_MEASURE_FRAMES=${BENCHMARK_MEASURE_FRAMES}
    BENCHMARK_MAX_METADATA_BYTES=${BENCHMARK_MAX_METADATA_BYTES}
)

target_link_libraries(imx500_inference_fps_benchmark
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_dma
)

pico_add_extra_outputs(imx500_inference_fps_benchmark)
pico_enable_stdio_usb(imx500_inference_fps_benchmark 1)
pico_enable_stdio_uart(imx500_inference_fps_benchmark 0)

message(STATUS "IMX500 benchmark SPI format: ${BENCHMARK_SPI_FORMAT}")
message(STATUS "IMX500 benchmark SPI clock: ${BENCHMARK_SPI_BAUDRATE_HZ} Hz")
message(STATUS "IMX500 benchmark frames: ${BENCHMARK_WARMUP_FRAMES} warmup + ${BENCHMARK_MEASURE_FRAMES} measured")
