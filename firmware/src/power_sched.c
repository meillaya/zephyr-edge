/*
 * power_sched.c — Low-power sleep/wake scheduler (Zephyr PM subsystem).
 *
 * Implements the wake -> sample -> log -> sleep cycle described in
 * power_sched.h. A PM policy lock keeps the CPU awake during the active/idle
 * windows and is released during the sleep window so the PM subsystem may
 * suspend. The lock is tracked so it is acquired/released exactly once per
 * transition (balanced even when CONFIG_PM is enabled on real hardware).
 *
 * All window durations are runtime-configurable to keep the ztest suite fast and
 * deterministic. power_sched_init() (re)configures the callback and zeroes the
 * counters on every call so the suite can establish a clean slate per test.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_PM)
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#endif

#include "power_sched.h"

LOG_MODULE_REGISTER(POWER_SCHED, CONFIG_LOG_DEFAULT_LEVEL);

static power_sched_active_fn active_cb;
static void *active_user_data;

static power_sched_service_fn service_cb;
static void *service_user_data;
static uint32_t service_interval_ms = CONFIG_POWER_SERVICE_INTERVAL_MS;

static uint32_t active_ms = CONFIG_POWER_ACTIVE_MS;
static uint32_t idle_ms = CONFIG_POWER_IDLE_MS;
static uint32_t sleep_ms = CONFIG_POWER_SLEEP_MS;

static struct power_stats stats;
static atomic_t current_state = POWER_STATE_ACTIVE;
static bool initialized;
static bool awake_lock_held;

/* Background thread bookkeeping. */
static K_THREAD_STACK_DEFINE(sched_stack, 2048);
static struct k_thread sched_thread;
static k_tid_t sched_tid;
static atomic_t stop_requested;

/* --- PM policy helpers (no-ops when CONFIG_PM is disabled) --- */

static void prevent_suspend(void)
{
#if defined(CONFIG_PM)
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
#endif
}

static void allow_suspend(void)
{
#if defined(CONFIG_PM)
	pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
#endif
}

/* Idempotently drive the awake lock so get/put stay balanced. */
static void set_awake(bool awake)
{
	if (awake && !awake_lock_held) {
		prevent_suspend();
		awake_lock_held = true;
	} else if (!awake && awake_lock_held) {
		allow_suspend();
		awake_lock_held = false;
	}
}

/*
 * Sleep for `ms` milliseconds, returning the actual elapsed time. When
 * `service` is true and a service callback is registered, the sleep is split
 * into interval-sized chunks so MQTT input/keepalive and OTA frames are
 * handled during the sleep window without waiting for the next full wake.
 */
static uint64_t sleep_measure(uint32_t ms, bool service)
{
	int64_t start = k_uptime_get();
	uint32_t remaining = ms;

	if (ms == 0) {
		return 0;
	}

	if (!service || service_cb == NULL || service_interval_ms == 0) {
		k_msleep(ms);
	} else {
		while (remaining > 0) {
			uint32_t chunk = MIN(remaining, service_interval_ms);

			k_msleep(chunk);
			remaining -= chunk;
			service_cb(service_user_data);
		}
	}

	return (uint64_t)(k_uptime_get() - start);
}

int power_sched_init(power_sched_active_fn cb, void *user_data)
{
	active_cb = cb;
	active_user_data = user_data;
	service_cb = NULL;
	service_user_data = NULL;
	service_interval_ms = CONFIG_POWER_SERVICE_INTERVAL_MS;
	memset(&stats, 0, sizeof(stats));
	atomic_set(&current_state, POWER_STATE_ACTIVE);
	atomic_set(&stop_requested, 0);
	/* Default awake: hold the suspend lock until a sleep window releases it. */
	set_awake(true);
	initialized = true;
	LOG_INF("power_sched ready: active=%ums idle=%ums sleep=%ums", active_ms,
		idle_ms, sleep_ms);
	return 0;
}

void power_sched_set_durations(uint32_t a_ms, uint32_t i_ms, uint32_t s_ms)
{
	active_ms = a_ms;
	idle_ms = i_ms;
	sleep_ms = s_ms;
	LOG_DBG("durations set: active=%u idle=%u sleep=%u", a_ms, i_ms, s_ms);
}

int power_sched_set_service(power_sched_service_fn cb, void *user_data,
			    uint32_t interval_ms)
{
	if (cb == NULL) {
		return -EINVAL;
	}

	service_cb = cb;
	service_user_data = user_data;
	service_interval_ms = interval_ms != 0 ? interval_ms :
						 CONFIG_POWER_SERVICE_INTERVAL_MS;
	return 0;
}

static void run_one_cycle(void)
{
	/* ACTIVE: stay awake, do the active-window work (sample + log). */
	atomic_set(&current_state, POWER_STATE_ACTIVE);
	set_awake(true);
	if (active_cb != NULL) {
		active_cb(active_user_data);
	}
	stats.active_ms += sleep_measure(active_ms, false);

	/* IDLE: awake but quiescent (lock still held). */
	atomic_set(&current_state, POWER_STATE_IDLE);
	stats.idle_ms += sleep_measure(idle_ms, false);

	/* SLEEP: release the PM lock so the system may suspend-to-idle. */
	atomic_set(&current_state, POWER_STATE_SLEEP);
	set_awake(false);
	stats.sleep_ms += sleep_measure(sleep_ms, true);

	/* Awake again for the next cycle. */
	set_awake(true);
	stats.cycles++;
	LOG_DBG("cycle %u complete (a=%llu i=%llu s=%llu ms)", stats.cycles,
		stats.active_ms, stats.idle_ms, stats.sleep_ms);
}

int power_sched_run_cycles(uint32_t n)
{
	if (!initialized) {
		return -EAGAIN;
	}
	for (uint32_t i = 0; i < n; i++) {
		run_one_cycle();
	}
	return 0;
}

static void sched_thread_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (!atomic_get(&stop_requested)) {
		run_one_cycle();
	}
}

int power_sched_start(void)
{
	if (!initialized) {
		return -EAGAIN;
	}
	if (sched_tid != NULL) {
		return -EALREADY;
	}
	atomic_set(&stop_requested, 0);
	sched_tid = k_thread_create(&sched_thread, sched_stack,
				    K_THREAD_STACK_SIZEOF(sched_stack),
				    sched_thread_entry, NULL, NULL, NULL,
				    K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(sched_tid, "power_sched");
	return 0;
}

void power_sched_stop(void)
{
	if (sched_tid == NULL) {
		return;
	}
	atomic_set(&stop_requested, 1);
	k_thread_join(sched_tid, K_FOREVER);
	sched_tid = NULL;
}

void power_sched_get_stats(struct power_stats *out)
{
	if (out == NULL) {
		return;
	}
	out->active_ms = stats.active_ms;
	out->idle_ms = stats.idle_ms;
	out->sleep_ms = stats.sleep_ms;
	out->cycles = stats.cycles;
}

enum power_state power_sched_state(void)
{
	return (enum power_state)atomic_get(&current_state);
}
