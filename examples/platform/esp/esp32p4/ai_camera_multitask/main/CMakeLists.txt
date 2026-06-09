set(AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK ON CACHE BOOL "Enable the Arducam IMX500 SDK flow" FORCE)

set(srcs
    "ai_camera_multitask_main.c"
    "local_camera_display.c"
    "display_compose_helper.c")

set(priv_include_dirs
    ".")

if(AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK)
    get_filename_component(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
    set(IMX500_FILE2CPP_SCRIPT "${IMX500_SDK_DIR}/tools/file2cpp.py")
    if(DEFINED INTEGRATION_TEST_BOOT_MODE AND NOT DEFINED AI_CAMERA_MULTITASK_BOOT_MODE)
        set(AI_CAMERA_MULTITASK_BOOT_MODE "${INTEGRATION_TEST_BOOT_MODE}" CACHE STRING
            "ai_camera_multitask boot mode: FLASH or DIRECT")
    else()
        set(AI_CAMERA_MULTITASK_BOOT_MODE "DIRECT" CACHE STRING "ai_camera_multitask boot mode: FLASH or DIRECT")
    endif()
    set_property(CACHE AI_CAMERA_MULTITASK_BOOT_MODE PROPERTY STRINGS FLASH DIRECT)
    set(AI_CAMERA_MULTITASK_SPI_METADATA_MODE "JPEG_INPUT_OUTPUT" CACHE STRING
        "ai_camera_multitask SPI metadata mode: OUTPUT_TENSOR_ONLY or JPEG_INPUT_OUTPUT")
    set_property(CACHE AI_CAMERA_MULTITASK_SPI_METADATA_MODE PROPERTY STRINGS OUTPUT_TENSOR_ONLY JPEG_INPUT_OUTPUT)
    set(IMX500_GENERATED_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_imx500_models")

    set(IMX500_MOBILENET_V2_FPK "${IMX500_SDK_DIR}/tools/assets/models/mobilenet_v2/imx500_network_mobilenet_v2.fpk")
    set(IMX500_MOBILENET_V2_INFO "${IMX500_SDK_DIR}/tools/assets/models/mobilenet_v2/network_info.txt")
    set(IMX500_SSD_FPNLITE_FPK "${IMX500_SDK_DIR}/tools/assets/models/ssd_mobilenetv2_fpnlite/imx500_network_ssd_mobilenetv2_fpnlite_320x320_pp.fpk")
    set(IMX500_SSD_FPNLITE_INFO "${IMX500_SDK_DIR}/tools/assets/models/ssd_mobilenetv2_fpnlite/network_info.txt")
    set(IMX500_HIGHERHRNET_FPK "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network.fpk")
    set(IMX500_HIGHERHRNET_INFO "${IMX500_SDK_DIR}/tools/assets/models/higherhrnet/network_info.txt")
    set(IMX500_DEEPLABV3PLUS_FPK "${IMX500_SDK_DIR}/tools/assets/models/deeplabv3plus/imx500_network_deeplabv3plus.fpk")
    set(IMX500_DEEPLABV3PLUS_INFO "${IMX500_SDK_DIR}/tools/assets/models/deeplabv3plus/network_info.txt")
    set(IMX500_IMAGENET_LABELS "${IMX500_SDK_DIR}/labels/imagenet_labels.txt")
    set(IMX500_COCO_SSD_LABELS "${IMX500_SDK_DIR}/labels/coco_ssd.txt")
    set(IMX500_VOC20_SEG_LABELS "${IMX500_SDK_DIR}/labels/voc20_segmentation_labels.txt")

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

            list(APPEND IMX500_GENERATED_ASSET_SRCS "imx500_multitask_ui.cpp" "imx500_multitask_postprocess.cpp" "${generated_source}")
        endmacro()

        set(IMX500_GENERATED_ASSET_SRCS)
        imx500_embed_asset("${IMX500_MOBILENET_V2_FPK}" "mobilenet_v2_fpk")
        imx500_embed_asset("${IMX500_MOBILENET_V2_INFO}" "mobilenet_v2_network_info")
        imx500_embed_asset("${IMX500_SSD_FPNLITE_FPK}" "ssd_mobilenetv2_fpnlite_fpk")
        imx500_embed_asset("${IMX500_SSD_FPNLITE_INFO}" "ssd_mobilenetv2_fpnlite_network_info")
        imx500_embed_asset("${IMX500_HIGHERHRNET_FPK}" "higherhrnet_fpk")
        imx500_embed_asset("${IMX500_HIGHERHRNET_INFO}" "higherhrnet_network_info")
        imx500_embed_asset("${IMX500_DEEPLABV3PLUS_FPK}" "deeplabv3plus_fpk")
        imx500_embed_asset("${IMX500_DEEPLABV3PLUS_INFO}" "deeplabv3plus_network_info")
        imx500_embed_asset("${IMX500_IMAGENET_LABELS}" "imagenet_labels")
        imx500_embed_asset("${IMX500_COCO_SSD_LABELS}" "coco_ssd_labels")
        imx500_embed_asset("${IMX500_VOC20_SEG_LABELS}" "voc20_segmentation_labels")
    endif()

    list(APPEND srcs
        "ai_camera_multitask.cpp"
        "imx500_multitask_model_registry.cpp"
        "imx500_multitask_postprocess.cpp"
        "imx500_multitask_ui.cpp"
        "higherhrnet_postprocess.cpp"
        "peripherals_adapter.c"
        "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc"
        "${IMX500_SDK_DIR}/ai_driver.c"
        ${IMX500_GENERATED_ASSET_SRCS})

    set(ai_camera_multitask_srcs
        "ai_camera_multitask.cpp"
        "imx500_multitask_model_registry.cpp"
        "imx500_multitask_postprocess.cpp"
        "imx500_multitask_ui.cpp"
        "higherhrnet_postprocess.cpp"
        "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc")

    list(APPEND priv_include_dirs
        "${IMX500_SDK_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${IMX500_SDK_DIR}/third_party/flatbuffers/include")
endif()

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs})

if(AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK=1)
    imx500_mcu_sdk_apply_config(${COMPONENT_LIB})

    if(AI_CAMERA_MULTITASK_BOOT_MODE STREQUAL "DIRECT")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            AI_CAMERA_MULTITASK_BOOT_MODE=AI_CAMERA_MULTITASK_BOOT_MODE_DIRECT)
    elseif(AI_CAMERA_MULTITASK_BOOT_MODE STREQUAL "FLASH")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            AI_CAMERA_MULTITASK_BOOT_MODE=AI_CAMERA_MULTITASK_BOOT_MODE_FLASH)
    else()
        message(FATAL_ERROR "Unsupported AI_CAMERA_MULTITASK_BOOT_MODE=${AI_CAMERA_MULTITASK_BOOT_MODE}, expected FLASH or DIRECT")
    endif()

    if(AI_CAMERA_MULTITASK_SPI_METADATA_MODE STREQUAL "OUTPUT_TENSOR_ONLY")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            AI_CAMERA_MULTITASK_SPI_METADATA_MODE=AI_CAMERA_MULTITASK_SPI_METADATA_MODE_OUTPUT_TENSOR_ONLY)
    elseif(AI_CAMERA_MULTITASK_SPI_METADATA_MODE STREQUAL "JPEG_INPUT_OUTPUT")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            AI_CAMERA_MULTITASK_SPI_METADATA_MODE=AI_CAMERA_MULTITASK_SPI_METADATA_MODE_JPEG_INPUT_OUTPUT)
    else()
        message(FATAL_ERROR "Unsupported AI_CAMERA_MULTITASK_SPI_METADATA_MODE=${AI_CAMERA_MULTITASK_SPI_METADATA_MODE}, expected OUTPUT_TENSOR_ONLY or JPEG_INPUT_OUTPUT")
    endif()

    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_source_files_properties(${ai_camera_multitask_srcs}
            PROPERTIES COMPILE_DEFINITIONS "open=imx500_sdk_open")
    endif()
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AI_CAMERA_MULTITASK_ENABLE_IMX500_SDK=0)
endif()
