cmake_minimum_required(VERSION 3.16)

project(guideXOSHelloWorldNativeElf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GUIDEXOS_SAMPLE_ARCH "amd64" CACHE STRING "guideXOS app target architecture")
set_property(CACHE GUIDEXOS_SAMPLE_ARCH PROPERTY STRINGS amd64)

if(NOT GUIDEXOS_SAMPLE_ARCH STREQUAL "amd64")
    message(FATAL_ERROR "The HelloWorld sample currently supports amd64 only.")
endif()

get_filename_component(GUIDEXOS_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(GUIDEXOS_REPO_ROOT "${GUIDEXOS_SDK_ROOT}/.." ABSOLUTE)
set(GUIDEXOS_APP_OUTPUT_DIR "${GUIDEXOS_REPO_ROOT}/Apps/HelloWorld")
set(GUIDEXOS_APP_BIN_DIR "${GUIDEXOS_APP_OUTPUT_DIR}/bin/${GUIDEXOS_SAMPLE_ARCH}")

add_executable(helloworld.elf main.cpp)
target_include_directories(helloworld.elf PRIVATE "${GUIDEXOS_SDK_ROOT}/include")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(helloworld.elf PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector)
    target_link_options(helloworld.elf PRIVATE -nostdlib -static -no-pie -Wl,-e,gx_main)
endif()

set_target_properties(helloworld.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GUIDEXOS_APP_BIN_DIR}"
    OUTPUT_NAME "helloworld"
    SUFFIX ".elf"
)

add_custom_command(TARGET helloworld.elf POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GUIDEXOS_APP_OUTPUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_LIST_DIR}/app.json" "${GUIDEXOS_APP_OUTPUT_DIR}/app.json"
    COMMENT "Copying guideXOS app manifest to Apps/HelloWorld"
)
