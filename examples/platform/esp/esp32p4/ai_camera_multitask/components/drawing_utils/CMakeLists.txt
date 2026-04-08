set(srcs "app_drawing_utils.c")
set(inc_dirs "include")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${inc_dirs}")
