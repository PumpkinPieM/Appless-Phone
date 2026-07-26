cmake_minimum_required(VERSION 3.5.0)
project(lite_proxy)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(lite_proxy SHARED
    proxy_gateway.cpp
    proxy_napi.cpp
    protocol_codec.cpp
)

target_include_directories(lite_proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(lite_proxy PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lite_proxy PUBLIC libace_napi.z.so)

option(LITE_PROXY_BUILD_TESTS "Build protocol codec tests" OFF)
if(LITE_PROXY_BUILD_TESTS)
    add_executable(lite_proxy_protocol_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/protocol_codec_test.cpp
        protocol_codec.cpp
    )
    target_include_directories(lite_proxy_protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
