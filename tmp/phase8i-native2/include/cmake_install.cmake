# Install script for directory: D:/dev/guideXOSServerV0.2/third_party/mbedtls/include

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/Mbed TLS")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/mingw64/bin/objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/build_info.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/debug.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/error.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/mbedtls_config.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/net_sockets.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/oid.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/pkcs7.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/ssl.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/ssl_cache.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/ssl_ciphersuites.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/ssl_cookie.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/ssl_ticket.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/timing.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/version.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/x509.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/x509_crl.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/x509_crt.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/x509_csr.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/mbedtls/private" TYPE FILE PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ FILES
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/private/config_adjust_ssl.h"
    "D:/dev/guideXOSServerV0.2/third_party/mbedtls/include/mbedtls/private/config_adjust_x509.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/dev/guideXOSServerV0.2/tmp/phase8i-native2/include/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
