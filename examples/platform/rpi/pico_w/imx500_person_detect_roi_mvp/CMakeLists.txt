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

project(imx500_person_detect_roi_mvp C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

set(WIFI_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/.env" CACHE FILEPATH "Path to .env file containing WIFI_SSID and WIFI_PASSWORD")
set(WIFI_CONNECT_TIMEOUT_MS "30000" CACHE STRING "Wi-Fi connect timeout in milliseconds")
set(WIFI_COUNTRY "CN" CACHE STRING "Two-letter Wi-Fi country code")
set(PERSON_CLASS_ID "0" CACHE STRING "SSDMobileNet class id for person")
set(PERSON_SCORE_THRESHOLD "0.30" CACHE STRING "Minimum detection score for person boxes")
set(METADATA_BUFFER_SIZE "65536" CACHE STRING "SPI metadata frame buffer size in bytes")
set(METADATA_DEBUG_FRAMES "5" CACHE STRING "Number of metadata parse failures to dump in detail")
set(SSD_EXPECTED_OUTPUT_BYTES "1102" CACHE STRING "Expected raw SSD output tensor bytes for diagnostics only")
set(IMX500_STREAM_FPS "10" CACHE STRING "IMX500 stream frame rate")
set(IMX500_SPI_METADATA_FORMAT "SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR" CACHE STRING "SPI metadata format for the ROI backend")

if(NOT WIFI_CONNECT_TIMEOUT_MS MATCHES "^[0-9]+$")
    message(FATAL_ERROR "WIFI_CONNECT_TIMEOUT_MS must be an integer millisecond value")
endif()
if(NOT PERSON_CLASS_ID MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PERSON_CLASS_ID must be an integer class id")
endif()
if(NOT METADATA_BUFFER_SIZE MATCHES "^[0-9]+$")
    message(FATAL_ERROR "METADATA_BUFFER_SIZE must be an integer byte count")
endif()
if(NOT METADATA_DEBUG_FRAMES MATCHES "^[0-9]+$")
    message(FATAL_ERROR "METADATA_DEBUG_FRAMES must be an integer frame count")
endif()
if(NOT SSD_EXPECTED_OUTPUT_BYTES MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SSD_EXPECTED_OUTPUT_BYTES must be an integer byte count")
endif()
if(NOT IMX500_STREAM_FPS MATCHES "^[0-9]+$")
    message(FATAL_ERROR "IMX500_STREAM_FPS must be an integer fps value")
endif()
if(NOT IMX500_SPI_METADATA_FORMAT STREQUAL "SPI_METADATA_OUTPUT_TENSOR" AND
   NOT IMX500_SPI_METADATA_FORMAT STREQUAL "SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR")
    message(FATAL_ERROR "IMX500_SPI_METADATA_FORMAT must be SPI_METADATA_OUTPUT_TENSOR or SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR")
endif()

function(load_dotenv DOTENV_FILE)
    if(NOT EXISTS "${DOTENV_FILE}")
        message(FATAL_ERROR
            "Wi-Fi .env file not found: ${DOTENV_FILE}\n"
            "Create ${CMAKE_CURRENT_LIST_DIR}/.env from .env.example, or pass -DWIFI_ENV_FILE=/path/to/.env"
        )
    endif()

    file(STRINGS "${DOTENV_FILE}" DOTENV_LINES)
    foreach(DOTENV_LINE IN LISTS DOTENV_LINES)
        string(STRIP "${DOTENV_LINE}" DOTENV_LINE)
        if(DOTENV_LINE STREQUAL "" OR DOTENV_LINE MATCHES "^#")
            continue()
        endif()

        if(DOTENV_LINE MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*(.*)$")
            set(DOTENV_KEY "${CMAKE_MATCH_1}")
            set(DOTENV_VALUE "${CMAKE_MATCH_2}")
            string(STRIP "${DOTENV_VALUE}" DOTENV_VALUE)

            if(DOTENV_VALUE MATCHES "^\"(.*)\"$")
                set(DOTENV_VALUE "${CMAKE_MATCH_1}")
            elseif(DOTENV_VALUE MATCHES "^'(.*)'$")
                set(DOTENV_VALUE "${CMAKE_MATCH_1}")
            endif()

            set("${DOTENV_KEY}" "${DOTENV_VALUE}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

function(escape_c_string INPUT OUTPUT)
    set(VALUE "${INPUT}")
    string(REPLACE "\\" "\\\\" VALUE "${VALUE}")
    string(REPLACE "\"" "\\\"" VALUE "${VALUE}")
    set("${OUTPUT}" "${VALUE}" PARENT_SCOPE)
endfunction()

load_dotenv("${WIFI_ENV_FILE}")

if(NOT DEFINED WIFI_SSID OR WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "WIFI_SSID is missing or empty in ${WIFI_ENV_FILE}")
endif()

if(NOT DEFINED WIFI_PASSWORD)
    message(FATAL_ERROR "WIFI_PASSWORD is missing in ${WIFI_ENV_FILE}")
endif()

string(LENGTH "${WIFI_COUNTRY}" WIFI_COUNTRY_LENGTH)
if(NOT WIFI_COUNTRY_LENGTH EQUAL 2)
    message(FATAL_ERROR "WIFI_COUNTRY must be a two-letter country code, got '${WIFI_COUNTRY}'")
endif()

escape_c_string("${WIFI_SSID}" WIFI_SSID_ESCAPED)
escape_c_string("${WIFI_PASSWORD}" WIFI_PASSWORD_ESCAPED)
string(SUBSTRING "${WIFI_COUNTRY}" 0 1 WIFI_COUNTRY_CHAR0)
string(SUBSTRING "${WIFI_COUNTRY}" 1 1 WIFI_COUNTRY_CHAR1)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/wifi_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/wifi_config.h
    @ONLY
)

add_executable(imx500_person_detect_roi_mvp
    ${IMX500_MCU_SDK_SRC_FILES}
    ${CMAKE_CURRENT_LIST_DIR}/../peripherals_adapter.c
    main.cpp
)

target_include_directories(imx500_person_detect_roi_mvp PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../imx500_firmware_cpp/imx500_firmware
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/flatbuffers/include
)

target_compile_definitions(imx500_person_detect_roi_mvp PRIVATE
    WIFI_CONNECT_TIMEOUT_MS=${WIFI_CONNECT_TIMEOUT_MS}
    PERSON_CLASS_ID=${PERSON_CLASS_ID}
    PERSON_SCORE_THRESHOLD=${PERSON_SCORE_THRESHOLD}
    METADATA_BUFFER_SIZE=${METADATA_BUFFER_SIZE}
    METADATA_DEBUG_FRAMES=${METADATA_DEBUG_FRAMES}
    SSD_EXPECTED_OUTPUT_BYTES=${SSD_EXPECTED_OUTPUT_BYTES}
    IMX500_STREAM_FPS=${IMX500_STREAM_FPS}
    IMX500_SPI_METADATA_FORMAT=${IMX500_SPI_METADATA_FORMAT}
    CYW43_HOST_NAME="imx500-roi-mvp"
)

# Pull in basic dependencies
target_link_libraries(imx500_person_detect_roi_mvp
    pico_stdlib
    pico_multicore
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_spi
    hardware_i2c
    hardware_dma
)

# create map/bin/hex file etc.
pico_add_extra_outputs(imx500_person_detect_roi_mvp)
pico_enable_stdio_usb(imx500_person_detect_roi_mvp 1)
pico_enable_stdio_uart(imx500_person_detect_roi_mvp 0)
# add url via pico_set_program_url
