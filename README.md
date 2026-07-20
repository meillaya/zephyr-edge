# zephyr-edge

Zephyr RTOS edge platform: BME280 sensor driver, MQTT/CBOR telemetry, MCUboot OTA, and Go cloud ingestion.

## Quick Start

```sh
devenv shell      # reproducible toolchain (Zephyr, west, cmake, ninja)
just build        # west build -b native_sim firmware
just test         # run all ztest suites on native_sim
just demo         # end-to-end: firmware + cloud + Grafana dashboard
```

## Structure

```
firmware/    Zephyr app + custom BME280 driver module
cloud/       Go MQTT/CBOR ingestion service
infra/       docker-compose observability (Prometheus + Grafana)
tests/       fleet simulator, fault injection, HIL harness
bootloader/  MCUboot OTA image signing config
```

## References

- [Zephyr RTOS docs](https://docs.zephyrproject.org/latest/)
- [BME280 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf)
- [MCUboot docs](https://docs.mcuboot.com/)
- [paho.mqtt.golang](https://github.com/eclipse/paho.mqtt.golang)
- [fxamacker/cbor](https://github.com/fxamacker/cbor)

## License

MIT — see [LICENSE](LICENSE) if present; otherwise MIT applies.
