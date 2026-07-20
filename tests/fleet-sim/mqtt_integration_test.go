package main

import (
	"context"
	"errors"
	"io"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/zephyr-edge-platform/cloud/telemetry"
)

// ── Minimal in-process MQTT 3.1.1 broker (test-only, stdlib net) ─────
//
// Speaks just enough of MQTT 3.1.1 to accept the paho client used by runFleet:
// CONNECT→CONNACK, PUBLISH (QoS 0/1)→record + PUBACK, SUBSCRIBE→SUBACK,
// PINGREQ→PINGRESP, DISCONNECT→close. It exists so the publish path can be
// exercised end-to-end (real TCP socket, real paho client, real CBOR payloads)
// without depending on an external broker in CI.

type brokerMsg struct {
	topic   string
	payload []byte
}

type miniBroker struct {
	ln net.Listener

	mu   sync.Mutex
	msgs []brokerMsg
}

func newMiniBroker(t *testing.T) *miniBroker {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	b := &miniBroker{ln: ln}
	go b.acceptLoop()
	return b
}

func (b *miniBroker) url() string { return "tcp://" + b.ln.Addr().String() }

func (b *miniBroker) close() { b.ln.Close() }

func (b *miniBroker) record(topic string, payload []byte) {
	cp := make([]byte, len(payload))
	copy(cp, payload)
	b.mu.Lock()
	b.msgs = append(b.msgs, brokerMsg{topic: topic, payload: cp})
	b.mu.Unlock()
}

func (b *miniBroker) snapshot() []brokerMsg {
	b.mu.Lock()
	defer b.mu.Unlock()
	out := make([]brokerMsg, len(b.msgs))
	copy(out, b.msgs)
	return out
}

func (b *miniBroker) acceptLoop() {
	for {
		conn, err := b.ln.Accept()
		if err != nil {
			return // listener closed
		}
		go b.handleConn(conn)
	}
}

func (b *miniBroker) handleConn(c net.Conn) {
	defer c.Close()
	for {
		ptype, flags, body, err := readPacket(c)
		if err != nil {
			return
		}
		switch ptype {
		case 1: // CONNECT → CONNACK (session present=0, return code=0 accepted)
			if _, err := c.Write([]byte{0x20, 0x02, 0x00, 0x00}); err != nil {
				return
			}
		case 3: // PUBLISH
			b.handlePublish(c, flags, body)
		case 8: // SUBSCRIBE → SUBACK (grant QoS 1)
			if len(body) >= 2 {
				if _, err := c.Write([]byte{0x90, 0x03, body[0], body[1], 0x01}); err != nil {
					return
				}
			}
		case 12: // PINGREQ → PINGRESP
			if _, err := c.Write([]byte{0xd0, 0x00}); err != nil {
				return
			}
		case 14: // DISCONNECT
			return
		}
	}
}

func (b *miniBroker) handlePublish(c net.Conn, flags byte, body []byte) {
	qos := (flags >> 1) & 0x03
	if len(body) < 2 {
		return
	}
	tlen := int(body[0])<<8 | int(body[1])
	if len(body) < 2+tlen {
		return
	}
	topic := string(body[2 : 2+tlen])
	idx := 2 + tlen
	var pid []byte
	if qos > 0 {
		if len(body) < idx+2 {
			return
		}
		pid = body[idx : idx+2]
		idx += 2
	}
	payload := body[idx:]
	b.record(topic, payload)
	if qos == 1 {
		// PUBACK echoes the packet id.
		_, _ = c.Write(append([]byte{0x40, 0x02}, pid...))
	}
}

// readPacket reads one MQTT fixed header + body.
func readPacket(r io.Reader) (ptype, flags byte, body []byte, err error) {
	var hdr [1]byte
	if _, err = io.ReadFull(r, hdr[:]); err != nil {
		return 0, 0, nil, err
	}
	ptype = hdr[0] >> 4
	flags = hdr[0] & 0x0f

	mult := 1
	remLen := 0
	for {
		var b [1]byte
		if _, err = io.ReadFull(r, b[:]); err != nil {
			return 0, 0, nil, err
		}
		remLen += int(b[0]&0x7f) * mult
		if b[0]&0x80 == 0 {
			break
		}
		mult *= 128
		if mult > 128*128*128 {
			return 0, 0, nil, errors.New("mqtt: malformed remaining length")
		}
	}
	body = make([]byte, remLen)
	if _, err = io.ReadFull(r, body); err != nil {
		return 0, 0, nil, err
	}
	return ptype, flags, body, nil
}

// ── The integration test ────────────────────────────────────────────

// TestFleetPublishesOverMQTT drives the real runFleet against the in-process
// broker with 60 concurrent devices and verifies that:
//   - every device published at least one message on telemetry/fleet-sim-<N>;
//   - every payload is decoded as Valid by the real cloud decoder;
//   - per-device sequence numbers arrive monotonically;
//   - no publishes errored.
func TestFleetPublishesOverMQTT(t *testing.T) {
	b := newMiniBroker(t)
	defer b.close()

	const numDevices = 60
	// Reset the package-level publish counters (shared across the binary).
	publishedOK.Store(0)
	publishedErrs.Store(0)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// 200ms interval → each device publishes ~20–25 times in 5s.
	runFleet(ctx, numDevices, b.url(), "fleet-sim-test", 1, 200*time.Millisecond, 1234)

	msgs := b.snapshot()
	if len(msgs) == 0 {
		t.Fatal("broker received no messages")
	}

	seen := map[string]uint64{} // topic → last seq seen
	for _, m := range msgs {
		if !strings.HasPrefix(m.topic, "telemetry/fleet-sim-") {
			t.Errorf("unexpected topic %q", m.topic)
			continue
		}
		res := telemetry.Decode(m.payload)
		if res.Validation != telemetry.Valid {
			t.Fatalf("payload on %s rejected by cloud decoder: validation=%v err=%v",
				m.topic, res.Validation, res.Err)
		}
		if len(res.RangeViolations) != 0 {
			t.Fatalf("payload on %s has range violations: %+v", m.topic, res.RangeViolations)
		}
		// The device_id in the payload must match the topic.
		wantID := strings.TrimPrefix(m.topic, "telemetry/")
		if res.Reading.DeviceID != wantID {
			t.Errorf("topic %s carries device_id %q", m.topic, res.Reading.DeviceID)
		}
		// Per-device seq must be monotonic (QoS 1 + single publisher per topic).
		if last, ok := seen[m.topic]; ok && res.Reading.Seq <= last {
			t.Errorf("non-monotonic seq on %s: %d after %d", m.topic, res.Reading.Seq, last)
		}
		seen[m.topic] = res.Reading.Seq
	}

	if len(seen) != numDevices {
		t.Errorf("expected publishes from all %d devices, got %d distinct topics", numDevices, len(seen))
	}
	if errs := publishedErrs.Load(); errs != 0 {
		t.Errorf("publish errors = %d, want 0", errs)
	}
	if ok := publishedOK.Load(); ok == 0 {
		t.Error("publishedOK counter = 0, want > 0")
	}

	t.Logf("broker received %d messages across %d devices; publishedOK=%d errs=%d",
		len(msgs), len(seen), publishedOK.Load(), publishedErrs.Load())
}
