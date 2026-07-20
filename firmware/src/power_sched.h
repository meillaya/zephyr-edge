/*
 * power_sched.h — Low-power sleep/wake scheduler (Zephyr PM subsystem).
 *
 * Drives a periodic wake -> sample -> log -> sleep cycle. Each cycle has three
 * windows whose durations default to Kconfig values (CONFIG_POWER_ACTIVE_MS /
 * CONFIG_POWER_IDLE_MS / CONFIG_POWER_SLEEP_MS) and can be overridden at runtime
 * (used by the ztest suite to shrink cycles to milliseconds).
 *
 * Power-state policy:
 *   - ACTIVE / IDLE windows hold a PM policy lock that prevents suspend, so the
 *     CPU stays awake to sample and log.
 *   - SLEEP window releases the lock so the PM subsystem may enter
 *     suspend-to-idle, then re-acquires it on wake.
 *
 * Cumulative time spent in each state is tracked in struct power_stats for
 * telemetry / power benchmarks (see docs/DATA_MODEL.md key 8).
 */
#ifndef ZEPHYR_EDGE_PLATFORM_POWER_SCHED_H_
#define ZEPHYR_EDGE_PLATFORM_POWER_SCHED_H_

#include <stdbool.h>
#include <stdint.h>

enum power_state {
	POWER_STATE_ACTIVE = 0, /* CPU running, sensor sampling          */
	POWER_STATE_IDLE = 1,   /* runtime-idle / low-power idle window  */
	POWER_STATE_SLEEP = 2,  /* suspend-to-idle sleep window          */
};

struct power_stats {
	uint64_t active_ms; /* cumulative active-window time   */
	uint64_t idle_ms;   /* cumulative idle-window time     */
	uint64_t sleep_ms;  /* cumulative sleep-window time    */
	uint32_t cycles;    /* completed wake/sleep cycles     */
};

/* Callback invoked once per active window (e.g. to sample + log). Optional. */
typedef void (*power_sched_active_fn)(void *user_data);

/*
 * Initialize the scheduler. Registers the active-window callback (may be NULL).
 * Loads default window durations from Kconfig. Idempotent.
 */
int power_sched_init(power_sched_active_fn active_cb, void *user_data);

/* Override window durations (ms). Pass 0 for any window to skip it. */
void power_sched_set_durations(uint32_t active_ms, uint32_t idle_ms,
			       uint32_t sleep_ms);

/*
 * Run exactly n wake/sleep cycles synchronously (blocking). Updates stats and
 * invokes the active callback once per cycle. Returns 0 on success. Used by the
 * application main loop and by the ztest suite.
 */
int power_sched_run_cycles(uint32_t n);

/* Start a background thread that runs cycles until power_sched_stop(). */
int power_sched_start(void);

/* Signal the background thread to stop and wait for it to exit. */
void power_sched_stop(void);

/* Snapshot cumulative power-state counters. */
void power_sched_get_stats(struct power_stats *out);

/* Current power state. */
enum power_state power_sched_state(void);

#endif /* ZEPHYR_EDGE_PLATFORM_POWER_SCHED_H_ */
