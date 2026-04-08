set(srcs "example_init_video.c")
set(inc_dirs "include")

if(NOT CONFIG_IDF_TARGET_ESP32C61)
    list(APPEND srcs "example_encoder.c")
endif()

if(CONFIG_EXAMPLE_SELECT_ESP32P4_FUNCTION_EV_BOARD_V1_4)
    list(APPEND inc_dirs "include/boards/esp32-p4-function-ev-board-v1.4")
elseif(CONFIG_EXAMPLE_SELECT_ESP32P4_FUNCTION_EV_BOARD_V1_5)
    list(APPEND inc_dirs "include/boards/esp32-p4-function-ev-board-v1.5")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${inc_dirs}"
                       REQUIRES "esp_video" "esp_driver_jpeg")
