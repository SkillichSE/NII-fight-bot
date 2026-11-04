# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.13)

set(ENV{BTSTACK_ROOT} ${CMAKE_CURRENT_SOURCE_DIR}/external/btstack)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)

project(bluepad32_arduino_app_template)
