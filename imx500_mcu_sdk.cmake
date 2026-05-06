
set(IMX500_MCU_SDK_VERSION_U32 0x00000005)

file(GLOB IMX500_MCU_SDK_SRC_FILES 
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
    @ONLY
)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)