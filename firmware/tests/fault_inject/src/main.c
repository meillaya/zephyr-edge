/*
 * main.c — ztest suite for the fault injection framework (fault_inject.c).
 *
 * Tests all three fault modes:
 *   - FAULT_MODE_SENSOR_FAIL: sensor reads return -EIO when enabled
 *   - FAULT_MODE_FLASH_CORRUPT: bytes are inverted when enabled
 *   - FAULT_MODE_DROP_PACKETS: packet drop flag returns true when enabled
 *
 * Each test verifies both the enabled and disabled behavior to ensure the
 * runtime toggle works correctly.
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fault_inject.h"

/* Test fixture: reset all fault modes before each test. */
static void *fault_setup(void)
{
	fault_inject_init();
	return NULL;
}

static void fault_before(void *fixture)
{
	ARG_UNUSED(fixture);
	fault_inject_reset();
}

ZTEST_SUITE(fault_inject_tests, NULL, fault_setup, fault_before, NULL, NULL);

ZTEST(fault_inject_tests, test_init_succeeds)
{
	int err = fault_inject_init();

	zassert_ok(err, "fault_inject_init failed: %d", err);
}

ZTEST(fault_inject_tests, test_all_modes_disabled_initially)
{
	zassert_ok(fault_inject_init());

	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		zassert_false(fault_inject_is_active((enum fault_mode)i),
			      "mode %d should be disabled initially", i);
	}
}

ZTEST(fault_inject_tests, test_sensor_fail_disabled)
{
	fault_inject_set(FAULT_MODE_SENSOR_FAIL, false);
	zassert_false(fault_inject_is_active(FAULT_MODE_SENSOR_FAIL));

	int err = fault_inject_sensor_check();
	zassert_ok(err, "sensor_check should return 0 when disabled, got %d", err);
}

ZTEST(fault_inject_tests, test_sensor_fail_enabled)
{
	fault_inject_set(FAULT_MODE_SENSOR_FAIL, true);
	zassert_true(fault_inject_is_active(FAULT_MODE_SENSOR_FAIL));

	int err = fault_inject_sensor_check();
	zassert_equal(err, -EIO, "sensor_check should return -EIO when enabled, got %d",
		      err);
}

ZTEST(fault_inject_tests, test_sensor_fail_toggle)
{
	/* Enable, verify, disable, verify. */
	fault_inject_set(FAULT_MODE_SENSOR_FAIL, true);
	zassert_equal(fault_inject_sensor_check(), -EIO);

	fault_inject_set(FAULT_MODE_SENSOR_FAIL, false);
	zassert_ok(fault_inject_sensor_check());
}

ZTEST(fault_inject_tests, test_flash_corrupt_disabled)
{
	fault_inject_set(FAULT_MODE_FLASH_CORRUPT, false);
	zassert_false(fault_inject_is_active(FAULT_MODE_FLASH_CORRUPT));

	uint8_t original = 0xA5;
	uint8_t result = fault_inject_flash_corrupt_byte(original);
	zassert_equal(result, original, "byte should be unchanged when disabled");
}

ZTEST(fault_inject_tests, test_flash_corrupt_enabled)
{
	fault_inject_set(FAULT_MODE_FLASH_CORRUPT, true);
	zassert_true(fault_inject_is_active(FAULT_MODE_FLASH_CORRUPT));

	uint8_t original = 0xA5;
	uint8_t result = fault_inject_flash_corrupt_byte(original);
	zassert_equal(result, (uint8_t)(~original),
		      "byte should be inverted when enabled: 0x%02x -> 0x%02x",
		      original, result);
}

ZTEST(fault_inject_tests, test_flash_corrupt_various_bytes)
{
	fault_inject_set(FAULT_MODE_FLASH_CORRUPT, true);

	/* Test a range of byte values. */
	for (int i = 0; i < 256; i++) {
		uint8_t original = (uint8_t)i;
		uint8_t result = fault_inject_flash_corrupt_byte(original);
		zassert_equal(result, (uint8_t)(~original),
			      "byte 0x%02x should invert to 0x%02x, got 0x%02x",
			      original, (uint8_t)(~original), result);
	}
}

ZTEST(fault_inject_tests, test_drop_packets_disabled)
{
	fault_inject_set(FAULT_MODE_DROP_PACKETS, false);
	zassert_false(fault_inject_is_active(FAULT_MODE_DROP_PACKETS));

	bool drop = fault_inject_should_drop_packet();
	zassert_false(drop, "should_drop_packet should return false when disabled");
}

ZTEST(fault_inject_tests, test_drop_packets_enabled)
{
	fault_inject_set(FAULT_MODE_DROP_PACKETS, true);
	zassert_true(fault_inject_is_active(FAULT_MODE_DROP_PACKETS));

	bool drop = fault_inject_should_drop_packet();
	zassert_true(drop, "should_drop_packet should return true when enabled");
}

ZTEST(fault_inject_tests, test_drop_packets_toggle)
{
	fault_inject_set(FAULT_MODE_DROP_PACKETS, true);
	zassert_true(fault_inject_should_drop_packet());

	fault_inject_set(FAULT_MODE_DROP_PACKETS, false);
	zassert_false(fault_inject_should_drop_packet());
}

ZTEST(fault_inject_tests, test_independent_modes)
{
	/* Enable only sensor fail, verify others are unaffected. */
	fault_inject_set(FAULT_MODE_SENSOR_FAIL, true);

	zassert_true(fault_inject_is_active(FAULT_MODE_SENSOR_FAIL));
	zassert_false(fault_inject_is_active(FAULT_MODE_FLASH_CORRUPT));
	zassert_false(fault_inject_is_active(FAULT_MODE_DROP_PACKETS));

	/* Verify behavior. */
	zassert_equal(fault_inject_sensor_check(), -EIO);
	zassert_equal(fault_inject_flash_corrupt_byte(0xFF), 0xFF); /* unchanged */
	zassert_false(fault_inject_should_drop_packet());
}

ZTEST(fault_inject_tests, test_reset_clears_all)
{
	/* Enable all modes. */
	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		fault_inject_set((enum fault_mode)i, true);
	}

	/* Verify all active. */
	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		zassert_true(fault_inject_is_active((enum fault_mode)i));
	}

	/* Reset and verify all cleared. */
	fault_inject_reset();
	for (int i = 0; i < FAULT_MODE_COUNT; i++) {
		zassert_false(fault_inject_is_active((enum fault_mode)i),
			      "mode %d should be disabled after reset", i);
	}
}

ZTEST(fault_inject_tests, test_invalid_mode_ignored)
{
	/* Out-of-range mode should be safely ignored. */
	fault_inject_set((enum fault_mode)99, true);
	zassert_false(fault_inject_is_active((enum fault_mode)99));
}
