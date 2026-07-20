package main

import (
	"bytes"
	"math/rand/v2"
	"testing"

	"github.com/fxamacker/cbor/v2"
)

// decodeMap decodes a payload exactly the way the cloud consumer does
// (cloud/telemetry/telemetry.go): into a map[uint64]interface{}.
func decodeMap(t *testing.T, payload []byte) map[uint64]interface{} {
	t.Helper()
	var m map[uint64]interface{}
	if err := cbor.Unmarshal(payload, &m); err != nil {
		t.Fatalf("cbor decode failed: %v", err)
	}
	return m
}

func asU64(t *testing.T, v interface{}, key string) uint64 {
	t.Helper()
	switch n := v.(type) {
	case uint64:
		return n
	case int:
		return uint64(n)
	case int64:
		return uint64(n)
	default:
		t.Fatalf("key %s: expected uint, got %T (%v)", key, v, v)
		return 0
	}
}

func asF64(t *testing.T, v interface{}, key string) float64 {
	t.Helper()
	f, ok := v.(float64)
	if !ok {
		t.Fatalf("key %s: expected float64, got %T (%v)", key, v, v)
	}
	return f
}

func sampleReading() Reading {
	return Reading{
		DeviceID:     "fleet-sim-7",
		Seq:          42,
		TimestampMs:  210000,
		TemperatureC: 25.08,
		HumidityPct:  49.98,
		PressureKpa:  101.325,
		PowerState:   PowerActive,
		ActiveMs:     5000,
		IdleMs:       120,
		SleepMs:      54880,
	}
}

// TestEncodeRoundTrip verifies every field survives encode → decode with the
// exact integer keys and types from docs/DATA_MODEL.md §1.1.
func TestEncodeRoundTrip(t *testing.T) {
	r := sampleReading()
	payload, err := r.Encode()
	if err != nil {
		t.Fatalf("encode failed: %v", err)
	}

	m := decodeMap(t, payload)

	if got := asU64(t, m[KeySchemaVersion], "schema_version"); got != SchemaVersion {
		t.Errorf("schema_version = %d, want %d", got, SchemaVersion)
	}
	if got, ok := m[KeyDeviceID].(string); !ok || got != r.DeviceID {
		t.Errorf("device_id = %v, want %q", m[KeyDeviceID], r.DeviceID)
	}
	if got := asU64(t, m[KeySeq], "seq"); got != r.Seq {
		t.Errorf("seq = %d, want %d", got, r.Seq)
	}
	if got := asU64(t, m[KeyTimestampMs], "timestamp_ms"); got != r.TimestampMs {
		t.Errorf("timestamp_ms = %d, want %d", got, r.TimestampMs)
	}
	if got := asF64(t, m[KeyTemperatureC], "temperature_c"); got != r.TemperatureC {
		t.Errorf("temperature_c = %v, want %v", got, r.TemperatureC)
	}
	if got := asF64(t, m[KeyHumidityPct], "humidity_pct"); got != r.HumidityPct {
		t.Errorf("humidity_pct = %v, want %v", got, r.HumidityPct)
	}
	if got := asF64(t, m[KeyPressureKpa], "pressure_kpa"); got != r.PressureKpa {
		t.Errorf("pressure_kpa = %v, want %v", got, r.PressureKpa)
	}
	if got := asU64(t, m[KeyPowerState], "power_state"); got != r.PowerState {
		t.Errorf("power_state = %d, want %d", got, r.PowerState)
	}

	// power_counters (key 8) decodes as a nested map[interface{}]interface{}.
	pc, ok := m[KeyPowerCounters].(map[interface{}]interface{})
	if !ok {
		t.Fatalf("power_counters: expected nested map, got %T", m[KeyPowerCounters])
	}
	counters := map[uint64]uint64{}
	for k, v := range pc {
		counters[asU64(t, k, "counter_key")] = asU64(t, v, "counter_val")
	}
	if counters[CounterActiveMs] != r.ActiveMs || counters[CounterIdleMs] != r.IdleMs || counters[CounterSleepMs] != r.SleepMs {
		t.Errorf("power_counters = %v, want active=%d idle=%d sleep=%d",
			counters, r.ActiveMs, r.IdleMs, r.SleepMs)
	}
}

// TestEncodeMatchesDataModelContract re-implements the consumer validation
// rules from docs/DATA_MODEL.md §1.4 and asserts the encoded payload passes
// all of them (the same gates cloud-ingest applies before export).
func TestEncodeMatchesDataModelContract(t *testing.T) {
	payload, err := sampleReading().Encode()
	if err != nil {
		t.Fatalf("encode failed: %v", err)
	}
	m := decodeMap(t, payload)

	// Rule 2: schema_version present and == 1.
	if sv := asU64(t, m[KeySchemaVersion], "schema_version"); sv != 1 {
		t.Fatalf("rule 2: schema_version = %d, want 1", sv)
	}

	// Rule 3: required keys 0–6 present.
	for _, k := range []uint64{
		KeySchemaVersion, KeyDeviceID, KeySeq, KeyTimestampMs,
		KeyTemperatureC, KeyHumidityPct, KeyPressureKpa,
	} {
		if _, ok := m[k]; !ok {
			t.Fatalf("rule 3: required key %d missing", k)
		}
	}

	// Rule 4: sensor values within valid ranges.
	temp := asF64(t, m[KeyTemperatureC], "temperature_c")
	hum := asF64(t, m[KeyHumidityPct], "humidity_pct")
	press := asF64(t, m[KeyPressureKpa], "pressure_kpa")
	if temp < -40 || temp > 85 {
		t.Errorf("rule 4: temperature_c %v out of range [-40,85]", temp)
	}
	if hum < 0 || hum > 100 {
		t.Errorf("rule 4: humidity_pct %v out of range [0,100]", hum)
	}
	if press < 30 || press > 115 {
		t.Errorf("rule 4: pressure_kpa %v out of range [30,115]", press)
	}
}

// TestEncodeDeterministic verifies the encoder is deterministic: identical
// readings produce byte-identical payloads (RFC 8949 core-deterministic order).
func TestEncodeDeterministic(t *testing.T) {
	r := sampleReading()
	a, err := r.Encode()
	if err != nil {
		t.Fatalf("encode a: %v", err)
	}
	b, err := r.Encode()
	if err != nil {
		t.Fatalf("encode b: %v", err)
	}
	if !bytes.Equal(a, b) {
		t.Errorf("encoding is not deterministic:\n a=%x\n b=%x", a, b)
	}
}

// TestFloat64NotShrunk proves sensor values are encoded as full-precision
// float64 (DATA_MODEL §1.1), not shrunk to float16/float32. 101.325 cannot be
// represented exactly in float16, so an exact round-trip rules out shrinking.
func TestFloat64NotShrunk(t *testing.T) {
	r := sampleReading()
	r.PressureKpa = 101.325 // not exactly representable in float16
	r.TemperatureC = 25.08
	payload, err := r.Encode()
	if err != nil {
		t.Fatalf("encode: %v", err)
	}
	m := decodeMap(t, payload)
	if got := asF64(t, m[KeyPressureKpa], "pressure_kpa"); got != 101.325 {
		t.Errorf("pressure_kpa = %v, want exactly 101.325 (float64 preserved)", got)
	}
	if got := asF64(t, m[KeyTemperatureC], "temperature_c"); got != 25.08 {
		t.Errorf("temperature_c = %v, want exactly 25.08 (float64 preserved)", got)
	}
}

// TestDeviceSequence verifies a device produces a monotonic, well-formed stream
// over many samples.
func TestDeviceSequence(t *testing.T) {
	rng := rand.New(rand.NewPCG(1, 2))
	dev := NewDevice("fleet-sim-1", rng)

	const intervalMs = 1000
	var prevSeq, prevTs uint64
	var prevActive, prevIdle, prevSleep uint64
	for i := 0; i < 200; i++ {
		r := dev.Next(intervalMs)

		if r.DeviceID != "fleet-sim-1" {
			t.Fatalf("device_id = %q, want fleet-sim-1", r.DeviceID)
		}
		// seq increments by exactly 1 each sample.
		if r.Seq != prevSeq+1 {
			t.Fatalf("seq = %d, want %d (monotonic +1)", r.Seq, prevSeq+1)
		}
		// timestamp_ms never goes backwards.
		if r.TimestampMs < prevTs {
			t.Fatalf("timestamp_ms went backwards: %d < %d", r.TimestampMs, prevTs)
		}
		// power_state is a valid enum value.
		if r.PowerState > PowerSleep {
			t.Fatalf("power_state = %d, want 0..2", r.PowerState)
		}
		// power counters are monotonic.
		if r.ActiveMs < prevActive || r.IdleMs < prevIdle || r.SleepMs < prevSleep {
			t.Fatalf("power counters decreased: active %d<%d idle %d<%d sleep %d<%d",
				r.ActiveMs, prevActive, r.IdleMs, prevIdle, r.SleepMs, prevSleep)
		}
		// sensor values stay within DATA_MODEL valid ranges.
		if r.TemperatureC < -40 || r.TemperatureC > 85 {
			t.Fatalf("temperature_c %v out of range", r.TemperatureC)
		}
		if r.HumidityPct < 0 || r.HumidityPct > 100 {
			t.Fatalf("humidity_pct %v out of range", r.HumidityPct)
		}
		if r.PressureKpa < 30 || r.PressureKpa > 115 {
			t.Fatalf("pressure_kpa %v out of range", r.PressureKpa)
		}

		prevSeq, prevTs = r.Seq, r.TimestampMs
		prevActive, prevIdle, prevSleep = r.ActiveMs, r.IdleMs, r.SleepMs
	}

	// Over 200 samples the power-state cycle must have visited all three states
	// (8 active / 1 idle / 1 sleep per 10-sample window).
	if prevActive == 0 || prevIdle == 0 || prevSleep == 0 {
		t.Errorf("expected all power counters > 0 after 200 samples; got active=%d idle=%d sleep=%d",
			prevActive, prevIdle, prevSleep)
	}
}

// TestDeviceIDFormat verifies the fleet-sim-<N> naming convention.
func TestDeviceIDFormat(t *testing.T) {
	cases := map[int]string{
		1:  "fleet-sim-1",
		10: "fleet-sim-10",
		50: "fleet-sim-50",
	}
	for n, want := range cases {
		if got := deviceID(n); got != want {
			t.Errorf("deviceID(%d) = %q, want %q", n, got, want)
		}
	}
}
