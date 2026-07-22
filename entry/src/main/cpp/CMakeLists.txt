# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(AIPhoneDemo)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

add_library(entry SHARED napi_init.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so)

set(LITE_ROOT ${NATIVERENDER_ROOT_PATH}/lite)
set(LITE_VENDOR_ROOT ${NATIVERENDER_ROOT_PATH}/vendor/lite_llm)
set(LITE_VENDOR_HEADER ${LITE_VENDOR_ROOT}/include/lite_llm.h)
set(LITE_VENDOR_ABI_DIR ${LITE_VENDOR_ROOT}/lib/${OHOS_ARCH})
set(LITE_VENDOR_LIBRARY ${LITE_VENDOR_ABI_DIR}/liblite_llm.so)
set(LITE_MOCK_ROOT ${LITE_VENDOR_ROOT}/mock)

# Change only this value to switch implementations:
#   ON  - build and package the repository's mock liblite_llm.so
#   OFF - link and package the real vendor libraries from vendor/lite_llm
set(LITE_USE_MOCK_VENDOR OFF)

add_library(lite_model_native SHARED ${LITE_ROOT}/lite_model_napi.cpp)
target_include_directories(lite_model_native PRIVATE ${LITE_ROOT})
target_compile_features(lite_model_native PRIVATE cxx_std_17)
target_link_libraries(lite_model_native PUBLIC libace_napi.z.so)
target_compile_definitions(lite_model_native PRIVATE LITE_LLM_VENDOR_AVAILABLE=1)

set(LITE_BUILD_MOCK_VENDOR ${LITE_USE_MOCK_VENDOR})
if(NOT LITE_BUILD_MOCK_VENDOR)
    if(EXISTS ${LITE_VENDOR_HEADER} AND EXISTS ${LITE_VENDOR_LIBRARY})
        set(LITE_BUILD_MOCK_VENDOR OFF)
    elseif(OHOS_ARCH STREQUAL "x86_64")
        set(LITE_BUILD_MOCK_VENDOR ON)
        message(STATUS "Real Lite LLM is unavailable for x86_64; using mock for the simulator")
    elseif(NOT EXISTS ${LITE_VENDOR_HEADER})
        message(FATAL_ERROR "Real Lite LLM header not found: ${LITE_VENDOR_HEADER}")
    else()
        message(FATAL_ERROR "Real Lite LLM library not found for ${OHOS_ARCH}: ${LITE_VENDOR_LIBRARY}")
    endif()
endif()

if(LITE_BUILD_MOCK_VENDOR)
    message(STATUS "Lite LLM implementation: mock (${OHOS_ARCH})")
    add_library(lite_llm_mock SHARED ${LITE_MOCK_ROOT}/lite_llm_mock.cpp)
    set_target_properties(lite_llm_mock PROPERTIES OUTPUT_NAME lite_llm)
    target_compile_features(lite_llm_mock PRIVATE cxx_std_17)
    target_include_directories(lite_llm_mock PUBLIC ${LITE_MOCK_ROOT}/include)
    target_link_libraries(lite_llm_mock PUBLIC libhilog_ndk.z.so)

    target_include_directories(lite_model_native PRIVATE ${LITE_MOCK_ROOT}/include)
    target_link_libraries(lite_model_native PRIVATE lite_llm_mock)
else()
    message(STATUS "Lite LLM implementation: real vendor (${OHOS_ARCH})")
    target_include_directories(lite_model_native PRIVATE ${LITE_VENDOR_ROOT}/include)
    add_library(lite_llm_vendor SHARED IMPORTED)
    set_target_properties(lite_llm_vendor PROPERTIES
        IMPORTED_LOCATION ${LITE_VENDOR_LIBRARY}
        IMPORTED_NO_SONAME TRUE)
    target_link_libraries(lite_model_native PRIVATE lite_llm_vendor)

    file(GLOB LITE_VENDOR_SHARED_LIBRARIES ${LITE_VENDOR_ABI_DIR}/*.so)
    foreach(LITE_VENDOR_SO ${LITE_VENDOR_SHARED_LIBRARIES})
        add_custom_command(TARGET lite_model_native POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LITE_VENDOR_SO} $<TARGET_FILE_DIR:lite_model_native>)
    endforeach()
endif()
