package main

import (
	"os"
	"testing"

	"github.com/fxamacker/cbor/v2"
	"github.com/prometheus/client_golang/prometheus/testutil"
)

type testMessage struct {
	topic   string
	payload []byte
}

func (m testMessage) Duplicate() bool   { return false }
func (m testMessage) Qos() byte         { return 1 }
func (m testMessage) Retained() bool    { return false }
func (m testMessage) Topic() string     { return m.topic }
func (m testMessage) MessageID() uint16 { return 1 }
func (m testMessage) Payload() []byte   { return m.payload }
func (m testMessage) Ack()              {}

func setTopicFilter(t *testing.T, filter string) {
	t.Helper()
	old := topicFilter
	topicFilter = filter
	t.Cleanup(func() { topicFilter = old })
}

func goldenPayload(t *testing.T) []byte {
	t.Helper()
	payload, err := os.ReadFile("telemetry/testdata/firmware-v1.cbor")
	if err != nil {
		t.Fatalf("read golden payload: %v", err)
	}
	return payload
}

func validMap() map[uint64]interface{} {
	return map[uint64]interface{}{
		0: uint64(1),
		1: "native-sim-001",
		2: uint64(42),
		3: uint64(210000),
		4: 25.08,
		5: 49.98,
		6: 101.325,
	}
}

func marshal(t *testing.T, m map[uint64]interface{}) []byte {
	t.Helper()
	payload, err := cbor.Marshal(m)
	if err != nil {
		t.Fatalf("cbor marshal: %v", err)
	}
	return payload
}

func TestHandleMessageGoldenPayload(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001"))

	handleMessage(nil, testMessage{
		topic:   "telemetry/native-sim-001",
		payload: goldenPayload(t),
	})

	after := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001"))
	if after != before+1 {
		t.Errorf("telemetry_messages_total = %v, want %v", after, before+1)
	}
	if got := testutil.ToFloat64(lastSeenGauge.WithLabelValues("native-sim-001")); got <= 0 {
		t.Errorf("last_seen = %v, want > 0", got)
	}
}

func TestHandleMessageRejectsTopicMismatch(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(topicMismatchTotal)
	beforeMsg := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001"))

	handleMessage(nil, testMessage{
		topic:   "telemetry/other-device",
		payload: goldenPayload(t),
	})

	if got := testutil.ToFloat64(topicMismatchTotal); got != before+1 {
		t.Errorf("topic_mismatch_total = %v, want %v", got, before+1)
	}
	if got := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001")); got != beforeMsg {
		t.Errorf("mismatched message was counted: %v -> %v", beforeMsg, got)
	}
}

func TestHandleMessageDecodeError(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(decodeErrTotal)

	handleMessage(nil, testMessage{topic: "telemetry/native-sim-001", payload: []byte{0xff}})

	if got := testutil.ToFloat64(decodeErrTotal); got != before+1 {
		t.Errorf("decode_errors_total = %v, want %v", got, before+1)
	}
}

func TestHandleMessageUnknownSchema(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(unknownSchemaTotal)

	handleMessage(nil, testMessage{
		topic:   "telemetry/native-sim-001",
		payload: marshal(t, map[uint64]interface{}{0: uint64(99)}),
	})

	if got := testutil.ToFloat64(unknownSchemaTotal); got != before+1 {
		t.Errorf("unknown_schema_total = %v, want %v", got, before+1)
	}
}

func TestHandleMessageInvalid(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(invalidTotal)

	payload := validMap()
	delete(payload, 4) // temperature_c is required
	handleMessage(nil, testMessage{topic: "telemetry/native-sim-001", payload: marshal(t, payload)})

	if got := testutil.ToFloat64(invalidTotal); got != before+1 {
		t.Errorf("invalid_total = %v, want %v", got, before+1)
	}
}

func TestHandleMessageOutOfRangeStillExported(t *testing.T) {
	setTopicFilter(t, "telemetry/+")
	before := testutil.ToFloat64(outOfRangeTotal.WithLabelValues("native-sim-001", "temperature_c"))
	beforeMsg := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001"))

	payload := validMap()
	payload[4] = 200.0
	handleMessage(nil, testMessage{topic: "telemetry/native-sim-001", payload: marshal(t, payload)})

	if got := testutil.ToFloat64(outOfRangeTotal.WithLabelValues("native-sim-001", "temperature_c")); got != before+1 {
		t.Errorf("out_of_range_total = %v, want %v", got, before+1)
	}
	if got := testutil.ToFloat64(msgTotal.WithLabelValues("native-sim-001")); got != beforeMsg+1 {
		t.Errorf("out-of-range reading was not exported: %v -> %v", beforeMsg, got)
	}
}
