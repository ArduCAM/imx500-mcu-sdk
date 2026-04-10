get_filename_component(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
set(IMX500_FIRMWARE_CPP_DIR "${IMX500_SDK_DIR}/imx500_firmware_cpp/imx500_firmware")
set(IMX500_MODEL_DIR "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet" CACHE PATH
    "Path to the IMX500 model directory that contains network.fpk and network_info.txt")
set(IMX500_MODEL_FPK "${IMX500_MODEL_DIR}/network.fpk")
set(IMX500_MODEL_INFO "${IMX500_MODEL_DIR}/network_info.txt")
set(IMX500_FILE2CPP_SCRIPT "${IMX500_SDK_DIR}/tools/file2cpp.py")
set(INTEGRATION_TEST_BOOT_MODE "DIRECT" CACHE STRING "ai_camera_multitask boot mode: FLASH or DIRECT")
set_property(CACHE INTEGRATION_TEST_BOOT_MODE PROPERTY STRINGS FLASH DIRECT)
set(IMX500_GENERATED_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_imx500_models")
set(IMX500_GENERATED_MODEL_FPK_H "${IMX500_GENERATED_MODEL_DIR}/network.h")
set(IMX500_GENERATED_MODEL_FPK_CPP "${IMX500_GENERATED_MODEL_DIR}/network.cpp")
set(IMX500_GENERATED_MODEL_INFO_H "${IMX500_GENERATED_MODEL_DIR}/network_info.h")
set(IMX500_GENERATED_MODEL_INFO_CPP "${IMX500_GENERATED_MODEL_DIR}/network_info.cpp")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${IMX500_MODEL_FPK}")
        message(FATAL_ERROR "IMX500 model file not found: ${IMX500_MODEL_FPK}")
    endif()

    if(NOT EXISTS "${IMX500_MODEL_INFO}")
        message(FATAL_ERROR "IMX500 network info file not found: ${IMX500_MODEL_INFO}")
    endif()

    if(NOT EXISTS "${IMX500_FILE2CPP_SCRIPT}")
        message(FATAL_ERROR "IMX500 file2cpp converter not found: ${IMX500_FILE2CPP_SCRIPT}")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_custom_command(
        OUTPUT
            "${IMX500_GENERATED_MODEL_FPK_H}"
            "${IMX500_GENERATED_MODEL_FPK_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IMX500_GENERATED_MODEL_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${IMX500_FILE2CPP_SCRIPT}" "${IMX500_MODEL_FPK}" "${IMX500_GENERATED_MODEL_DIR}"
        DEPENDS
            "${IMX500_MODEL_FPK}"
            "${IMX500_FILE2CPP_SCRIPT}"
        COMMENT "Generating IMX500 embedded model sources from ${IMX500_MODEL_FPK}"
        VERBATIM)

    add_custom_command(
        OUTPUT
            "${IMX500_GENERATED_MODEL_INFO_H}"
            "${IMX500_GENERATED_MODEL_INFO_CPP}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${IMX500_GENERATED_MODEL_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${IMX500_FILE2CPP_SCRIPT}" "${IMX500_MODEL_INFO}" "${IMX500_GENERATED_MODEL_DIR}"
        DEPENDS
            "${IMX500_MODEL_INFO}"
            "${IMX500_FILE2CPP_SCRIPT}"
        COMMENT "Generating IMX500 network info sources from ${IMX500_MODEL_INFO}"
        VERBATIM)
endif()

set(srcs
    "imx500_sdk_integration_test_main.c"
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "peripherals_adapter.c"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc"
    "${IMX500_SDK_DIR}/ai_driver.c"
    "${IMX500_GENERATED_MODEL_FPK_CPP}"
    "${IMX500_GENERATED_MODEL_INFO_CPP}")

set(imx500_sdk_test_srcs
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc")

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS
                       "."
                       "${IMX500_SDK_DIR}"
                       "${CMAKE_CURRENT_BINARY_DIR}"
                       "${IMX500_SDK_DIR}/imx500_firmware_cpp"
                       "${IMX500_SDK_DIR}/third_party/flatbuffers/include")

if(INTEGRATION_TEST_BOOT_MODE STREQUAL "DIRECT")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_DIRECT)
elseif(INTEGRATION_TEST_BOOT_MODE STREQUAL "FLASH")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_FLASH)
else()
    message(FATAL_ERROR "Unsupported INTEGRATION_TEST_BOOT_MODE=${INTEGRATION_TEST_BOOT_MODE}, expected FLASH or DIRECT")
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(${imx500_sdk_test_srcs}
        PROPERTIES COMPILE_DEFINITIONS "open=imx500_sdk_open")
endif()
