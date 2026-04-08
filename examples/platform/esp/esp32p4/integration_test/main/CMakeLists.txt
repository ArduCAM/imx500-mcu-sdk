set(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../..")
set(REPO_ROOT "${IMX500_SDK_DIR}/..")
set(IMX500_FIRMWARE_CPP_DIR "${REPO_ROOT}/imx500_firmware_cpp/imx500_firmware")

set(srcs
    "imx500_sdk_integration_test_main.c"
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "peripherals_adapter.c"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc"
    "${IMX500_SDK_DIR}/ai_driver.c")

set(imx500_sdk_test_srcs
    "imx500_sdk_integration_test.cpp"
    "higherhrnet_postprocess.cpp"
    "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc")

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS
                       "."
                       "${IMX500_SDK_DIR}"
                       "${REPO_ROOT}"
                       "${REPO_ROOT}/imx500_firmware_cpp"
                       "${REPO_ROOT}/third_party/flatbuffers/include")

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(${imx500_sdk_test_srcs}
        PROPERTIES COMPILE_DEFINITIONS "open=imx500_sdk_open")
endif()
