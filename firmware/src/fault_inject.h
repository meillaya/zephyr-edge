/*
 * fault_inject.h — Fault injection framework (Kconfig-gated).
 *
 * Provides runtime-controllable fault injection for testing resilience:
 *   - CONFIG_FAULT_INJECT_SENSOR_FAIL: sensor reads return -EIO
 *   - CONFIG_FAULT_INJECT_FLASH_CORRUPT: flash writes are silently corrupted
 *   - CONFIG_FAULT_INJECT_DROP_PACKETS: MQTT publishes are silently dropped
 *
 * When CONFIG_FAULT_INJECT is disabled, all fault_inject_*() calls compile to
 * no-ops (zero overhead). Individual fault modes are further gated by their
 * own Kconfig symbols.
 *
 * Usage:
 *   fault_inject_init();                    // once at startup
 *   fault_inject_set(FAULT_MODE_SENSOR, true);  // enable sensor failures
 *   ... run tests ...
 *   fault_inject_set(FAULT_MODE_SENSOR, false); // disable
 *
 * The ztest suite enables each mode, exercises the affected path, and asserts
 * the expected failure behavior.
 */
#ifndef ZEPHYR_EDGE_PLATFORM_FAULT_INJECT_H_
#define ZEPHYR_EDGE_PLATFORM_FAULT_INJECT_H_

#include <stdbool.h>
#include <stdint.h>

/* Fault injection modes. */
enum fault_mode {
	FAULT_MODE_SENSOR_FAIL = 0,   /* Sensor reads return -EIO          */
	FAULT_MODE_FLASH_CORRUPT,     /* Flash writes are corrupted        */
	FAULT_MODE_DROP_PACKETS,      /* MQTT publishes are dropped        */
	FAULT_MODE_COUNT,             /* Number of fault modes             */
};

/*
 * Initialize the fault injection framework. All modes start disabled.
 * Idempotent. Returns 0 on success.
 */
int fault_inject_init(void);

/*
 * Enable or disable a fault mode at runtime. Only effective when the
 * corresponding Kconfig symbol is enabled; otherwise a no-op.
 */
void fault_inject_set(enum fault_mode mode, bool enabled);

/* Query whether a fault mode is currently active. */
bool fault_inject_is_active(enum fault_mode mode);

/*
 * Sensor fault hook: call before/instead of sensor_sample_fetch().
 * Returns -EIO if FAULT_MODE_SENSOR_FAIL is active, 0 otherwise.
 */
int fault_inject_sensor_check(void);

/*
 * Flash corruption hook: call on each byte before writing to flash.
 * Returns the (possibly corrupted) byte value.
 */
uint8_t fault_inject_flash_corrupt_byte(uint8_t byte);

/*
 * Packet drop hook: call before mqtt_publish().
 * Returns true if the packet should be dropped (caller returns 0 without
 * sending), false if the packet should proceed normally.
 */
bool fault_inject_should_drop_packet(void);

/* Reset all fault modes to disabled (for testing). */
void fault_inject_reset(void);

#endif /* ZEPHYR_EDGE_PLATFORM_FAULT_INJECT_H_ */
