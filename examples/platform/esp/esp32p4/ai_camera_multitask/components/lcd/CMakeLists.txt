set(srcs "app_video.c" "app_lcd.c")
set(inc_dirs "include")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${inc_dirs}")
