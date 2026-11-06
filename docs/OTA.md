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
  assert on native_sim. A real board needs an MCUboot partition layout (see
  `docs/ROADMAP.md`).
- `CONFIG_IMG_ERASE_PROGRESSIVELY=y` erases slot1 pages as chunks arrive; this
  is required for NOR flash and makes repeated simulator sessions deterministic.
- With `CONFIG_FAULT_INJECT_FLASH_CORRUPT=y`, every byte is inverted before the
  flash write to exercise error handling.

## Host publisher

```bash
# Build an image first, e.g. `west build -b nrf52840dk/nrf52840 --sysbuild firmware`
just ota-sim image=build/zephyr/zephyr.bin device=nrf52840-001
# or:
cd tests/ota-sim
go run . -broker tcp://localhost:1883 -device native-sim-001 -image /path/image.bin
```

The tool publishes start → chunked data → finish with QoS1 and sends abort on
interrupt/failure. It stops at the publisher; there is no device status topic
yet, so confirm completion on the console (or after reboot for a real MCUboot
swap).

## Security and limitations

- The local dev/CI stack runs MQTT without TLS or authentication. Do not expose
  `1883` or the OTA topic to untrusted networks.
- The application does not verify image signatures itself. On real hardware,
  MCUboot must validate the image; without `CONFIG_BOOTLOADER_MCUBOOT` +
  sysbuild, `native_sim` only simulates staging/marking.
- A protocol version byte is not included; the topic is versioned implicitly by
  `schema_version`-style deployment. Changing frame semantics is a breaking
  change (pre-1.0).

## Verification

```sh
just test                       # includes the OTA session/reject ztests
(cd tests/ota-sim && go test ./...)
```
