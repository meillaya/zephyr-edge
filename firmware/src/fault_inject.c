/*
 * fault_inject.c — Fault injection framework (Kconfig-gated).
 *
 * Implements the runtime fault injection hooks described in fault_inject.h.
 * Each fault mode is independently gated at compile time (Kconfig) and at
 * runtime (atomic flag). When CONFIG_FAULT_INJECT is disabled, the entire
 * file compiles to empty stubs via the #if guard.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fault_inject.h"

#if defined(CONFIG_FAULT_INJECT)

LOG_MODULE_REGISTER(FAULT_INJECT, CONFIG_LOG_DEFAULT_LEVEL);

/* Runtime enable flags for each fault mode (atomic for thread safety). */
static atomic_t fault_flags[FAULT_MODE_COUNT];

int fault_inject_init(void)
{
	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		atomic_set(&fault_flags[i], 0);
	}
	LOG_INF("fault injection framework initialized");
	return 0;
}

void fault_inject_set(enum fault_mode mode, bool enabled)
{
	if (mode >= FAULT_MODE_COUNT) {
		return;
	}

#if defined(CONFIG_FAULT_INJECT_SENSOR_FAIL)
	if (mode == FAULT_MODE_SENSOR_FAIL) {
		atomic_set(&fault_flags[mode], enabled ? 1 : 0);
		LOG_INF("FAULT_MODE_SENSOR_FAIL %s", enabled ? "enabled" : "disabled");
		return;
	}
#endif

#if defined(CONFIG_FAULT_INJECT_FLASH_CORRUPT)
	if (mode == FAULT_MODE_FLASH_CORRUPT) {
		atomic_set(&fault_flags[mode], enabled ? 1 : 0);
		LOG_INF("FAULT_MODE_FLASH_CORRUPT %s", enabled ? "enabled" : "disabled");
		return;
	}
#endif

#if defined(CONFIG_FAULT_INJECT_DROP_PACKETS)
	if (mode == FAULT_MODE_DROP_PACKETS) {
		atomic_set(&fault_flags[mode], enabled ? 1 : 0);
		LOG_INF("FAULT_MODE_DROP_PACKETS %s", enabled ? "enabled" : "disabled");
		return;
	}
#endif

	/* Mode not compiled in — silently ignore. */
	ARG_UNUSED(enabled);
}

bool fault_inject_is_active(enum fault_mode mode)
{
	if (mode >= FAULT_MODE_COUNT) {
		return false;
	}
	return atomic_get(&fault_flags[mode]) != 0;
}

int fault_inject_sensor_check(void)
{
#if defined(CONFIG_FAULT_INJECT_SENSOR_FAIL)
	if (atomic_get(&fault_flags[FAULT_MODE_SENSOR_FAIL]) != 0) {
		LOG_DBG("fault injected: sensor read failure");
		return -EIO;
	}
#endif
	return 0;
}

uint8_t fault_inject_flash_corrupt_byte(uint8_t byte)
{
#if defined(CONFIG_FAULT_INJECT_FLASH_CORRUPT)
	if (atomic_get(&fault_flags[FAULT_MODE_FLASH_CORRUPT]) != 0) {
		/* Invert all bits to simulate flash corruption. */
		return (uint8_t)(~byte);
	}
#endif
	return byte;
}

bool fault_inject_should_drop_packet(void)
{
#if defined(CONFIG_FAULT_INJECT_DROP_PACKETS)
	if (atomic_get(&fault_flags[FAULT_MODE_DROP_PACKETS]) != 0) {
		LOG_DBG("fault injected: packet dropped");
		return true;
	}
#endif
	return false;
}

void fault_inject_reset(void)
{
	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		atomic_set(&fault_flags[i], 0);
	}
	LOG_DBG("all fault modes reset");
}

#else /* !CONFIG_FAULT_INJECT */

/* Stubs when the framework is disabled: zero overhead. */

int fault_inject_init(void) { return 0; }
void fault_inject_set(enum fault_mode mode, bool enabled)
{
	ARG_UNUSED(mode);
	ARG_UNUSED(enabled);
}
bool fault_inject_is_active(enum fault_mode mode)
{
	ARG_UNUSED(mode);
	return false;
}
int fault_inject_sensor_check(void) { return 0; }
uint8_t fault_inject_flash_corrupt_byte(uint8_t byte) { return byte; }
bool fault_inject_should_drop_packet(void) { return false; }
void fault_inject_reset(void) {}

#endif /* CONFIG_FAULT_INJECT */
