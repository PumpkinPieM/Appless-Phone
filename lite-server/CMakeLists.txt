cmake_minimum_required(VERSION 3.14)
project(lite_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITE_USE_MOCK_VENDOR "Build the bundled mock liblite_llm.so" ON)
option(LITE_SERVER_BUILD_TESTS "Build lite-server unit tests" ON)

set(LITE_VENDOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lite_llm)
set(LITE_MOCK_ROOT ${LITE_VENDOR_ROOT}/mock)

if(LITE_USE_MOCK_VENDOR)
    include(FetchContent)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    FetchContent_Declare(mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2
        URL_HASH SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(mbedtls)

    add_library(lite_llm_backend SHARED
        ${LITE_MOCK_ROOT}/lite_llm_mock.cpp
    )
    set_target_properties(lite_llm_backend PROPERTIES OUTPUT_NAME lite_llm)
    target_include_directories(lite_llm_backend PUBLIC ${LITE_MOCK_ROOT}/include)
    target_link_libraries(lite_llm_backend PUBLIC libhilog_ndk.z.so mbedtls)
else()
    if(NOT DEFINED OHOS_ARCH)
        message(FATAL_ERROR "OHOS_ARCH is required when selecting the real vendor")
    endif()
    set(LITE_VENDOR_HEADER ${LITE_VENDOR_ROOT}/include/lite_llm.h)
    set(LITE_VENDOR_LIBRARY ${LITE_VENDOR_ROOT}/lib/${OHOS_ARCH}/liblite_llm.so)
    if(NOT EXISTS ${LITE_VENDOR_HEADER})
        message(FATAL_ERROR "Real vendor header not found: ${LITE_VENDOR_HEADER}")
    endif()
    if(NOT EXISTS ${LITE_VENDOR_LIBRARY})
        message(FATAL_ERROR "Real vendor library not found: ${LITE_VENDOR_LIBRARY}")
    endif()
    add_library(lite_llm_backend SHARED IMPORTED GLOBAL)
    set_target_properties(lite_llm_backend PROPERTIES
        IMPORTED_LOCATION ${LITE_VENDOR_LIBRARY}
        IMPORTED_NO_SONAME TRUE
        INTERFACE_INCLUDE_DIRECTORIES ${LITE_VENDOR_ROOT}/include
    )
endif()

add_library(lite_server_core STATIC
    src/json_value.cpp
    src/model.cpp
    src/openai_chat.cpp
    src/protocol_codec.cpp
    src/reverse_client.cpp
)
target_include_directories(lite_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lite_server_core PUBLIC lite_llm_backend)
target_compile_options(lite_server_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lite-server src/main.cpp)
target_link_libraries(lite-server PRIVATE lite_server_core)
target_compile_options(lite-server PRIVATE -Wall -Wextra -Wpedantic)

if(LITE_SERVER_BUILD_TESTS)
    add_executable(lite-server-tests tests/lite_server_tests.cpp)
    target_link_libraries(lite-server-tests PRIVATE lite_server_core)
    target_compile_options(lite-server-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS lite-server RUNTIME DESTINATION .)
if(LITE_USE_MOCK_VENDOR)
    install(TARGETS lite_llm_backend LIBRARY DESTINATION .)
endif()
