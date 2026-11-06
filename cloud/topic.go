package main

import (
	"fmt"
	"strings"
)

// validateTopicFilter accepts the narrow MQTT filter shape used by the
// ingestion service: slash-separated levels with exactly one "+" level and no
// "#". The "+" level identifies the device ID, which is compared with the
// payload's device_id field so one device cannot publish under another's topic.
func validateTopicFilter(filter string) error {
	if filter == "" {
		return fmt.Errorf("empty topic filter")
	}

	plus := 0
	for _, level := range strings.Split(filter, "/") {
		switch {
		case level == "#":
			return fmt.Errorf("multi-level wildcard '#' is not supported: %q", filter)
		case level == "+":
			plus++
		case strings.Contains(level, "+"):
			return fmt.Errorf("'+' must occupy an entire topic level: %q", filter)
		}
	}
	if plus != 1 {
		return fmt.Errorf("topic filter must contain exactly one '+': %q", filter)
	}
	return nil
}

// deviceIDFromTopic returns the device ID captured by the filter's "+" level.
// It mirrors MQTT single-level wildcard matching: both topics must have the
// same number of levels, and non-wildcard levels must match exactly.
func deviceIDFromTopic(filter, topic string) (string, bool) {
	filterLevels := strings.Split(filter, "/")
	topicLevels := strings.Split(topic, "/")
	if len(filterLevels) != len(topicLevels) {
		return "", false
	}

	deviceID := ""
	for i := range filterLevels {
		switch filterLevels[i] {
		case "+":
			if topicLevels[i] == "" {
				return "", false
			}
			deviceID = topicLevels[i]
		case "#":
			return "", false
		default:
			if filterLevels[i] != topicLevels[i] {
				return "", false
			}
		}
	}
	return deviceID, deviceID != ""
}
