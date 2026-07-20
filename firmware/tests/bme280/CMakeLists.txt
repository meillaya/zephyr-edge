# CMakeLists.txt — bme280 ztest suite (native_sim + I2C emulator).

cmake_minimum_required(VERSION 3.20.0)

# Register the in-repo BME280 driver module (firmware/drivers).
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_bme280)

target_sources(app PRIVATE src/main.c)

# Expose the driver header (bme280_custom.h) for the compensation known-answer
# tests. The module also adds this include dir globally, but be explicit.
target_include_directories(app PRIVATE ../../drivers/sensor/bme280_custom)
