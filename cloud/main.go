// Command cloud-ingest subscribes to device telemetry on an MQTT broker,
// decodes CBOR payloads per docs/DATA_MODEL.md, and exposes Prometheus
// metrics plus a /health endpoint.
package main

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/zephyr-edge-platform/cloud/telemetry"
)

// ── Prometheus metrics ──────────────────────────────────────────────

var (
	msgTotal = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "telemetry_messages_total",
		Help: "Total telemetry messages received per device.",
	}, []string{"device_id"})

	decodeErrTotal = prometheus.NewCounter(prometheus.CounterOpts{
		Name: "telemetry_decode_errors_total",
		Help: "Total CBOR decode failures.",
	})

	unknownSchemaTotal = prometheus.NewCounter(prometheus.CounterOpts{
		Name: "telemetry_unknown_schema_total",
		Help: "Total messages with missing or unsupported schema_version.",
	})

	invalidTotal = prometheus.NewCounter(prometheus.CounterOpts{
		Name: "telemetry_invalid_total",
		Help: "Total messages missing required fields.",
	})

	outOfRangeTotal = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "telemetry_out_of_range_total",
		Help: "Total out-of-range sensor values (still exported).",
	}, []string{"device_id", "field"})

	temperatureGauge = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "telemetry_temperature_celsius",
		Help: "Latest temperature reading in °C.",
	}, []string{"device_id"})

	humidityGauge = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "telemetry_humidity_percent",
		Help: "Latest relative humidity reading in %RH.",
	}, []string{"device_id"})

	pressureGauge = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "telemetry_pressure_kpa",
		Help: "Latest barometric pressure reading in kPa.",
	}, []string{"device_id"})

	seqGauge = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "telemetry_seq",
		Help: "Latest sequence number per device.",
	}, []string{"device_id"})

	topicMismatchTotal = prometheus.NewCounter(prometheus.CounterOpts{
		Name: "telemetry_topic_mismatch_total",
		Help: "Total messages whose topic-derived device ID did not match the payload.",
	})

	lastSeenGauge = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "telemetry_last_seen_timestamp_seconds",
		Help: "Unix timestamp of the last valid telemetry message per device.",
	}, []string{"device_id"})
)

func init() {
	prometheus.MustRegister(
		msgTotal, decodeErrTotal, unknownSchemaTotal, invalidTotal,
		outOfRangeTotal, temperatureGauge, humidityGauge, pressureGauge, seqGauge,
		topicMismatchTotal, lastSeenGauge,
	)
}

// topicFilter is the MQTT subscription filter; handleMessage uses it to derive
// the device ID from the topic. main() sets it from MQTT_TOPIC.
var topicFilter = "telemetry/+"

// ── Configuration ───────────────────────────────────────────────────

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// mqttTLSConfig loads the CA and returns a TLS config for tls:// / ssl://
// brokers. Plaintext brokers return (nil, nil) only when explicitly selected.
func mqttTLSConfig(broker string) (*tls.Config, error) {
	if !strings.HasPrefix(broker, "tls://") && !strings.HasPrefix(broker, "ssl://") {
		return nil, nil
	}

	caFile := envOr("MQTT_TLS_CA", "../infra/tls/ca.crt")
	caPEM, err := os.ReadFile(caFile)
	if err != nil {
		return nil, fmt.Errorf("read MQTT_TLS_CA %q: %w", caFile, err)
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caPEM) {
		return nil, fmt.Errorf("MQTT_TLS_CA %q contains no usable certificates", caFile)
	}

	u, err := url.Parse(broker)
	if err != nil {
		return nil, fmt.Errorf("parse MQTT_BROKER %q: %w", broker, err)
	}
	serverName := envOr("MQTT_TLS_SERVER_NAME", u.Hostname())
	return &tls.Config{
		RootCAs:    pool,
		ServerName: serverName,
		MinVersion: tls.VersionTLS12,
	}, nil
}

// ── Main ────────────────────────────────────────────────────────────

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelInfo}))
	slog.SetDefault(logger)

	mqttBroker := envOr("MQTT_BROKER", "tls://localhost:8883")
	mqttTopic := envOr("MQTT_TOPIC", "telemetry/+")
	httpAddr := envOr("HTTP_ADDR", ":9090")
	clientID := envOr("CLIENT_ID", "cloud-ingest")
	tlsConfig, err := mqttTLSConfig(mqttBroker)
	if err != nil {
		slog.Error("MQTT TLS configuration failed", "error", err)
		os.Exit(1)
	}

	if err := validateTopicFilter(mqttTopic); err != nil {
		slog.Error("invalid MQTT_TOPIC", "topic", mqttTopic, "error", err)
		os.Exit(1)
	}
	topicFilter = mqttTopic

	// ── MQTT subscriber ─────────────────────────────────────────────
	opts := mqtt.NewClientOptions().
		AddBroker(mqttBroker).
		SetClientID(clientID).
		SetUsername(envOr("MQTT_USERNAME", "edge-ingest")).
		SetPassword(envOr("MQTT_PASSWORD", "change-me")).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(5 * time.Second).
		SetOnConnectHandler(func(c mqtt.Client) {
			slog.Info("mqtt connected, subscribing", "broker", mqttBroker, "topic", mqttTopic)
			tok := c.Subscribe(mqttTopic, 1, handleMessage)
			if !tok.WaitTimeout(10 * time.Second) {
				slog.Error("mqtt subscribe timed out", "topic", mqttTopic)
				return
			}
			if err := tok.Error(); err != nil {
				slog.Error("mqtt subscribe failed", "error", err)
			}
		}).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			slog.Warn("mqtt connection lost", "error", err)
		})
	if tlsConfig != nil {
		opts.SetTLSConfig(tlsConfig)
	}

	client := mqtt.NewClient(opts)
	connectTok := client.Connect()
	if !connectTok.WaitTimeout(30 * time.Second) {
		slog.Error("mqtt connect timed out", "broker", mqttBroker)
		os.Exit(1)
	}
	if err := connectTok.Error(); err != nil {
		slog.Error("mqtt connect failed", "error", err)
		os.Exit(1)
	}

	// ── HTTP server (/metrics + /health) ────────────────────────────
	mux := http.NewServeMux()
	mux.Handle("/metrics", promhttp.Handler())
	healthHandler := func(w http.ResponseWriter, r *http.Request) {
		connected := client.IsConnected()
		status := http.StatusOK
		label := "ok"
		if !connected {
			status = http.StatusServiceUnavailable
			label = "degraded"
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(status)
		json.NewEncoder(w).Encode(map[string]interface{}{
			"status":         label,
			"mqtt_connected": connected,
		})
	}
	mux.HandleFunc("/health", healthHandler)
	mux.HandleFunc("/ready", healthHandler)

	srv := &http.Server{Addr: httpAddr, Handler: mux}
	go func() {
		slog.Info("http server listening", "addr", httpAddr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			slog.Error("http server error", "error", err)
			os.Exit(1)
		}
	}()

	// ── Graceful shutdown ───────────────────────────────────────────
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	slog.Info("shutting down")
	client.Disconnect(500)
	srv.Close()
}

// handleMessage is the MQTT message callback. It decodes, validates, and
// exports each telemetry payload.
func handleMessage(_ mqtt.Client, msg mqtt.Message) {
	res := telemetry.Decode(msg.Payload())

	switch res.Validation {
	case telemetry.DecodeError:
		decodeErrTotal.Inc()
		slog.Warn("cbor decode error", "topic", msg.Topic(), "error", res.Err)
		return
	case telemetry.UnknownSchema:
		unknownSchemaTotal.Inc()
		slog.Warn("unknown schema", "topic", msg.Topic(), "error", res.Err)
		return
	case telemetry.Invalid:
		invalidTotal.Inc()
		slog.Warn("invalid payload", "topic", msg.Topic(), "error", res.Err)
		return
	}

	r := res.Reading

	// The topic is the authentication-adjacent identity: a publisher on
	// telemetry/<id> may only claim that <id> in the payload.
	topicID, ok := deviceIDFromTopic(topicFilter, msg.Topic())
	if !ok || topicID != r.DeviceID {
		topicMismatchTotal.Inc()
		slog.Warn("topic/device mismatch",
			"topic", msg.Topic(), "topic_device_id", topicID,
			"payload_device_id", r.DeviceID)
		return
	}

	msgTotal.WithLabelValues(r.DeviceID).Inc()
	lastSeenGauge.WithLabelValues(r.DeviceID).Set(float64(time.Now().Unix()))
	temperatureGauge.WithLabelValues(r.DeviceID).Set(r.TemperatureC)
	humidityGauge.WithLabelValues(r.DeviceID).Set(r.HumidityPct)
	pressureGauge.WithLabelValues(r.DeviceID).Set(r.PressureKpa)
	seqGauge.WithLabelValues(r.DeviceID).Set(float64(r.Seq))

	// Rule 4: range violations are flagged but the reading is still exported.
	for _, v := range res.RangeViolations {
		outOfRangeTotal.WithLabelValues(r.DeviceID, v.Field).Inc()
		slog.Warn("out-of-range value", "device_id", r.DeviceID, "field", v.Field, "value", v.Value)
	}

	slog.Debug("telemetry ingested",
		"device_id", r.DeviceID, "seq", r.Seq,
		"temp", fmt.Sprintf("%.2f", r.TemperatureC),
		"hum", fmt.Sprintf("%.2f", r.HumidityPct),
		"press", fmt.Sprintf("%.2f", r.PressureKpa),
	)
}
