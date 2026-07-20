package telemetry

import (
	"testing"

	"github.com/fxamacker/cbor/v2"
)

// encode builds a CBOR map with integer keys, mirroring the firmware encoder.
func encode(t *testing.T, m map[uint64]interface{}) []byte {
	t.Helper()
	b, err := cbor.Marshal(m)
	if err != nil {
		t.Fatalf("cbor marshal: %v", err)
	}
	return b
}

// validPayload returns a well-formed telemetry payload.
func validPayload() map[uint64]interface{} {
	return map[uint64]interface{}{
		0: uint64(1),
		1: "native-sim-001",
		2: uint64(42),
		3: uint64(210000),
		4: 25.08,
		5: 49.98,
		6: 101.325,
		7: uint64(0),
		8: map[uint64]interface{}{uint64(0): uint64(5000), uint64(1): uint64(120), uint64(2): uint64(54880)},
	}
}

func TestDecodeValid(t *testing.T) {
	payload := encode(t, validPayload())
	res := Decode(payload)

	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v (err: %v)", res.Validation, res.Err)
	}
	r := res.Reading
	if r.DeviceID != "native-sim-001" {
		t.Errorf("device_id = %q, want %q", r.DeviceID, "native-sim-001")
	}
	if r.Seq != 42 {
		t.Errorf("seq = %d, want 42", r.Seq)
	}
	if r.TimestampMs != 210000 {
		t.Errorf("timestamp_ms = %d, want 210000", r.TimestampMs)
	}
	if r.TemperatureC != 25.08 {
		t.Errorf("temperature_c = %f, want 25.08", r.TemperatureC)
	}
	if r.HumidityPct != 49.98 {
		t.Errorf("humidity_pct = %f, want 49.98", r.HumidityPct)
	}
	if r.PressureKpa != 101.325 {
		t.Errorf("pressure_kpa = %f, want 101.325", r.PressureKpa)
	}
	if r.PowerState == nil || *r.PowerState != 0 {
		t.Errorf("power_state = %v, want 0", r.PowerState)
	}
	if len(r.PowerCounters) != 3 {
		t.Errorf("power_counters len = %d, want 3", len(r.PowerCounters))
	}
	if len(res.RangeViolations) != 0 {
		t.Errorf("expected no range violations, got %v", res.RangeViolations)
	}
}

func TestDecodeMinimalValid(t *testing.T) {
	// Only required keys, no optional power_state/power_counters.
	m := validPayload()
	delete(m, 7)
	delete(m, 8)
	payload := encode(t, m)
	res := Decode(payload)

	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v (err: %v)", res.Validation, res.Err)
	}
	if res.Reading.PowerState != nil {
		t.Errorf("expected nil power_state, got %v", *res.Reading.PowerState)
	}
	if res.Reading.PowerCounters != nil {
		t.Errorf("expected nil power_counters, got %v", res.Reading.PowerCounters)
	}
}

func TestDecodeError(t *testing.T) {
	res := Decode([]byte{0xFF, 0xFE, 0x00})
	if res.Validation != DecodeError {
		t.Fatalf("expected DecodeError, got %v", res.Validation)
	}
}

func TestDecodeEmptyPayload(t *testing.T) {
	res := Decode([]byte{})
	if res.Validation != DecodeError {
		t.Fatalf("expected DecodeError for empty payload, got %v", res.Validation)
	}
}

func TestUnknownSchemaMissing(t *testing.T) {
	m := validPayload()
	delete(m, 0)
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != UnknownSchema {
		t.Fatalf("expected UnknownSchema, got %v (err: %v)", res.Validation, res.Err)
	}
}

func TestUnknownSchemaWrongVersion(t *testing.T) {
	m := validPayload()
	m[0] = uint64(99)
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != UnknownSchema {
		t.Fatalf("expected UnknownSchema, got %v (err: %v)", res.Validation, res.Err)
	}
}

func TestInvalidMissingRequired(t *testing.T) {
	// Remove each required key one at a time and verify Invalid.
	for _, key := range []uint64{1, 2, 3, 4, 5, 6} {
		m := validPayload()
		delete(m, key)
		payload := encode(t, m)
		res := Decode(payload)
		if res.Validation != Invalid {
			t.Errorf("key %d: expected Invalid, got %v (err: %v)", key, res.Validation, res.Err)
		}
	}
}

func TestInvalidDeviceIDNotString(t *testing.T) {
	m := validPayload()
	m[1] = uint64(123)
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Invalid {
		t.Fatalf("expected Invalid for non-string device_id, got %v", res.Validation)
	}
}

func TestRangeViolationTemperature(t *testing.T) {
	m := validPayload()
	m[4] = 200.0 // way above 85
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid (range violations don't reject), got %v", res.Validation)
	}
	if len(res.RangeViolations) != 1 || res.RangeViolations[0].Field != "temperature_c" {
		t.Fatalf("expected 1 temperature_c violation, got %v", res.RangeViolations)
	}
}

func TestRangeViolationHumidity(t *testing.T) {
	m := validPayload()
	m[5] = -5.0
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	found := false
	for _, v := range res.RangeViolations {
		if v.Field == "humidity_pct" {
			found = true
		}
	}
	if !found {
		t.Fatalf("expected humidity_pct violation, got %v", res.RangeViolations)
	}
}

func TestRangeViolationPressure(t *testing.T) {
	m := validPayload()
	m[6] = 10.0 // below 30 kPa
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	found := false
	for _, v := range res.RangeViolations {
		if v.Field == "pressure_kpa" {
			found = true
		}
	}
	if !found {
		t.Fatalf("expected pressure_kpa violation, got %v", res.RangeViolations)
	}
}

func TestMultipleRangeViolations(t *testing.T) {
	m := validPayload()
	m[4] = -50.0
	m[5] = 150.0
	m[6] = 200.0
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	if len(res.RangeViolations) != 3 {
		t.Fatalf("expected 3 violations, got %d: %v", len(res.RangeViolations), res.RangeViolations)
	}
}

func TestBoundaryValuesValid(t *testing.T) {
	// Exactly at boundaries should NOT trigger violations.
	m := validPayload()
	m[4] = -40.0
	m[5] = 0.0
	m[6] = 30.0
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	if len(res.RangeViolations) != 0 {
		t.Fatalf("expected 0 violations at boundaries, got %v", res.RangeViolations)
	}

	m2 := validPayload()
	m2[4] = 85.0
	m2[5] = 100.0
	m2[6] = 115.0
	payload2 := encode(t, m2)
	res2 := Decode(payload2)
	if res2.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res2.Validation)
	}
	if len(res2.RangeViolations) != 0 {
		t.Fatalf("expected 0 violations at upper boundaries, got %v", res2.RangeViolations)
	}
}

func TestFleetSimDeviceID(t *testing.T) {
	m := validPayload()
	m[1] = "fleet-sim-7"
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	if res.Reading.DeviceID != "fleet-sim-7" {
		t.Errorf("device_id = %q, want fleet-sim-7", res.Reading.DeviceID)
	}
}

func TestSeqWrap(t *testing.T) {
	m := validPayload()
	m[2] = uint64(4294967295) // 2^32 - 1
	payload := encode(t, m)
	res := Decode(payload)
	if res.Validation != Valid {
		t.Fatalf("expected Valid, got %v", res.Validation)
	}
	if res.Reading.Seq != 4294967295 {
		t.Errorf("seq = %d, want 4294967295", res.Reading.Seq)
	}
}
