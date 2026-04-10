get_filename_component(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
get_filename_component(REPO_ROOT "${IMX500_SDK_DIR}/.." ABSOLUTE)
set(IMX500_FILE2CPP_SCRIPT "${IMX500_SDK_DIR}/tools/file2cpp.py")
set(IMX500_GENERATED_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_imx500_models")

set(INTEGRATION_TEST_BOOT_MODE "DIRECT" CACHE STRING "integration_test boot mode: FLASH or DIRECT")
set_property(CACHE INTEGRATION_TEST_BOOT_MODE PROPERTY STRINGS FLASH DIRECT)
set(INTEGRATION_TEST_SPI_METADATA_FORMAT "SPI_METADATA_OUTPUT_TENSOR" CACHE STRING
    "integration_test SPI metadata format: OUTPUT_TENSOR/SPI_METADATA_OUTPUT_TENSOR or JPEG_INPUT_TENSOR_OUTPUT_TENSOR/SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR")
set_property(CACHE INTEGRATION_TEST_SPI_METADATA_FORMAT PROPERTY STRINGS
    OUTPUT_TENSOR
    SPI_METADATA_OUTPUT_TENSOR
    JPEG_INPUT_TENSOR_OUTPUT_TENSOR
    SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR)

set(IMX500_HIGHERHRNET_FPK "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network.fpk")
set(IMX500_HIGHERHRNET_INFO "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network_info.txt")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT EXISTS "${IMX500_FILE2CPP_SCRIPT}")
        message(FATAL_ERROR "IMX500 file2cpp converter not found: ${IMX500_FILE2CPP_SCRIPT}")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    macro(imx500_embed_asset input_file asset_name)
        if(NOT EXISTS "${input_file}")
            message(FATAL_ERROR "Embedded asset not found: ${input_file}")
        endif()

        set(staged_input "${IMX500_GENERATED_MODEL_DIR}/${asset_name}.bin")
        set(generated_header "${IMX500_GENERATED_MODEL_DIR}/${asset_name}.h")
        set(generated_source "${IMX500_GENERATED_MODEL_DIR}/${asset_name}.cpp")

        add_custom_command(
            OUTPUT
                "${generated_header}"
                "${generated_source}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${IMX500_GENERATED_MODEL_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${input_file}" "${staged_input}"
            COMMAND "${Python3_EXECUTABLE}" "${IMX500_FILE2CPP_SCRIPT}" "${staged_input}" "${IMX500_GENERATED_MODEL_DIR}"
            DEPENDS
                "${input_file}"
                "${IMX500_FILE2CPP_SCRIPT}"
            COMMENT "Generating embedded asset ${asset_name} from ${input_file}"
            VERBATIM)

        list(APPEND IMX500_GENERATED_ASSET_SRCS "${generated_source}")
    endmacro()

    set(IMX500_GENERATED_ASSET_SRCS)
    imx500_embed_asset("${IMX500_HIGHERHRNET_FPK}" "higherhrnet_fpk")
    imx500_embed_asset("${IMX500_HIGHERHRNET_INFO}" "higherhrnet_network_info")
endif()

set(srcs
    "imx500_sdk_integration_test_main.c"
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "peripherals_adapter.c"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc"
    "${IMX500_SDK_DIR}/ai_driver.c"
    ${IMX500_GENERATED_ASSET_SRCS})

set(imx500_sdk_test_srcs
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc")

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS
                       "."
                       "${CMAKE_CURRENT_BINARY_DIR}"
                       "${IMX500_SDK_DIR}"
                       "${REPO_ROOT}"
                       "${REPO_ROOT}/imx500_firmware_cpp"
                       "${REPO_ROOT}/third_party/flatbuffers/include")

if(INTEGRATION_TEST_BOOT_MODE STREQUAL "DIRECT")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_DIRECT)
elseif(INTEGRATION_TEST_BOOT_MODE STREQUAL "FLASH")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_BOOT_MODE=INTEGRATION_TEST_BOOT_MODE_FLASH)
else()
    message(FATAL_ERROR "Unsupported INTEGRATION_TEST_BOOT_MODE=${INTEGRATION_TEST_BOOT_MODE}, expected FLASH or DIRECT")
endif()

if(INTEGRATION_TEST_SPI_METADATA_FORMAT STREQUAL "OUTPUT_TENSOR" OR
   INTEGRATION_TEST_SPI_METADATA_FORMAT STREQUAL "SPI_METADATA_OUTPUT_TENSOR")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_SPI_METADATA_FORMAT=INTEGRATION_TEST_SPI_METADATA_FORMAT_OUTPUT_TENSOR)
elseif(INTEGRATION_TEST_SPI_METADATA_FORMAT STREQUAL "JPEG_INPUT_TENSOR_OUTPUT_TENSOR" OR
       INTEGRATION_TEST_SPI_METADATA_FORMAT STREQUAL "SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INTEGRATION_TEST_SPI_METADATA_FORMAT=INTEGRATION_TEST_SPI_METADATA_FORMAT_JPEG_INPUT_TENSOR_OUTPUT_TENSOR)
else()
    message(FATAL_ERROR "Unsupported INTEGRATION_TEST_SPI_METADATA_FORMAT=${INTEGRATION_TEST_SPI_METADATA_FORMAT}, expected OUTPUT_TENSOR/SPI_METADATA_OUTPUT_TENSOR or JPEG_INPUT_TENSOR_OUTPUT_TENSOR/SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR")
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(${imx500_sdk_test_srcs}
        PROPERTIES COMPILE_DEFINITIONS "open=imx500_sdk_open")
endif()
