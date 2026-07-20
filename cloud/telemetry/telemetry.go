// Package telemetry decodes and validates CBOR-encoded telemetry payloads
// produced by the Zephyr edge firmware, as specified in docs/DATA_MODEL.md §1.
package telemetry

import (
	"fmt"

	"github.com/fxamacker/cbor/v2"
)

// SchemaVersion is the only telemetry schema version this consumer accepts.
const SchemaVersion = 1

// CBOR integer map keys (docs/DATA_MODEL.md §1.1).
const (
	KeySchemaVersion = 0
	KeyDeviceID      = 1
	KeySeq           = 2
	KeyTimestampMs   = 3
	KeyTemperatureC  = 4
	KeyHumidityPct   = 5
	KeyPressureKpa   = 6
	KeyPowerState    = 7
	KeyPowerCounters = 8
)

// Reading is a validated telemetry message from a device.
type Reading struct {
	SchemaVersion uint64
	DeviceID      string
	Seq           uint64
	TimestampMs   uint64
	TemperatureC  float64
	HumidityPct   float64
	PressureKpa   float64
	PowerState    *uint64
	PowerCounters map[uint64]uint64
}

// ValidationResult classifies the outcome of payload validation.
type ValidationResult int

const (
	// Valid indicates the payload passed all validation rules.
	Valid ValidationResult = iota
	// DecodeError indicates the CBOR payload could not be parsed.
	DecodeError
	// UnknownSchema indicates schema_version is missing or != 1.
	UnknownSchema
	// Invalid indicates one or more required fields are missing.
	Invalid
)

// RangeViolation records a single out-of-range sensor value.
type RangeViolation struct {
	Field string
	Value float64
}

// DecodeResult bundles the outcome of decoding + validation.
type DecodeResult struct {
	Reading         *Reading
	Validation      ValidationResult
	RangeViolations []RangeViolation
	Err             error
}

// Decode parses a raw CBOR telemetry payload and validates it against
// docs/DATA_MODEL.md §1.4 rules. Range violations are reported but do not
// cause the reading to be rejected (rule 4).
func Decode(payload []byte) DecodeResult {
	// Rule 1: decode CBOR.
	var raw map[uint64]interface{}
	if err := cbor.Unmarshal(payload, &raw); err != nil {
		return DecodeResult{Validation: DecodeError, Err: fmt.Errorf("cbor decode: %w", err)}
	}

	// Rule 2: schema_version must be present and == 1.
	svRaw, ok := raw[KeySchemaVersion]
	if !ok {
		return DecodeResult{Validation: UnknownSchema, Err: fmt.Errorf("missing schema_version (key 0)")}
	}
	sv, ok := toUint64(svRaw)
	if !ok || sv != SchemaVersion {
		return DecodeResult{Validation: UnknownSchema, Err: fmt.Errorf("unsupported schema_version: %v", svRaw)}
	}

	// Rule 3: required keys 0–6 must be present.
	requiredKeys := []uint64{KeySchemaVersion, KeyDeviceID, KeySeq, KeyTimestampMs, KeyTemperatureC, KeyHumidityPct, KeyPressureKpa}
	for _, k := range requiredKeys {
		if _, exists := raw[k]; !exists {
			return DecodeResult{Validation: Invalid, Err: fmt.Errorf("missing required key %d", k)}
		}
	}

	deviceID, ok := raw[KeyDeviceID].(string)
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("device_id (key 1) is not a string")}
	}

	seq, ok := toUint64(raw[KeySeq])
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("seq (key 2) is not a uint")}
	}

	tsMs, ok := toUint64(raw[KeyTimestampMs])
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("timestamp_ms (key 3) is not a uint")}
	}

	temp, ok := toFloat64(raw[KeyTemperatureC])
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("temperature_c (key 4) is not a float")}
	}

	hum, ok := toFloat64(raw[KeyHumidityPct])
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("humidity_pct (key 5) is not a float")}
	}

	press, ok := toFloat64(raw[KeyPressureKpa])
	if !ok {
		return DecodeResult{Validation: Invalid, Err: fmt.Errorf("pressure_kpa (key 6) is not a float")}
	}

	r := &Reading{
		SchemaVersion: sv,
		DeviceID:      deviceID,
		Seq:           seq,
		TimestampMs:   tsMs,
		TemperatureC:  temp,
		HumidityPct:   hum,
		PressureKpa:   press,
	}

	// Optional: power_state (key 7).
	if psRaw, exists := raw[KeyPowerState]; exists {
		if ps, ok := toUint64(psRaw); ok {
			r.PowerState = &ps
		}
	}

	// Optional: power_counters (key 8).
	// fxamacker/cbor decodes nested maps as map[interface{}]interface{}.
	if pcRaw, exists := raw[KeyPowerCounters]; exists {
		r.PowerCounters = decodePowerCounters(pcRaw)
	}

	// Rule 4: range checks (flagged but not rejected).
	var violations []RangeViolation
	if temp < -40 || temp > 85 {
		violations = append(violations, RangeViolation{Field: "temperature_c", Value: temp})
	}
	if hum < 0 || hum > 100 {
		violations = append(violations, RangeViolation{Field: "humidity_pct", Value: hum})
	}
	if press < 30 || press > 115 {
		violations = append(violations, RangeViolation{Field: "pressure_kpa", Value: press})
	}

	return DecodeResult{Reading: r, Validation: Valid, RangeViolations: violations}
}

// decodePowerCounters handles the nested CBOR map for power_counters.
// fxamacker/cbor decodes nested maps as map[interface{}]interface{} when
// the outer target is map[uint64]interface{}.
func decodePowerCounters(v interface{}) map[uint64]uint64 {
	switch m := v.(type) {
	case map[interface{}]interface{}:
		out := make(map[uint64]uint64, len(m))
		for k, val := range m {
			ku, ok1 := toUint64(k)
			vu, ok2 := toUint64(val)
			if ok1 && ok2 {
				out[ku] = vu
			}
		}
		return out
	case map[uint64]interface{}:
		out := make(map[uint64]uint64, len(m))
		for k, val := range m {
			if vu, ok := toUint64(val); ok {
				out[k] = vu
			}
		}
		return out
	}
	return nil
}

// toUint64 converts a CBOR-decoded numeric value to uint64.
func toUint64(v interface{}) (uint64, bool) {
	switch n := v.(type) {
	case uint64:
		return n, true
	case uint:
		return uint64(n), true
	case int:
		if n >= 0 {
			return uint64(n), true
		}
	case int64:
		if n >= 0 {
			return uint64(n), true
		}
	case float64:
		if n >= 0 && n == float64(uint64(n)) {
			return uint64(n), true
		}
	}
	return 0, false
}

// toFloat64 converts a CBOR-decoded numeric value to float64.
func toFloat64(v interface{}) (float64, bool) {
	switch n := v.(type) {
	case float64:
		return n, true
	case float32:
		return float64(n), true
	case uint64:
		return float64(n), true
	case int64:
		return float64(n), true
	case int:
		return float64(n), true
	case uint:
		return float64(n), true
	}
	return 0, false
}
