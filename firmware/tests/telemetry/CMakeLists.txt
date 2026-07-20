# CMakeLists.txt — telemetry CBOR encoding ztest suite (native_sim).
#
# Builds only the pure CBOR encoder (telemetry_cbor.c); the MQTT client and
# network stack are intentionally excluded so the encoding can be verified in
# isolation. The zcbor library is provided by the west zcbor module
# (CONFIG_ZCBOR=y).

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_telemetry)

target_sources(app PRIVATE
  src/main.c
  ../../src/telemetry/telemetry_cbor.c
)
target_include_directories(app PRIVATE ../../src/telemetry)
