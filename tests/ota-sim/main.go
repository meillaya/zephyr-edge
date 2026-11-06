// Command ota-sim publishes a firmware image to a Zephyr edge device using the
// OTA frame protocol in docs/OTA.md. It is the host-side counterpart of
// firmware/src/ota/ota_update.c and is intended for hardware-in-the-loop use:
//
//	go run . -broker tcp://localhost:1883 -device nrf52840-001 -image build/zephyr/zephyr.bin
//
// The device does not currently publish an OTA status topic, so success is
// confirmed from the device console (or by the MCUboot swap after reboot).
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelInfo}))
	slog.SetDefault(logger)

	var (
		broker    = flag.String("broker", envOr("MQTT_BROKER", "tcp://localhost:1883"), "MQTT broker URL")
		device    = flag.String("device", "native-sim-001", "target device ID")
		imagePath = flag.String("image", "", "firmware image file to publish")
		topicFmt  = flag.String("topic", "cmd/%s/ota", "OTA command topic format (one %s)")
		chunkSize = flag.Int("chunk", 512, "data chunk size in bytes")
		delay     = flag.Duration("delay", 10*time.Millisecond, "delay between publishes")
		qos       = flag.Int("qos", 1, "MQTT QoS (0 or 1)")
		clientID  = flag.String("client-id", "ota-sim", "MQTT client ID")
		maxSize   = flag.Int("max-size", 262144, "maximum accepted image size in bytes")
	)
	flag.Parse()

	if *imagePath == "" {
		slog.Error("missing -image")
		os.Exit(2)
	}
	if strings.Count(*topicFmt, "%s") != 1 {
		slog.Error("-topic must contain exactly one %s", "topic", *topicFmt)
		os.Exit(2)
	}
	if *chunkSize < 1 || *chunkSize > 4096 {
		slog.Error("-chunk must be 1..4096", "chunk", *chunkSize)
		os.Exit(2)
	}
	if *qos < 0 || *qos > 1 {
		slog.Error("-qos must be 0 or 1", "qos", *qos)
		os.Exit(2)
	}

	image, err := os.ReadFile(*imagePath)
	if err != nil {
		slog.Error("read image", "path", *imagePath, "error", err)
		os.Exit(1)
	}
	if len(image) == 0 || len(image) > *maxSize {
		slog.Error("image size out of range", "size", len(image), "max", *maxSize)
		os.Exit(1)
	}

	topic := fmt.Sprintf(*topicFmt, *device)

	opts := mqtt.NewClientOptions().
		AddBroker(*broker).
		SetClientID(*clientID).
		SetAutoReconnect(false).
		SetConnectTimeout(15 * time.Second)
	client := mqtt.NewClient(opts)
	tok := client.Connect()
	if !tok.WaitTimeout(20 * time.Second) {
		slog.Error("mqtt connect timed out", "broker", *broker)
		os.Exit(1)
	}
	if err := tok.Error(); err != nil {
		slog.Error("mqtt connect failed", "broker", *broker, "error", err)
		os.Exit(1)
	}
	defer client.Disconnect(250)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	publish := func(frame []byte) error {
		t := client.Publish(topic, byte(*qos), false, frame)
		if !t.WaitTimeout(15 * time.Second) {
			return errors.New("publish timed out")
		}
		return t.Error()
	}

	slog.Info("OTA session starting", "device", *device, "topic", topic, "bytes", len(image), "chunk", *chunkSize)

	if err := publish(startFrame(uint32(len(image)))); err != nil {
		slog.Error("publish start", "error", err)
		os.Exit(1)
	}

	for off := 0; off < len(image); off += *chunkSize {
		select {
		case <-ctx.Done():
			slog.Warn("interrupted; sending abort")
			_ = publish(abortFrame())
			os.Exit(130)
		default:
		}
		end := off + *chunkSize
		if end > len(image) {
			end = len(image)
		}
		if err := publish(dataFrame(image[off:end])); err != nil {
			slog.Error("publish data", "offset", off, "error", err)
			_ = publish(abortFrame())
			os.Exit(1)
		}
		if *delay > 0 {
			time.Sleep(*delay)
		}
	}

	if err := publish(finishFrame()); err != nil {
		slog.Error("publish finish", "error", err)
		os.Exit(1)
	}
	slog.Info("OTA session sent", "device", *device, "bytes", len(image))
}

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}
