# CMakeLists.txt — BME280 custom driver Zephyr module.
#
# Added to the build only when CONFIG_BME280_CUSTOM is enabled. The emulator
# source is compiled only when CONFIG_BME280_CUSTOM_EMUL is enabled (native_sim).

if(CONFIG_BME280_CUSTOM)
  zephyr_include_directories(sensor/bme280_custom)
  zephyr_library()
  zephyr_library_sources(sensor/bme280_custom/bme280_custom.c)

  if(CONFIG_BME280_CUSTOM_EMUL)
    zephyr_library_sources(sensor/bme280_custom/bme280_emul.c)
  endif()
endif()
