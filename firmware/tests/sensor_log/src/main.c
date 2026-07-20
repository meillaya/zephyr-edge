/*
 * main.c — ztest suite for the ring-buffered sensor log.
 *
 * Covers: empty/get-on-empty, FIFO put/get ordering, capacity reporting,
 * overwrite-oldest overflow (with dropped accounting), reset, and concurrent
 * producer access (mutex correctness + record integrity).
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "sensor_log.h"

/* Markers used to detect torn / corrupt records in the concurrency test. */
#define HUM_MARKER  0x5A5A
#define PRESS_MARKER 0xA5A5

static void fresh_log(void)
{
	zassert_ok(sensor_log_init());
	sensor_log_reset();
}

ZTEST(sensor_log_tests, test_empty_get_returns_enoent)
{
	struct sensor_reading r;

	fresh_log();
	zassert_equal(sensor_log_count(), 0, "expected empty log");
	zassert_equal(sensor_log_get(&r), -ENOENT, "expected -ENOENT on empty get");
}

ZTEST(sensor_log_tests, test_capacity_is_positive)
{
	fresh_log();
	zassert_true(sensor_log_capacity() > 0, "capacity must be > 0");
	/* 24-byte records must fit at least a handful in the default buffer. */
	zassert_true(sensor_log_capacity() >= 4, "capacity too small: %u",
		     sensor_log_capacity());
}

ZTEST(sensor_log_tests, test_fifo_ordering)
{
	struct sensor_reading in;
	struct sensor_reading out;

	fresh_log();

	for (int i = 1; i <= 3; i++) {
		in.timestamp_ms = i;
		in.temperature_mc = i * 1000;
		in.humidity_mpct = i * 10;
		in.pressure_pa = i * 100;
		zassert_ok(sensor_log_put(&in));
	}

	zassert_equal(sensor_log_count(), 3, "count=%u", sensor_log_count());

	for (int i = 1; i <= 3; i++) {
		zassert_ok(sensor_log_get(&out));
		zassert_equal(out.timestamp_ms, i, "FIFO order broken at %d", i);
		zassert_equal(out.temperature_mc, i * 1000, "temp mismatch");
	}
	zassert_equal(sensor_log_count(), 0, "log should be drained");
}

ZTEST(sensor_log_tests, test_overflow_overwrites_oldest)
{
	struct sensor_reading in;
	struct sensor_reading out;
	uint32_t cap = sensor_log_capacity();
	uint32_t total = cap + 5;

	fresh_log();
	cap = sensor_log_capacity();

	for (uint32_t i = 0; i < total; i++) {
		in.timestamp_ms = i;
		in.temperature_mc = (int32_t)i;
		in.humidity_mpct = 0;
		in.pressure_pa = 0;
		zassert_ok(sensor_log_put(&in));
	}

	zassert_equal(sensor_log_count(), cap, "count=%u cap=%u",
		      sensor_log_count(), cap);
	zassert_equal(sensor_log_dropped(), 5, "dropped=%u", sensor_log_dropped());

	/* The oldest 5 were overwritten: the first surviving record is #5. */
	zassert_ok(sensor_log_get(&out));
	zassert_equal(out.timestamp_ms, 5, "oldest surviving=%lld",
		      out.timestamp_ms);
}

ZTEST(sensor_log_tests, test_reset_clears)
{
	struct sensor_reading in = {.timestamp_ms = 42};
	struct sensor_reading out;

	fresh_log();
	zassert_ok(sensor_log_put(&in));
	zassert_equal(sensor_log_count(), 1, "count before reset");

	sensor_log_reset();
	zassert_equal(sensor_log_count(), 0, "count after reset");
	zassert_equal(sensor_log_get(&out), -ENOENT, "get after reset");
}

/* --- concurrency test --- */

#define THREAD_PUTS 100

struct producer_args {
	int id;
};

static void producer(void *a, void *b, void *c)
{
	struct producer_args *args = a;

	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int i = 0; i < THREAD_PUTS; i++) {
		struct sensor_reading r = {
			.timestamp_ms = k_uptime_get(),
			.temperature_mc = args->id * 1000000 + i,
			.humidity_mpct = HUM_MARKER,
			.pressure_pa = PRESS_MARKER,
		};
		zassert_ok(sensor_log_put(&r));
	}
}

K_THREAD_STACK_DEFINE(producer_stack_a, 2048);
K_THREAD_STACK_DEFINE(producer_stack_b, 2048);

ZTEST(sensor_log_tests, test_concurrent_producers)
{
	static struct k_thread ta;
	static struct k_thread tb;
	static struct producer_args args_a = {.id = 1};
	static struct producer_args args_b = {.id = 2};
	struct sensor_reading out;

	fresh_log();

	k_thread_create(&ta, producer_stack_a, K_THREAD_STACK_SIZEOF(producer_stack_a),
			producer, &args_a, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_create(&tb, producer_stack_b, K_THREAD_STACK_SIZEOF(producer_stack_b),
			producer, &args_b, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_join(&ta, K_FOREVER);
	k_thread_join(&tb, K_FOREVER);

	uint32_t total_puts = 2 * THREAD_PUTS;
	uint32_t count = sensor_log_count();
	uint32_t dropped = sensor_log_dropped();

	/* Every put either landed in the buffer or was dropped as oldest. */
	zassert_equal(count + dropped, total_puts,
		      "accounting: count=%u dropped=%u total=%u", count, dropped,
		      total_puts);
	zassert_true(count <= sensor_log_capacity(), "count exceeds capacity");

	/* Drain and verify no record is torn/corrupt (markers intact). */
	uint32_t drained = 0;

	while (sensor_log_get(&out) == 0) {
		zassert_equal(out.humidity_mpct, HUM_MARKER, "torn humidity marker");
		zassert_equal(out.pressure_pa, PRESS_MARKER, "torn pressure marker");
		drained++;
	}
	zassert_equal(drained, count, "drained=%u count=%u", drained, count);
}

ZTEST_SUITE(sensor_log_tests, NULL, NULL, NULL, NULL, NULL);
