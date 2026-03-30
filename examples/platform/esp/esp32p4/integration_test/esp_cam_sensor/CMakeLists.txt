set(srcs "src/esp_cam_sensor.c" "src/esp_cam_sensor_xclk.c" "src/esp_cam_motor.c")

if(CONFIG_CAM_CTRL_SPI_ENABLE)
    list(APPEND srcs "src/driver_cam/esp_cam_ctlr_spi_cam.c")

    if(CONFIG_SPIRAM)
        list(APPEND srcs "src/driver_spi/spi_slave.c")
    endif()
endif()

if(CONFIG_CAM_CTRL_DVP_ENABLE)
    list(APPEND srcs "src/driver_dvp/esp_cam_ctlr_dvp_cam.c")
endif()

set(include_dirs "include")
set(requires "esp_sccb_intf" "esp_driver_cam" "esp_driver_gpio" "esp_driver_ledc" "esp_driver_spi" "esp_driver_parlio")
set(priv_requires "esp_timer")

if(CONFIG_CAMERA_PIVARIETY)
    list(APPEND srcs "sensors/pivariety/pivariety.c")
    list(APPEND include_dirs "sensors/pivariety/include")
    list(APPEND priv_include_dirs "sensors/pivariety/private_include")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires}
                       LDFRAGMENTS linker.lf)

if(CONFIG_CAMERA_PIVARIETY_AUTO_DETECT_MIPI_INTERFACE_SENSOR OR CONFIG_CAMERA_PIVARIETY_AUTO_DETECT_DVP_INTERFACE_SENSOR)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pivariety_detect")
endif()

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
