set(IMX500_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../..")
set(REPO_ROOT "${IMX500_SDK_DIR}/..")
set(IMX500_FIRMWARE_CPP_DIR "${REPO_ROOT}/imx500_firmware_cpp/imx500_firmware")

set(srcs
    "simple_video_server_example.c")

if(CONFIG_EXAMPLE_ENABLE_IMX500_SDK_TEST)
    set(imx500_sdk_test_srcs
        "imx500_sdk_integration_test.cpp"
        "higherhrnet_postprocess.cpp"
        "${IMX500_SDK_DIR}/ArducamIMX500SDK.cc")
    list(APPEND srcs
        ${imx500_sdk_test_srcs}
        "peripherals_adapter.c"
        "${IMX500_SDK_DIR}/ai_driver.c")

    set_source_files_properties(${imx500_sdk_test_srcs}
        PROPERTIES COMPILE_DEFINITIONS "open=imx500_sdk_open")
endif()

set(html_files
    "${PROJECT_DIR}/frontend/gzipped/index.html.gz"
    "${PROJECT_DIR}/frontend/gzipped/loading.jpg.gz"
    "${PROJECT_DIR}/frontend/gzipped/favicon.ico.gz"
    "${PROJECT_DIR}/frontend/gzipped/assets/index.js.gz"
    "${PROJECT_DIR}/frontend/gzipped/assets/index.css.gz")

idf_component_register(SRCS ${srcs}
                       PRIV_INCLUDE_DIRS
                       "."
                       "${IMX500_SDK_DIR}"
                       "${REPO_ROOT}"
                       "${REPO_ROOT}/imx500_firmware_cpp"
                       "${REPO_ROOT}/third_party/flatbuffers/include"
                       EMBED_TXTFILES ${html_files})
