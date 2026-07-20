package main

import (
	"math/rand/v2"
	"testing"

	"github.com/zephyr-edge-platform/cloud/telemetry"
)

// TestCloudDecoderAcceptsFleetSim is the end-to-end contract guard: every
// reading the simulator produces must be decoded and validated as Valid by the
// real cloud ingestion decoder (cloud/telemetry.Decode), with no range
// violations. This proves fleet-sim's wire output is consumable by the actual
// cloud service, not just by a re-implementation of the spec.
func TestCloudDecoderAcceptsFleetSim(t *testing.T) {
	rng := rand.New(rand.NewPCG(42, 7))
	dev := NewDevice("fleet-sim-1", rng)

	const samples = 500
	for i := 0; i < samples; i++ {
		r := dev.Next(1000)
		payload, err := r.Encode()
		if err != nil {
			t.Fatalf("sample %d: encode failed: %v", i, err)
		}

		res := telemetry.Decode(payload)
		if res.Validation != telemetry.Valid {
			t.Fatalf("sample %d: cloud decoder rejected payload: validation=%v err=%v",
				i, res.Validation, res.Err)
		}
		if len(res.RangeViolations) != 0 {
			t.Fatalf("sample %d: unexpected range violations: %+v", i, res.RangeViolations)
		}

		got := res.Reading
		if got.DeviceID != r.DeviceID {
			t.Errorf("sample %d: device_id = %q, want %q", i, got.DeviceID, r.DeviceID)
		}
		if got.Seq != r.Seq {
			t.Errorf("sample %d: seq = %d, want %d", i, got.Seq, r.Seq)
		}
		if got.TemperatureC != r.TemperatureC || got.HumidityPct != r.HumidityPct || got.PressureKpa != r.PressureKpa {
			t.Errorf("sample %d: sensor mismatch got (%v,%v,%v) want (%v,%v,%v)",
				i, got.TemperatureC, got.HumidityPct, got.PressureKpa,
				r.TemperatureC, r.HumidityPct, r.PressureKpa)
		}
		// power_counters must round-trip through the consumer's nested-map decode.
		if got.PowerCounters[CounterActiveMs] != r.ActiveMs ||
			got.PowerCounters[CounterIdleMs] != r.IdleMs ||
			got.PowerCounters[CounterSleepMs] != r.SleepMs {
			t.Errorf("sample %d: power_counters mismatch got %v want active=%d idle=%d sleep=%d",
				i, got.PowerCounters, r.ActiveMs, r.IdleMs, r.SleepMs)
		}
	}
}
