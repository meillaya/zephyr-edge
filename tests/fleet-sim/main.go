// Command fleet-sim simulates a fleet of Zephyr edge devices publishing
// CBOR-encoded telemetry to an MQTT broker, per docs/DATA_MODEL.md.
//
// Each simulated device ("fleet-sim-<N>") runs in its own goroutine and
// publishes a synthetic telemetry sample on telemetry/<device_id> (QoS 1) at a
// configurable interval. Sensor values follow a bounded random walk around the
// BME280 known-answer fixture (25.0 °C / 50.0 %RH / 101.325 kPa). A single
// shared MQTT client multiplexes every device, so the simulator comfortably
// drives 50+ (or hundreds of) concurrent devices against a local mosquitto
// broker — mirroring how a fleet gateway aggregates many devices over one
// connection.
//
// The whole simulator lives in this one file (plus _test.go files) so it can be
// launched with `go run main.go -n <N>` — the invocation used by `just demo`
// (see ../../justfile) and documented in ../../docs/DEMO.md.
//
// Usage:
//
//	go run main.go -n 50                     # 50 devices @ 1s to localhost:1883
//	go run main.go -n 100 -interval 500ms    # 100 devices, 2 samples/s each
//	go run main.go -broker tcp://host:1883 -duration 30s
package main

import (
	"context"
	"flag"
	"fmt"
	"log/slog"
	"math/rand/v2"
	"os"
	"os/signal"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/fxamacker/cbor/v2"
)

// ── CBOR wire format (docs/DATA_MODEL.md §1) ────────────────────────

// SchemaVersion is the telemetry schema version this simulator emits
// (docs/DATA_MODEL.md §1.1, key 0). The consumer rejects anything else.
const SchemaVersion = 1

// CBOR integer map keys (docs/DATA_MODEL.md §1.1). These MUST stay in sync with
// cloud/telemetry/telemetry.go.
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

// power_state enum (docs/DATA_MODEL.md §1.2).
const (
	PowerActive uint64 = 0
	PowerIdle   uint64 = 1
	PowerSleep  uint64 = 2
)

// power_counters sub-keys (docs/DATA_MODEL.md §1.1, key 8): cumulative
// per-state durations in milliseconds.
const (
	CounterActiveMs uint64 = 0
	CounterIdleMs   uint64 = 1
	CounterSleepMs  uint64 = 2
)

// encMode is a deterministic CBOR encoder for the telemetry payload:
//   - integer map keys (major type 0), as required by DATA_MODEL §1.1;
//   - RFC 8949 core-deterministic key ordering, so identical readings produce
//     byte-identical payloads (matches the "deterministic encoding" rationale);
//   - ShortestFloat disabled so float64 sensor values are always written as
//     8-byte doubles (DATA_MODEL §1.1 types keys 4–6 as "float64"), preserving
//     full precision instead of shrinking to float16/float32.
var encMode = func() cbor.EncMode {
	em, err := cbor.EncOptions{
		Sort:          cbor.SortCoreDeterministic,
		ShortestFloat: cbor.ShortestFloatNone,
		NaNConvert:    cbor.NaNConvert7e00,
		InfConvert:    cbor.InfConvertFloat16,
		IndefLength:   cbor.IndefLengthForbidden,
	}.EncMode()
	if err != nil {
		// The options above are all valid; EncMode only errors on invalid
		// combinations, so this is unreachable in practice.
		panic("fleet-sim: invalid CBOR enc options: " + err.Error())
	}
	return em
}()

// Reading is a single telemetry sample for one device (DATA_MODEL §1.1).
type Reading struct {
	DeviceID     string
	Seq          uint64
	TimestampMs  uint64
	TemperatureC float64
	HumidityPct  float64
	PressureKpa  float64
	PowerState   uint64
	ActiveMs     uint64
	IdleMs       uint64
	SleepMs      uint64
}

// Encode serializes the reading as a CBOR map with integer keys per
// docs/DATA_MODEL.md §1.1. Required keys 0–6 are always present; optional keys
// 7 (power_state) and 8 (power_counters) are included to exercise the full
// schema the consumer supports.
func (r Reading) Encode() ([]byte, error) {
	m := map[uint64]interface{}{
		KeySchemaVersion: uint64(SchemaVersion),
		KeyDeviceID:      r.DeviceID,
		KeySeq:           r.Seq,
		KeyTimestampMs:   r.TimestampMs,
		KeyTemperatureC:  r.TemperatureC,
		KeyHumidityPct:   r.HumidityPct,
		KeyPressureKpa:   r.PressureKpa,
		KeyPowerState:    r.PowerState,
		KeyPowerCounters: map[uint64]uint64{
			CounterActiveMs: r.ActiveMs,
			CounterIdleMs:   r.IdleMs,
			CounterSleepMs:  r.SleepMs,
		},
	}
	return encMode.Marshal(m)
}

// ── Synthetic device model ──────────────────────────────────────────

// Sensor baselines + bounds. Values random-walk around the BME280 I2C-emulator
// known-answer fixture (25.0 °C / 50.0 %RH / 101.325 kPa) and are clamped well
// inside the DATA_MODEL §1.4 valid ranges (temp [-40,85], hum [0,100],
// press [30,115]) so the fleet normally produces in-range telemetry.
const (
	tempBaseC    = 25.0
	tempStepC    = 0.20
	tempMinC     = 15.0
	tempMaxC     = 35.0
	humBasePct   = 50.0
	humStepPct   = 1.00
	humMinPct    = 30.0
	humMaxPct    = 70.0
	pressBaseKpa = 101.325
	pressStepKpa = 0.05
	pressMinKpa  = 98.0
	pressMaxKpa  = 104.0

	// maxJitterMs bounds the per-sample uptime jitter that mimics scheduler
	// wakeup slack.
	maxJitterMs = 5
)

// Device is a single simulated edge device. It produces a monotonic sequence of
// telemetry readings via a bounded random walk. A Device is owned by exactly
// one goroutine, so it needs no internal synchronization.
type Device struct {
	id  string
	rng *rand.Rand

	seq    uint64
	bootMs uint64 // simulated uptime (k_uptime_get analogue) at last sample

	tempC    float64
	humPct   float64
	pressKpa float64

	powerState uint64
	activeMs   uint64
	idleMs     uint64
	sleepMs    uint64

	phase uint64
}

// NewDevice creates a device with the given id and RNG. Initial sensor values
// are perturbed slightly around the baselines so concurrent devices do not all
// report identical readings.
func NewDevice(id string, rng *rand.Rand) *Device {
	return &Device{
		id:         id,
		rng:        rng,
		tempC:      tempBaseC + jitter(rng, 1.0),
		humPct:     humBasePct + jitter(rng, 3.0),
		pressKpa:   pressBaseKpa + jitter(rng, 0.3),
		powerState: PowerActive,
	}
}

// Next advances the device state by one sampling tick (intervalMs of simulated
// time) and returns the reading to publish. seq and the power counters are
// monotonic; timestamp_ms is the simulated uptime at sample time.
func (d *Device) Next(intervalMs uint64) Reading {
	d.seq++

	// Simulated uptime advances by the interval plus a little jitter.
	d.bootMs += intervalMs + uint64(d.rng.IntN(maxJitterMs+1))

	// Bounded random walk around the baselines.
	d.tempC = clamp(d.tempC+walk(d.rng, tempStepC), tempMinC, tempMaxC)
	d.humPct = clamp(d.humPct+walk(d.rng, humStepPct), humMinPct, humMaxPct)
	d.pressKpa = clamp(d.pressKpa+walk(d.rng, pressStepKpa), pressMinKpa, pressMaxKpa)

	// Cycle power state ACTIVE → IDLE → SLEEP and accumulate per-state time.
	// Over every 10-sample window the device is active for 8 samples, idle for
	// 1, and asleep for 1 — a scaled-down analogue of the firmware's
	// active/idle/sleep duty cycle. Counters stay monotonic and sum to ≈uptime.
	d.phase++
	switch d.phase % 10 {
	case 8:
		d.powerState = PowerIdle
		d.idleMs += intervalMs
	case 9:
		d.powerState = PowerSleep
		d.sleepMs += intervalMs
	default:
		d.powerState = PowerActive
		d.activeMs += intervalMs
	}

	return Reading{
		DeviceID:     d.id,
		Seq:          d.seq,
		TimestampMs:  d.bootMs,
		TemperatureC: d.tempC,
		HumidityPct:  d.humPct,
		PressureKpa:  d.pressKpa,
		PowerState:   d.powerState,
		ActiveMs:     d.activeMs,
		IdleMs:       d.idleMs,
		SleepMs:      d.sleepMs,
	}
}

// walk returns a uniform random delta in [-maxAbs, +maxAbs].
func walk(rng *rand.Rand, maxAbs float64) float64 {
	return (rng.Float64()*2 - 1) * maxAbs
}

// jitter returns a uniform random offset in [-amp, +amp] (used for per-device
// baseline spread).
func jitter(rng *rand.Rand, amp float64) float64 {
	return walk(rng, amp)
}

// clamp restricts v to [lo, hi].
func clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// ── CLI + MQTT fleet orchestration ──────────────────────────────────

// publishTimeout bounds how long a single publish may block before it is
// counted as an error. It prevents goroutine pile-up if the broker stalls.
const publishTimeout = 5 * time.Second

// statsInterval is how often aggregate publish stats are logged.
const statsInterval = 5 * time.Second

// Counters shared across all device goroutines (updated atomically).
var (
	publishedOK   atomic.Uint64
	publishedErrs atomic.Uint64
)

// envOr returns the value of env var key, or fallback if unset/empty.
func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// deviceID returns the identifier for simulated device number n (1-based),
// matching the fleet-sim-<N> convention in docs/DATA_MODEL.md §1.1.
func deviceID(n int) string {
	return fmt.Sprintf("fleet-sim-%d", n)
}

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelInfo}))
	slog.SetDefault(logger)

	// ── Flags (with env fallbacks matching the cloud service's conventions) ──
	numDevices := flag.Int("n", 50, "number of simulated devices (fleet-sim-1..N)")
	broker := flag.String("broker", envOr("MQTT_BROKER", "tcp://localhost:1883"), "MQTT broker URL")
	interval := flag.Duration("interval", 1*time.Second, "publish interval per device")
	duration := flag.Duration("duration", 0, "how long to run (0 = until interrupted)")
	clientID := flag.String("client-id", envOr("CLIENT_ID", "fleet-sim"), "MQTT client ID")
	qos := flag.Int("qos", 1, "MQTT QoS for telemetry publishes")
	seed := flag.Uint64("seed", 0, "RNG seed (0 = random)")
	flag.Parse()

	if *numDevices < 1 {
		slog.Error("device count must be >= 1", "n", *numDevices)
		os.Exit(2)
	}
	if *interval <= 0 {
		slog.Error("interval must be > 0", "interval", *interval)
		os.Exit(2)
	}
	if *qos < 0 || *qos > 2 {
		slog.Error("qos must be 0, 1, or 2", "qos", *qos)
		os.Exit(2)
	}

	slog.Info("fleet-sim starting",
		"devices", *numDevices,
		"broker", *broker,
		"interval", interval.String(),
		"duration", duration.String(),
		"qos", *qos,
	)

	// ── Context: cancelled on SIGINT/SIGTERM, and bounded if -duration is set ──
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	if *duration > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, *duration)
		defer cancel()
	}

	runFleet(ctx, *numDevices, *broker, *clientID, byte(*qos), *interval, *seed)
}

// runFleet connects to the broker, spawns one publishing goroutine per device,
// and blocks until ctx is cancelled, then shuts down cleanly.
func runFleet(ctx context.Context, numDevices int, broker, clientID string, qos byte, interval time.Duration, seed uint64) {
	// ── Shared MQTT client (safe for concurrent Publish) ──
	opts := mqtt.NewClientOptions().
		AddBroker(broker).
		SetClientID(clientID).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(2 * time.Second).
		SetOrderMatters(false). // allow concurrent publishes from many goroutines
		SetOnConnectHandler(func(c mqtt.Client) {
			slog.Info("mqtt connected", "broker", broker)
		}).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			slog.Warn("mqtt connection lost", "error", err)
		})

	client := mqtt.NewClient(opts)
	tok := client.Connect()
	if !tok.WaitTimeout(30 * time.Second) {
		slog.Error("mqtt connect timed out", "broker", broker)
		os.Exit(1)
	}
	if err := tok.Error(); err != nil {
		slog.Error("mqtt connect failed", "broker", broker, "error", err)
		os.Exit(1)
	}

	// ── Per-device RNG streams ──
	// Derive independent, reproducible per-device RNGs from a single seed so a
	// given -seed reproduces the whole fleet's traffic.
	baseSeed := seed
	if baseSeed == 0 {
		baseSeed = rand.Uint64()
	}
	slog.Info("rng seeded", "seed", baseSeed)

	// ── Spawn one goroutine per device ──
	var wg sync.WaitGroup
	intervalMs := uint64(interval / time.Millisecond)
	for i := 1; i <= numDevices; i++ {
		id := deviceID(i)
		devRNG := rand.New(rand.NewPCG(baseSeed+uint64(i)*0x9E3779B97F4A7C15, baseSeed^uint64(i)))
		dev := NewDevice(id, devRNG)
		topic := "telemetry/" + id

		wg.Add(1)
		go func() {
			defer wg.Done()
			publishLoop(ctx, client, dev, topic, qos, interval, intervalMs)
		}()
	}

	// ── Aggregate stats until shutdown ──
	statsDone := make(chan struct{})
	go func() {
		defer close(statsDone)
		ticker := time.NewTicker(statsInterval)
		defer ticker.Stop()
		var lastOK uint64
		last := time.Now()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				ok := publishedOK.Load()
				errs := publishedErrs.Load()
				now := time.Now()
				rate := float64(ok-lastOK) / now.Sub(last).Seconds()
				lastOK, last = ok, now
				slog.Info("fleet stats",
					"published_total", ok,
					"errors_total", errs,
					"rate_per_s", fmt.Sprintf("%.1f", rate),
				)
			}
		}
	}()

	<-ctx.Done()
	slog.Info("shutdown signal received; draining publishers")
	wg.Wait()
	<-statsDone

	slog.Info("fleet-sim stopped",
		"published_total", publishedOK.Load(),
		"errors_total", publishedErrs.Load(),
	)
	client.Disconnect(500)
}

// publishLoop publishes one telemetry sample per tick for a single device until
// ctx is cancelled.
func publishLoop(ctx context.Context, client mqtt.Client, dev *Device, topic string, qos byte, interval time.Duration, intervalMs uint64) {
	// Publish immediately on start, then on each tick.
	publishOne(client, dev, topic, qos, intervalMs)

	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			publishOne(client, dev, topic, qos, intervalMs)
		}
	}
}

// publishOne encodes and publishes a single reading, applying backpressure via
// a bounded wait so a slow broker cannot cause unbounded goroutine pile-up.
func publishOne(client mqtt.Client, dev *Device, topic string, qos byte, intervalMs uint64) {
	reading := dev.Next(intervalMs)
	payload, err := reading.Encode()
	if err != nil {
		publishedErrs.Add(1)
		slog.Warn("cbor encode failed", "device_id", dev.id, "error", err)
		return
	}

	tok := client.Publish(topic, qos, false, payload)
	if !tok.WaitTimeout(publishTimeout) {
		publishedErrs.Add(1)
		slog.Warn("publish timed out", "device_id", dev.id, "topic", topic)
		return
	}
	if err := tok.Error(); err != nil {
		publishedErrs.Add(1)
		slog.Warn("publish failed", "device_id", dev.id, "topic", topic, "error", err)
		return
	}
	publishedOK.Add(1)
}
