# CMakeLists.txt — OTA update ztest suite (native_sim).
#
# Tests the OTA state machine and image validation logic. The full MQTT flow
# requires a broker and is tested at the integration level; this suite verifies
# the flash write path and MCUboot swap marking in isolation.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ota)

target_sources(app PRIVATE
  src/main.c
  ../../src/ota/ota_update.c
)
target_include_directories(app PRIVATE ../../src/ota)
