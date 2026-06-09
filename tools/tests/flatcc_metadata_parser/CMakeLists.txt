cmake_minimum_required(VERSION 3.16)

project(apparams_flatcc_metadata_test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(parse_apparams_metadata_flatcc
    ${CMAKE_CURRENT_LIST_DIR}/parse_apparams_metadata.c
    ${CMAKE_CURRENT_LIST_DIR}/../../third_party/flatcc/src/runtime/verifier.c
)

target_include_directories(parse_apparams_metadata_flatcc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../../..
    ${CMAKE_CURRENT_LIST_DIR}/../../../apParams_c
    ${CMAKE_CURRENT_LIST_DIR}/../../third_party/flatcc/include
)

if(MSVC)
    target_compile_options(parse_apparams_metadata_flatcc PRIVATE /W4)
else()
    target_compile_options(parse_apparams_metadata_flatcc PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(parse_apparams_metadata_flatcc PRIVATE -Wno-newline-eof)
endif()
