# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP-IDF 6.x moved driver/i2c_master.h out of the legacy `driver` component.
# The Waveshare components still declare only REQUIRES "driver", and
# managed_components/ is restored on every reconfigure, so make the new I2C
# component a build-wide requirement rather than patching them.
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_i2c APPEND)

project(version1)
