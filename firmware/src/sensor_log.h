/*
 * sensor_log.h — Ring-buffered, mutex-protected sensor reading storage.
 *
 * Stores fixed-size timestamped BME280 readings in a Zephyr sys_ring_buf,
 * serialized by a k_mutex so the sampler thread and the telemetry publisher
 * thread can operate concurrently. Overflow policy: overwrite oldest record
 * (the oldest reading is dropped to make room for the newest).
 *
 * Record layout matches docs/DATA_MODEL.md §2.
 */
#ifndef ZEPHYR_EDGE_PLATFORM_SENSOR_LOG_H_
#define ZEPHYR_EDGE_PLATFORM_SENSOR_LOG_H_

#include <stdint.h>

/* One timestamped sensor sample (24 bytes on 32-bit native_sim). */
struct sensor_reading {
	int64_t timestamp_ms;   /* k_uptime_get() at sample time        */
	int32_t temperature_mc; /* temperature, milli-degrees Celsius   */
	int32_t humidity_mpct;  /* relative humidity, milli-percent     */
	int32_t pressure_pa;    /* barometric pressure, pascals         */
};

/* Initialize the ring buffer and its protecting mutex. Idempotent. */
int sensor_log_init(void);

/*
 * Store a reading. If the buffer is full, the oldest reading(s) are dropped
 * to make room (overwrite-oldest policy). Returns 0 on success.
 */
int sensor_log_put(const struct sensor_reading *reading);

/*
 * Pop the oldest reading into *reading. Returns 0 on success, -ENOENT if the
 * buffer is empty.
 */
int sensor_log_get(struct sensor_reading *reading);

/* Number of readings currently stored. */
uint32_t sensor_log_count(void);

/* Maximum number of readings the buffer can hold. */
uint32_t sensor_log_capacity(void);

/* Number of readings dropped due to overflow since init/last reset. */
uint32_t sensor_log_dropped(void);

/* Discard all stored readings (does not reset the dropped counter). */
void sensor_log_reset(void);

#endif /* ZEPHYR_EDGE_PLATFORM_SENSOR_LOG_H_ */
