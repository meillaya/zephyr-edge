/*
 * main.c — ztest suite for the low-power sleep/wake scheduler.
 *
 * Uses runtime duration overrides to shrink cycles to a few milliseconds so the
 * suite runs fast and deterministically on native_sim. Verifies cycle counting,
 * cumulative power-state accounting, active-window callback invocation, observed
 * state transitions, and the background thread start/stop path.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "power_sched.h"

static volatile uint32_t active_calls;
static volatile uint32_t service_calls;
static volatile enum power_state state_seen_in_cb;
static volatile enum power_state service_state_seen;

static void counting_active_cb(void *user_data)
{
	uint32_t *bonus = user_data;

	active_calls++;
	state_seen_in_cb = power_sched_state();
	if (bonus != NULL) {
		(*bonus)++;
	}
}

ZTEST(power_sched_tests, test_init_zeroes_stats)
{
	struct power_stats st;

	active_calls = 0;
	zassert_ok(power_sched_init(counting_active_cb, NULL));
	power_sched_get_stats(&st);

	zassert_equal(st.cycles, 0, "cycles=%u", st.cycles);
	zassert_equal(st.active_ms, 0, "active_ms=%llu", st.active_ms);
	zassert_equal(st.idle_ms, 0, "idle_ms=%llu", st.idle_ms);
	zassert_equal(st.sleep_ms, 0, "sleep_ms=%llu", st.sleep_ms);
}

ZTEST(power_sched_tests, test_run_cycles_counts_and_accounts)
{
	struct power_stats st;

	active_calls = 0;
	zassert_ok(power_sched_init(counting_active_cb, NULL));
	power_sched_set_durations(10, 5, 15);

	zassert_ok(power_sched_run_cycles(3));

	power_sched_get_stats(&st);
	zassert_equal(st.cycles, 3, "cycles=%u", st.cycles);
	zassert_equal(active_calls, 3, "active_calls=%u", active_calls);

	/* k_msleep guarantees at least the requested time per window. */
	zassert_true(st.active_ms >= 30, "active_ms=%llu < 30", st.active_ms);
	zassert_true(st.idle_ms >= 15, "idle_ms=%llu < 15", st.idle_ms);
	zassert_true(st.sleep_ms >= 45, "sleep_ms=%llu < 45", st.sleep_ms);
}

ZTEST(power_sched_tests, test_active_window_state_is_active)
{
	active_calls = 0;
	state_seen_in_cb = POWER_STATE_SLEEP; /* sentinel */
	zassert_ok(power_sched_init(counting_active_cb, NULL));
	power_sched_set_durations(5, 1, 1);

	zassert_ok(power_sched_run_cycles(1));

	/* The callback runs during the active window. */
	zassert_equal(state_seen_in_cb, POWER_STATE_ACTIVE,
		      "state in callback=%d", (int)state_seen_in_cb);
}

ZTEST(power_sched_tests, test_user_data_passed_to_callback)
{
	uint32_t bonus = 0;

	zassert_ok(power_sched_init(counting_active_cb, &bonus));
	power_sched_set_durations(2, 0, 0);

	zassert_ok(power_sched_run_cycles(4));
	zassert_equal(bonus, 4, "bonus=%u", bonus);
}

ZTEST(power_sched_tests, test_background_thread_runs_and_stops)
{
	struct power_stats st;

	active_calls = 0;
	zassert_ok(power_sched_init(counting_active_cb, NULL));
	power_sched_set_durations(5, 1, 5);

	zassert_ok(power_sched_start());
	/* Second start while running should report already-started. */
	zassert_equal(power_sched_start(), -EALREADY, "expected -EALREADY");

	k_msleep(60); /* allow a few short cycles to complete */
	power_sched_stop();

	power_sched_get_stats(&st);
	zassert_true(st.cycles >= 1, "background cycles=%u", st.cycles);
	zassert_true(active_calls >= 1, "background active_calls=%u", active_calls);
}

static void counting_service_cb(void *user_data)
{
	ARG_UNUSED(user_data);
	service_calls++;
	service_state_seen = power_sched_state();
}

ZTEST(power_sched_tests, test_service_callback_during_sleep)
{
	service_calls = 0;
	service_state_seen = POWER_STATE_ACTIVE;

	zassert_ok(power_sched_init(counting_active_cb, NULL));
	power_sched_set_durations(1, 0, 20);
	zassert_ok(power_sched_set_service(counting_service_cb, NULL, 5));
	zassert_ok(power_sched_run_cycles(1));

	/* 20ms / 5ms interval -> at least 3 callbacks even with scheduler
	 * slack. The callback must run while the state is SLEEP.
	 */
	zassert_true(service_calls >= 3, "service_calls=%u", service_calls);
	zassert_equal(service_state_seen, POWER_STATE_SLEEP,
		      "service state=%d", (int)service_state_seen);
}

ZTEST(power_sched_tests, test_service_requires_callback)
{
	zassert_ok(power_sched_init(counting_active_cb, NULL));
	zassert_equal(power_sched_set_service(NULL, NULL, 5), -EINVAL);
}

ZTEST_SUITE(power_sched_tests, NULL, NULL, NULL, NULL, NULL);
