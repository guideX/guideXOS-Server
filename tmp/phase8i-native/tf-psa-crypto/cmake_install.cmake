# Install script for directory: D:/dev/guideXOSServerV0.2/third_party/mbedtls/tf-psa-crypto

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto" TYPE FILE FILES
    "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/cmake/TF-PSA-CryptoConfig.cmake"
    "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/cmake/TF-PSA-CryptoConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto/TF-PSA-CryptoTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto/TF-PSA-CryptoTargets.cmake"
         "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/CMakeFiles/Export/09558911d5a5b54b9ef304c36fb1e8b6/TF-PSA-CryptoTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto/TF-PSA-CryptoTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto/TF-PSA-CryptoTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto" TYPE FILE FILES "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/CMakeFiles/Export/09558911d5a5b54b9ef304c36fb1e8b6/TF-PSA-CryptoTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^()$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/TF-PSA-Crypto" TYPE FILE FILES "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/CMakeFiles/Export/09558911d5a5b54b9ef304c36fb1e8b6/TF-PSA-CryptoTargets-noconfig.cmake")
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/framework/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/include/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/drivers/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/extras/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/platform/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/utilities/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/core/cmake_install.cmake")
  include("D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/pkgconfig/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/dev/guideXOSServerV0.2/tmp/phase8i-native/tf-psa-crypto/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
