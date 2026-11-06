# OTA Protocol — `cmd/<device_id>/ota`

This document defines the OTA receive path implemented by
[`firmware/src/ota/ota_update.c`](../firmware/src/ota/ota_update.c) and the
host publisher [`tests/ota-sim`](../tests/ota-sim).

## Transport

| Property | Value |
|----------|-------|
| Topic | `cmd/<device_id>/ota` (`CONFIG_OTA_CMD_TOPIC_FMT`) |
| Direction | cloud/host → device |
| MQTT QoS | 1 |
| Payload | one raw binary frame (not CBOR) |
| Max image | `CONFIG_OTA_MAX_IMAGE_SIZE` (default 262144 bytes) |
| Max data frame | `CONFIG_OTA_CHUNK_SIZE` (default 512 bytes) |

The telemetry MQTT client is the only client: it reads each inbound PUBLISH
payload from the socket, dispatches it to the OTA handler, and acknowledges
QoS1 even for invalid/oversized frames so the broker does not redeliver
forever. The client resubscribes after every CONNACK.

## Frames

| Frame | Bytes | Meaning |
|-------|-------|---------|
| start | `01` + uint32 big-endian image size | Begin a session; exactly 5 bytes. Rejects zero, oversized, or > slot1 size. |
| data | `02` + 1..`CONFIG_OTA_CHUNK_SIZE` image bytes | Append bytes to the OTA slot. Rejected before start or on overflow. |
| finish | `03` | Require `bytes_received == size`, flush, and mark for MCUboot swap. Exactly 1 byte. |
| abort | `04` | Discard session state. Exactly 1 byte. |

Examples:

```
start 4 bytes:          01 00 00 00 04
data "AB":              02 41 42
data "CD":              02 43 44
finish:                 03
abort:                  04
```

## Device state machine

```
IDLE ──start──► RECEIVING ──finish (size match + flush + mark swap)──► COMPLETE
                  │  │
                  │  └─ overflow / malformed frame / flash error ──► ERROR
                  └─ abort / disconnect / reconnect ──────────────► IDLE / ERROR
```

On reconnect or disconnect during `RECEIVING`, the partial image is abandoned
(`ERROR`); a new `start` reinitializes the flash image context.

## Flashing and swap

- `flash_img_init()` + `flash_img_buffered_write()` write to
  `slot1_partition`.
- On `finish`, `boot_request_upgrade(BOOT_UPGRADE_TEST)` is called when
  `CONFIG_BOOTLOADER_MCUBOOT` is enabled; otherwise the image is staged in the
  flash simulator and a log line notes that no real swap occurs.
- `CONFIG_OTA_MAX_IMAGE_SIZE` must fit `slot1_partition`; this is a compile-time
  assert. The nRF52840 DK overlay provides a real 448 KiB slot1, and the same
  assert applies there.
- `CONFIG_IMG_ERASE_PROGRESSIVELY=y` erases slot1 pages as chunks arrive; this
  is required for NOR flash and makes repeated simulator sessions deterministic.
- With `CONFIG_FAULT_INJECT_FLASH_CORRUPT=y`, every byte is inverted before the
  flash write to exercise error handling.

## Host publisher

```bash
# Build an image first, e.g. `just build-hil` (nRF52840 + MCUboot).
just ota-sim build-hil/firmware/zephyr/zephyr.signed.bin nrf52840-001
# or:
cd tests/ota-sim
go run . -broker tls://localhost:8883 -ca ../../infra/tls/ca.crt \
  -username edge-device -password change-me \
  -device native-sim-001 -image /path/image.bin
```

The tool publishes start → chunked data → finish with QoS1 and sends abort on
interrupt/failure. It stops at the publisher; there is no device status topic
yet, so confirm completion on the console (or after reboot for a real MCUboot
swap).

## Real MCUboot path (nRF52840 DK)

The default `native_sim` build still simulates staging because it has no real
bootloader flash offset. The hardware path is a separate sysbuild:

```bash
# Requires Zephyr SDK; see docs/ROADMAP and tests/hil/README.
just build-hil
# -> build-hil/mcuboot/zephyr/zephyr.hex
# -> build-hil/firmware/zephyr/zephyr.signed.hex
```

- `firmware/sysbuild.conf` selects `SB_CONFIG_BOOTLOADER_MCUBOOT=y`.
- `firmware/boards/nrf52840dk_nrf52840.overlay` defines boot/slot0/slot1 and a
  448 KiB slot1; MCUboot uses the default swap-using-move mode.
- The application calls `boot_request_upgrade(BOOT_UPGRADE_TEST)`, and MCUboot
  verifies the signed image before booting/swapping it.
- **Signing key:** sysbuild currently uses MCUboot's default debug RSA key and
  prints a warning. For a real deployment set
  `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` to your private key and keep it out of git.
- The hardware path is cross-build/CI validated here, not physically flashed
  (see residual risks). The DK also needs an IP transport (shield or Thread
  border router) before MQTT/OTA traffic can reach it.

## Security and limitations

- MQTT is TLS 1.2+ with password authentication by default. The committed
  CA/server certificates and `change-me` passwords are **dev-only**; rotate them
  and keep private keys in a secret store before exposing the broker or OTA
  topic to untrusted networks.
- On hardware, MCUboot validates the signed image before swap. On `native_sim`,
  the application still only simulates staging/marking because there is no
  bootloader; do not treat the simulator log as proof of a secure boot.
- A protocol version byte is not included; the topic is versioned implicitly by
  `schema_version`-style deployment. Changing frame semantics is a breaking
  change (pre-1.0).

## Verification

```sh
just test                       # includes the OTA session/reject ztests
(cd tests/ota-sim && go test ./...)
```
