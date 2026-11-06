# CMakeLists.txt — OTA update ztest suite (native_sim).
#
# Drives the full start/data/finish/abort frame protocol and the rejection
# paths against the native_sim flash simulator; no broker is required.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ota)

target_sources(app PRIVATE
  src/main.c
  ../../src/ota/ota_update.c
  ../../src/fault_inject.c
)
target_include_directories(app PRIVATE ../../src/ota ../../src)
