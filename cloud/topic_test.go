package main

import "testing"

func TestValidateTopicFilter(t *testing.T) {
	valid := []string{
		"telemetry/+",
		"devices/+/telemetry",
	}
	for _, filter := range valid {
		if err := validateTopicFilter(filter); err != nil {
			t.Errorf("validateTopicFilter(%q) = %v, want nil", filter, err)
		}
	}

	invalid := []string{
		"",
		"telemetry",
		"telemetry/#",
		"telemetry/+/+",
		"telemetry/foo+",
		"telemetry/+suffix",
	}
	for _, filter := range invalid {
		if err := validateTopicFilter(filter); err == nil {
			t.Errorf("validateTopicFilter(%q) = nil, want error", filter)
		}
	}
}

func TestDeviceIDFromTopic(t *testing.T) {
	tests := []struct {
		filter string
		topic  string
		want   string
		ok     bool
	}{
		{"telemetry/+", "telemetry/dev-1", "dev-1", true},
		{"devices/+/telemetry", "devices/dev-2/telemetry", "dev-2", true},
		{"telemetry/+", "telemetry/dev-1/extra", "", false},
		{"telemetry/+", "other/dev-1", "", false},
		{"telemetry/+", "telemetry/", "", false},
	}
	for _, tc := range tests {
		got, ok := deviceIDFromTopic(tc.filter, tc.topic)
		if got != tc.want || ok != tc.ok {
			t.Errorf("deviceIDFromTopic(%q, %q) = (%q, %v), want (%q, %v)",
				tc.filter, tc.topic, got, ok, tc.want, tc.ok)
		}
	}
}
