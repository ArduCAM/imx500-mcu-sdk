set(IMX500_MICROPY_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(IMX500_MCU_SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    CACHE PATH "Path to the imx500-mcu-sdk repository root")

set(IMX500_MICROPY_BUILD_MODE "full" CACHE STRING "Build mode for the IMX500 MicroPython module")
if(DEFINED ENV{IMX500_MICROPY_BUILD_MODE} AND NOT "$ENV{IMX500_MICROPY_BUILD_MODE}" STREQUAL "")
    set(IMX500_MICROPY_BUILD_MODE "$ENV{IMX500_MICROPY_BUILD_MODE}" CACHE STRING "Build mode for the IMX500 MicroPython module" FORCE)
endif()
set_property(CACHE IMX500_MICROPY_BUILD_MODE PROPERTY STRINGS full minimal)

add_library(usermod_imx500_mcu_sdk INTERFACE)

if(IMX500_MICROPY_BUILD_MODE STREQUAL "minimal")
    target_sources(usermod_imx500_mcu_sdk INTERFACE
        ${IMX500_MICROPY_MODULE_DIR}/modimx500_mcu_sdk_min.cpp
    )

    target_include_directories(usermod_imx500_mcu_sdk INTERFACE
        ${IMX500_MICROPY_MODULE_DIR}
    )

    target_compile_definitions(usermod_imx500_mcu_sdk INTERFACE
        MODULE_IMX500_MCU_SDK_ENABLED=1
    )
elseif(IMX500_MICROPY_BUILD_MODE STREQUAL "full")
    include(${IMX500_MCU_SDK_ROOT}/imx500_mcu_sdk.cmake)

    target_sources(usermod_imx500_mcu_sdk INTERFACE
        ${IMX500_MICROPY_MODULE_DIR}/modimx500_mcu_sdk.cpp
        ${IMX500_MCU_SDK_SRC_FILES}
        ${IMX500_MCU_SDK_ROOT}/examples/platform/rpi/pico/peripherals_adapter.c
    )

    target_include_directories(usermod_imx500_mcu_sdk INTERFACE
        ${IMX500_MICROPY_MODULE_DIR}
        ${IMX500_MCU_SDK_ROOT}
        ${IMX500_MCU_SDK_ROOT}/examples/platform/rpi/pico
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${IMX500_MCU_SDK_ROOT}/imx500_firmware_cpp
        ${IMX500_MCU_SDK_ROOT}/imx500_firmware_cpp/imx500_firmware
        ${IMX500_MCU_SDK_ROOT}/third_party/flatbuffers/include
    )

    target_compile_features(usermod_imx500_mcu_sdk INTERFACE cxx_std_17)

    target_compile_definitions(usermod_imx500_mcu_sdk INTERFACE
        MODULE_IMX500_MCU_SDK_ENABLED=1
        ${IMX500_MCU_SDK_COMPILE_DEFINITIONS}
    )

    target_link_libraries(usermod_imx500_mcu_sdk INTERFACE
        hardware_gpio
        hardware_i2c
        hardware_spi
        pico_time
    )
else()
    message(FATAL_ERROR "Unsupported IMX500_MICROPY_BUILD_MODE='${IMX500_MICROPY_BUILD_MODE}'. Use 'full' or 'minimal'.")
endif()

target_link_libraries(usermod INTERFACE usermod_imx500_mcu_sdk)
