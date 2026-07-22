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

add_library(lite_model_native SHARED
    ${LITE_ROOT}/lite_model_napi.cpp
    ${LITE_ROOT}/lite_runtime.cpp)
target_include_directories(lite_model_native PRIVATE ${LITE_ROOT})
target_compile_features(lite_model_native PRIVATE cxx_std_17)
target_link_libraries(lite_model_native PUBLIC libace_napi.z.so)

if(EXISTS ${LITE_VENDOR_HEADER} AND EXISTS ${LITE_VENDOR_LIBRARY})
    message(STATUS "Lite LLM vendor adapter enabled for ${OHOS_ARCH}")
    target_sources(lite_model_native PRIVATE ${LITE_ROOT}/lite_vendor_adapter.cpp)
    target_include_directories(lite_model_native PRIVATE ${LITE_VENDOR_ROOT}/include)
    add_library(lite_llm_vendor SHARED IMPORTED)
    set_target_properties(lite_llm_vendor PROPERTIES IMPORTED_LOCATION ${LITE_VENDOR_LIBRARY})
    target_link_libraries(lite_model_native PRIVATE lite_llm_vendor)

    file(GLOB LITE_VENDOR_SHARED_LIBRARIES ${LITE_VENDOR_ABI_DIR}/*.so)
    foreach(LITE_VENDOR_SO ${LITE_VENDOR_SHARED_LIBRARIES})
        add_custom_command(TARGET lite_model_native POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LITE_VENDOR_SO} $<TARGET_FILE_DIR:lite_model_native>)
    endforeach()
else()
    message(STATUS "Lite LLM vendor inputs absent for ${OHOS_ARCH}; building runtime stub")
    target_sources(lite_model_native PRIVATE ${LITE_ROOT}/lite_vendor_stub.cpp)
endif()
