/*
 * sensor_log.c — Ring-buffered, mutex-protected sensor reading storage.
 *
 * Built on the Zephyr sys_ring_buf byte ring (CONFIG_RING_BUFFER). Readings are
 * written/read as whole fixed-size records. A k_mutex serializes all access so
 * a producer (sampler) and consumer (telemetry publisher) can run concurrently.
 *
 * Overflow policy is overwrite-oldest: when there is not enough free space for
 * a new record, the oldest record(s) are discarded until the write fits. The
 * number of discarded records is tracked in a dropped counter for telemetry /
 * diagnostics.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "sensor_log.h"

LOG_MODULE_REGISTER(SENSOR_LOG, CONFIG_LOG_DEFAULT_LEVEL);

/* Byte ring sized by Kconfig (default 4096 bytes ~ 170 records). */
RING_BUF_DECLARE(sensor_log_rb, CONFIG_SENSOR_LOG_BUF_SIZE);

static struct k_mutex sensor_log_mutex;
static uint32_t dropped_count;
static bool initialized;

#define RECORD_SIZE ((uint32_t)sizeof(struct sensor_reading))

int sensor_log_init(void)
{
	if (initialized) {
		/* Keep the "fresh init" contract for tests and supervisors: a
		 * repeated init empties the log and clears diagnostics.
		 */
		k_mutex_lock(&sensor_log_mutex, K_FOREVER);
		ring_buf_reset(&sensor_log_rb);
		dropped_count = 0;
		k_mutex_unlock(&sensor_log_mutex);
		return 0;
	}
	k_mutex_init(&sensor_log_mutex);
	ring_buf_reset(&sensor_log_rb);
	dropped_count = 0;
	initialized = true;
	LOG_INF("sensor_log ready: capacity=%u records (%u bytes)",
		ring_buf_capacity_get(&sensor_log_rb) / RECORD_SIZE,
		CONFIG_SENSOR_LOG_BUF_SIZE);
	return 0;
}

int sensor_log_put(const struct sensor_reading *reading)
{
	if (reading == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EAGAIN;
	}

	k_mutex_lock(&sensor_log_mutex, K_FOREVER);

	/* Overwrite-oldest: drop oldest records until the new one fits. */
	while (ring_buf_space_get(&sensor_log_rb) < RECORD_SIZE) {
		uint8_t discard[RECORD_SIZE];
		uint32_t n = ring_buf_get(&sensor_log_rb, discard, RECORD_SIZE);

		if (n == 0) {
			/* Should not happen: space < record but nothing to read. */
			break;
		}
		dropped_count++;
	}

	uint32_t written = ring_buf_put(&sensor_log_rb, (const uint8_t *)reading,
					RECORD_SIZE);

	k_mutex_unlock(&sensor_log_mutex);

	if (written != RECORD_SIZE) {
		LOG_ERR("ring_buf_put short write: %u/%u", written, RECORD_SIZE);
		return -ENOMEM;
	}
	return 0;
}

int sensor_log_get(struct sensor_reading *reading)
{
	if (reading == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EAGAIN;
	}

	k_mutex_lock(&sensor_log_mutex, K_FOREVER);
	uint32_t n = ring_buf_get(&sensor_log_rb, (uint8_t *)reading, RECORD_SIZE);
	k_mutex_unlock(&sensor_log_mutex);

	if (n != RECORD_SIZE) {
		return -ENOENT; /* empty (or partial, which cannot occur here) */
	}
	return 0;
}

uint32_t sensor_log_count(void)
{
	k_mutex_lock(&sensor_log_mutex, K_FOREVER);
	uint32_t used = ring_buf_size_get(&sensor_log_rb);
	k_mutex_unlock(&sensor_log_mutex);
	return used / RECORD_SIZE;
}

uint32_t sensor_log_capacity(void)
{
	return ring_buf_capacity_get(&sensor_log_rb) / RECORD_SIZE;
}

uint32_t sensor_log_dropped(void)
{
	k_mutex_lock(&sensor_log_mutex, K_FOREVER);
	uint32_t d = dropped_count;
	k_mutex_unlock(&sensor_log_mutex);
	return d;
}

void sensor_log_reset(void)
{
	k_mutex_lock(&sensor_log_mutex, K_FOREVER);
	ring_buf_reset(&sensor_log_rb);
	k_mutex_unlock(&sensor_log_mutex);
}
