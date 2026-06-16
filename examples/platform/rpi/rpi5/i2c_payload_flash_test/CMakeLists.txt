cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(rpi5_i2c_payload_flash_test C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This example uses Linux i2c-dev headers and is intended to build on Raspberry Pi OS.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_mcu_sdk.cmake)

set(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../")
set(IMX500_FILE2CPP_SCRIPT "${IMX500_SDK_DIR}/tools/file2cpp.py")
set(I2C_PAYLOAD_GENERATED_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_i2c_payload_assets")
set(I2C_PAYLOAD_MODEL_FPK "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network.fpk"
    CACHE FILEPATH
    "Default model FPK embedded by this test.")
set(I2C_PAYLOAD_NETWORK_INFO "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network_info.txt"
    CACHE FILEPATH
    "Default network_info embedded by this test.")
set(I2C_PAYLOAD_I2C_DEVICE "" CACHE STRING "I2C device override, for example /dev/i2c-10. Empty means auto-scan candidates.")
set(I2C_PAYLOAD_I2C_CANDIDATES
    "/dev/i2c-10;/dev/i2c-11;/dev/i2c-12;/dev/i2c-13;/dev/i2c-4;/dev/i2c-6;/dev/i2c-1;/dev/i2c-0"
    CACHE STRING
    "I2C device candidates to probe for the module MCU.")
string(REPLACE ";" ":" I2C_PAYLOAD_I2C_CANDIDATES_STR "${I2C_PAYLOAD_I2C_CANDIDATES}")

set(I2C_PAYLOAD_BASE_FIRMWARE_SRCS
    "${IMX500_SDK_DIR}/imx500_firmware_cpp/imx500_firmware/firmware.cpp"
    "${IMX500_SDK_DIR}/imx500_firmware_cpp/imx500_firmware/loader.cpp")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(i2c_payload_embed_asset input_file asset_name)
    if(NOT EXISTS "${input_file}")
        message(FATAL_ERROR "Embedded asset not found: ${input_file}")
    endif()
    if(NOT EXISTS "${IMX500_FILE2CPP_SCRIPT}")
        message(FATAL_ERROR "IMX500 file2cpp converter not found: ${IMX500_FILE2CPP_SCRIPT}")
    endif()

    set(staged_input "${I2C_PAYLOAD_GENERATED_ASSET_DIR}/${asset_name}.bin")
    set(generated_header "${I2C_PAYLOAD_GENERATED_ASSET_DIR}/${asset_name}.h")
    set(generated_source "${I2C_PAYLOAD_GENERATED_ASSET_DIR}/${asset_name}.cpp")

    add_custom_command(
        OUTPUT
            "${generated_header}"
            "${generated_source}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${I2C_PAYLOAD_GENERATED_ASSET_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${input_file}" "${staged_input}"
        COMMAND "${Python3_EXECUTABLE}" "${IMX500_FILE2CPP_SCRIPT}" "${staged_input}" "${I2C_PAYLOAD_GENERATED_ASSET_DIR}"
        DEPENDS
            "${input_file}"
            "${IMX500_FILE2CPP_SCRIPT}"
        COMMENT "Generating embedded asset ${asset_name} from ${input_file}"
        VERBATIM)

    set(I2C_PAYLOAD_GENERATED_ASSET_SRCS
        ${I2C_PAYLOAD_GENERATED_ASSET_SRCS}
        "${generated_source}"
        PARENT_SCOPE)
endfunction()

set(I2C_PAYLOAD_GENERATED_ASSET_SRCS)
i2c_payload_embed_asset("${I2C_PAYLOAD_MODEL_FPK}" "higherhrnet_fpk")
i2c_payload_embed_asset("${I2C_PAYLOAD_NETWORK_INFO}" "higherhrnet_network_info")

add_executable(i2c_payload_flash_test
    ${I2C_PAYLOAD_BASE_FIRMWARE_SRCS}
    ${IMX500_MCU_SDK_SRC_FILES}
    ${I2C_PAYLOAD_GENERATED_ASSET_SRCS}
    peripherals_adapter.c
    main.cpp
)

target_include_directories(i2c_payload_flash_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
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
