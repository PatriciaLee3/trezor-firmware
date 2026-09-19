cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set. Activate ESP-IDF 6.1.0 first.")
endif()

include($ENV{IDF_PATH}/tools/cmake/version.cmake)
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL "6.1.0")
  message(FATAL_ERROR "This port requires ESP-IDF 6.1.0 exactly")
endif()

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(legacy_trezor_one_esp32s3 VERSION 1.14.2)
