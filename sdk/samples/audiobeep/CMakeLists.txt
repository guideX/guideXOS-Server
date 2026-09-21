cmake_minimum_required(VERSION 3.16)

project(guideXOSAudioBeepNativeElf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GUIDEXOS_SAMPLE_ARCH "amd64" CACHE STRING "guideXOS app target architecture")
set_property(CACHE GUIDEXOS_SAMPLE_ARCH PROPERTY STRINGS amd64)

if(NOT GUIDEXOS_SAMPLE_ARCH STREQUAL "amd64")
    message(FATAL_ERROR "The AudioBeep sample currently supports amd64 only.")
endif()

get_filename_component(GUIDEXOS_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(GUIDEXOS_REPO_ROOT "${GUIDEXOS_SDK_ROOT}/.." ABSOLUTE)
set(GUIDEXOS_APP_OUTPUT_DIR "${GUIDEXOS_REPO_ROOT}/Apps/AudioBeep")
set(GUIDEXOS_APP_BIN_DIR "${GUIDEXOS_APP_OUTPUT_DIR}/bin/${GUIDEXOS_SAMPLE_ARCH}")

add_executable(audiobeep.elf main.cpp)
target_include_directories(audiobeep.elf PRIVATE
    "${GUIDEXOS_SDK_ROOT}/include"
    "${CMAKE_CURRENT_LIST_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(audiobeep.elf PRIVATE -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector)
    target_link_options(audiobeep.elf PRIVATE -nostdlib -static -no-pie -Wl,-e,gx_main)
endif()

set_target_properties(audiobeep.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GUIDEXOS_APP_BIN_DIR}"
    OUTPUT_NAME "audiobeep"
    SUFFIX ".elf"
)
