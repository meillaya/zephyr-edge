# CMakeLists.txt — Fault injection ztest suite (native_sim).
#
# Tests the fault injection framework with all three fault modes enabled.
# Each test verifies that the corresponding hook behaves correctly when the
# fault mode is enabled and disabled.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_fault_inject)

target_sources(app PRIVATE
  src/main.c
  ../../src/fault_inject.c
)
target_include_directories(app PRIVATE ../../src)
