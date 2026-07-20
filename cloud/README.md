# cloud/ — Go telemetry ingestion service

MQTT subscriber that decodes CBOR telemetry payloads (per
[`../docs/DATA_MODEL.md`](../docs/DATA_MODEL.md) §1) and exposes Prometheus
metrics plus a `/health` endpoint.

## Architecture

```
MQTT broker ──► cloud-ingest ──► /metrics ──► Prometheus ──► Grafana
                  │
                  └──► /health (liveness)
```

## Configuration (environment variables)

| Variable      | Default              | Description                          |
|---------------|----------------------|--------------------------------------|
| `MQTT_BROKER` | `tcp://localhost:1883` | MQTT broker URL                    |
| `MQTT_TOPIC`  | `telemetry/+`        | MQTT subscription topic (wildcard)   |
| `HTTP_ADDR`   | `:9090`              | HTTP listen address                  |
| `CLIENT_ID`   | `cloud-ingest`       | MQTT client identifier               |

## Endpoints

- **`/metrics`** — Prometheus metrics (telemetry gauges, counters, validation stats)
- **`/health`** — JSON health check (`{"status":"ok"}`)

## Prometheus Metrics

| Metric                            | Type  | Labels                | Description                          |
|-----------------------------------|-------|-----------------------|--------------------------------------|
| `telemetry_messages_total`        | counter | `device_id`         | Total messages received per device   |
| `telemetry_decode_errors_total`   | counter | —                   | CBOR decode failures                 |
| `telemetry_unknown_schema_total`  | counter | —                   | Unsupported schema_version           |
| `telemetry_invalid_total`         | counter | —                   | Missing required fields              |
| `telemetry_out_of_range_total`    | counter | `device_id`, `field`| Out-of-range sensor values           |
| `telemetry_temperature_celsius`   | gauge   | `device_id`         | Latest temperature (°C)              |
| `telemetry_humidity_percent`      | gauge   | `device_id`         | Latest humidity (%RH)                |
| `telemetry_pressure_kpa`          | gauge   | `device_id`         | Latest pressure (kPa)                |
| `telemetry_seq`                   | gauge   | `device_id`         | Latest sequence number               |

## Development

```bash
go test ./... -v    # run tests
go build .          # build binary
go vet ./...        # static analysis
```

## Docker

```bash
docker build -t cloud-ingest .
docker run -p 9090:9090 -e MQTT_BROKER=tcp://mosquitto:1883 cloud-ingest
```
