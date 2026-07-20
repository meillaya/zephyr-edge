# tests/fleet-sim — device fleet simulator

A Go traffic generator that simulates a fleet of Zephyr edge devices publishing
**CBOR-encoded telemetry** to an MQTT broker, exactly matching the wire contract
in [`docs/DATA_MODEL.md`](../../docs/DATA_MODEL.md). It exists to load-test the
cloud ingestion service and light up the Grafana dashboard with realistic
multi-device traffic **without** needing real hardware or the native_sim
firmware.

## What it does

- Spawns one goroutine per simulated device (`-n`, default **50**).
- Each device `fleet-sim-<N>` publishes on `telemetry/fleet-sim-<N>` (QoS 1) at
  a configurable interval.
- Sensor values follow a bounded random walk around the BME280 known-answer
  fixture (25.0 °C / 50.0 %RH / 101.325 kPa), so readings are realistic and stay
  inside the DATA_MODEL §1.4 valid ranges.
- Emits the full schema: required keys `0–6`, plus optional `power_state` (key
  `7`) and cumulative `power_counters` (key `8`).
- A **single shared MQTT client** multiplexes every device (mirroring a fleet
  gateway), so it comfortably drives 50–100+ concurrent devices over one
  connection.
- Deterministic output: RFC 8949 core-deterministic CBOR key order, and a
  reproducible fleet-wide RNG stream via `-seed`.
- Graceful shutdown on `SIGINT`/`SIGTERM` (or after `-duration`), with periodic
  aggregate publish-rate stats.

## Quick start

Start the local broker + cloud stack first (see [`infra/`](../../infra/)):

```bash
cd infra && docker compose up -d mosquitto cloud-ingest
```

Then run the simulator:

```bash
cd tests/fleet-sim

go run . -n 50                      # 50 devices @ 1s → tcp://localhost:1883
go run . -n 100 -interval 500ms     # 100 devices, 2 samples/s each
go run . -duration 30s              # auto-stop after 30s
go run . -broker tcp://host:1883    # point at a different broker
```

Watch it land:

- Cloud metrics: `curl -s localhost:9090/metrics | grep telemetry_messages_total`
- Grafana dashboard: http://localhost:3000 (admin/admin)

## Flags

| Flag          | Env            | Default                  | Description                              |
|---------------|----------------|--------------------------|------------------------------------------|
| `-n`          | —              | `50`                     | number of simulated devices              |
| `-broker`     | `MQTT_BROKER`  | `tcp://localhost:1883`   | MQTT broker URL                          |
| `-interval`   | —              | `1s`                     | publish interval per device              |
| `-duration`   | —              | `0` (until interrupted)  | how long to run                          |
| `-client-id`  | `CLIENT_ID`    | `fleet-sim`              | MQTT client ID                           |
| `-qos`        | —              | `1`                      | MQTT QoS (DATA_MODEL uses 1)             |
| `-seed`       | —              | `0` (random)             | RNG seed for reproducible traffic        |

## Wire format

Payloads are CBOR maps with **integer keys** (DATA_MODEL §1.1). The encoder in
`telemetry.go` deliberately mirrors the consumer's key constants in
[`cloud/telemetry/telemetry.go`](../../cloud/telemetry/telemetry.go) and writes
sensor values as full-precision `float64` (never shrunk to float16/float32).

```
{ 0: 1, 1: "fleet-sim-7", 2: 42, 3: 210000,
  4: 25.08, 5: 49.98, 6: 101.325,
  7: 0, 8: { 0: 5000, 1: 120, 2: 54880 } }
```

## Testing

```bash
go test ./...          # unit + contract + MQTT integration
go test -race ./...    # concurrency-clean check
```

- `telemetry_test.go` — round-trips the CBOR encoding, re-validates it against
  the DATA_MODEL §1.4 consumer rules, and checks determinism + float64
  precision.
- `contract_test.go` — feeds 500 generated samples through the **real** cloud
  decoder (`cloud/telemetry.Decode`) and asserts every one is accepted with no
  range violations. This is the regression guard that keeps fleet-sim and the
  cloud service in lock-step.
- `mqtt_integration_test.go` — runs the full publisher against an in-process
  minimal MQTT 3.1.1 broker with **60 concurrent devices** over a real TCP
  socket, verifying topic naming, per-device monotonic `seq`, and that every
  payload decodes as `Valid`. No external broker required.

## Notes & limitations

- The simulator models **telemetry traffic only** — it does not simulate OTA
  commands, LWT/status topics, or broker auth/TLS (local dev runs MQTT without
  TLS; documented in DATA_MODEL §4).
- `timestamp_ms` is a simulated per-device uptime, not wall clock (matching the
  firmware's `k_uptime_get()` semantics; wall-clock alignment is the consumer's
  job per DATA_MODEL §1.1).
