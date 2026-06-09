cmake_minimum_required(VERSION 3.16)

project(apparams_flatbuffers_metadata_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(parse_apparams_metadata_flatbuffers
    ${CMAKE_CURRENT_LIST_DIR}/parse_apparams_metadata_flatbuffers.cpp
)

target_include_directories(parse_apparams_metadata_flatbuffers PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../../..
    ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/flatbuffers/include
)

if(MSVC)
    target_compile_options(parse_apparams_metadata_flatbuffers PRIVATE /W4)
else()
    target_compile_options(parse_apparams_metadata_flatbuffers PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(parse_apparams_metadata_flatbuffers PRIVATE -Wno-newline-eof)
endif()
