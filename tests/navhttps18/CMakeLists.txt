cmake_minimum_required(VERSION 3.20)
project(navhttps18_mbedtls_verifier C)

if(NOT DEFINED GUIDEXOS_REPO_ROOT OR NOT DEFINED MBEDTLS_ROOT)
    message(FATAL_ERROR "Pass -DGUIDEXOS_REPO_ROOT and -DMBEDTLS_ROOT.")
endif()

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_CONFIG_FILE "${GUIDEXOS_REPO_ROOT}/guidexos/mbedtls_config.h" CACHE FILEPATH "" FORCE)
set(TF_PSA_CRYPTO_CONFIG_FILE "${GUIDEXOS_REPO_ROOT}/guidexos/crypto_config.h" CACHE FILEPATH "" FORCE)

add_subdirectory("${MBEDTLS_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/mbedtls")

add_executable(navhttps18_mbedtls_verifier navhttps18_mbedtls_verifier.c)
target_link_libraries(navhttps18_mbedtls_verifier PRIVATE mbedtls)
target_compile_features(navhttps18_mbedtls_verifier PRIVATE c_std_99)
