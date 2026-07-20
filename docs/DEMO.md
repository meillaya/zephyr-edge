# End-to-End Demo

This guide walks through the full end-to-end (e2e) demo of the Zephyr edge
platform: a fleet of simulated edge devices publishing CBOR telemetry over MQTT
to the cloud ingestion service, with live metrics in Prometheus and Grafana.

## Prerequisites

| Tool           | Version  | Purpose                          |
|----------------|----------|----------------------------------|
| Docker         | ≥ 24     | Runs the observability stack     |
| Docker Compose | ≥ 2.20   | Orchestrates multi-service stack |
| Go             | ≥ 1.24   | Builds and runs the fleet sim    |
| just           | ≥ 1.30   | Task runner (see `justfile`)     |
| curl, jq       | any      | Health-check smoke tests         |

All tools are available inside the devenv shell (`devenv shell`) except Docker,
which must be installed on the host.

## Quick start

```bash
# From the repository root (zephyr-edge-platform/):
just demo            # 10 simulated devices (default)
just demo devices=50 # 50 simulated devices
```

The command:

1. **Starts the infra stack** — Mosquitto (MQTT broker), cloud-ingest (Go
   telemetry service), Prometheus, and Grafana via `docker compose`.
2. **Waits for health checks** — blocks until cloud-ingest reports healthy.
3. **Launches the fleet simulator** — `tests/fleet-sim/main.go` publishes
   synthetic CBOR telemetry (matching [`DATA_MODEL.md`](DATA_MODEL.md)) to the
   MQTT broker for N concurrent devices.
4. **Waits for telemetry** — polls Prometheus until `telemetry_messages_total`
   shows data from at least one device.
5. **Prints dashboard URLs** — Grafana, Prometheus, cloud-ingest metrics.
6. **Runs until Ctrl-C** — then tears down the fleet sim and the Docker stack.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Fleet Simulator (Go)                                           │
│  tests/fleet-sim/main.go -n <N>                                 │
│                                                                 │
│  ┌──────────┐  ┌──────────┐       ┌──────────┐                 │
│  │ device-1 │  │ device-2 │  ...  │ device-N │                 │
│  └────┬─────┘  └────┬─────┘       └────┬─────┘                 │
│       │              │                  │                       │
│       └──────────────┼──────────────────┘                       │
│                      │ MQTT PUBLISH (QoS 1)                     │
│                      │ topic: telemetry/fleet-sim-<N>           │
│                      │ payload: CBOR map (DATA_MODEL.md §1)     │
└──────────────────────┼──────────────────────────────────────────┘
                       ▼
              ┌─────────────────┐
              │   Mosquitto     │  :1883
              │   MQTT Broker   │
              └────────┬────────┘
                       │ subscribe: telemetry/+
                       ▼
              ┌─────────────────┐
              │  cloud-ingest   │  :9090
              │  (Go service)   │──→ /metrics (Prometheus format)
              │                 │──→ /health
              └────────┬────────┘
                       │ scrape :9090/metrics
                       ▼
              ┌─────────────────┐
              │   Prometheus    │  :9091
              └────────┬────────┘
                       │ query
                       ▼
              ┌─────────────────┐
              │    Grafana      │  :3000
              │  (dashboards)   │
              └─────────────────┘
```

## Dashboard URLs

| Service        | URL                          | Credentials     |
|----------------|------------------------------|-----------------|
| Grafana        | http://localhost:3000        | admin / admin   |
| Prometheus     | http://localhost:9091        | —               |
| Cloud ingest   | http://localhost:9090/metrics| —               |
| MQTT broker    | localhost:1883               | anonymous (dev) |

## Grafana dashboard

The **"Zephyr Edge Telemetry"** dashboard is auto-provisioned
(`infra/grafana/provisioning/`) and shows:

- Temperature (°C), humidity (%RH), pressure (kPa) time series per device
- Message counts (`telemetry_messages_total`)
- Decode errors, unknown-schema rejections, out-of-range flags
- Per-device sequence numbers (gap detection)

## Useful Prometheus queries

```promql
# Total messages per device
telemetry_messages_total

# Temperature per device
telemetry_temperature_celsius

# Rate of decode errors
rate(telemetry_decode_errors_total[1m])

# Out-of-range sensor values
telemetry_out_of_range_total

# Devices currently reporting
count(telemetry_seq) by (device_id)
```

## Manual teardown

If the demo is interrupted without a clean Ctrl-C:

```bash
cd infra/
docker compose down -v
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `cloud-ingest` not healthy | MQTT broker not ready | Check `docker compose logs mosquitto` |
| No telemetry in Prometheus | Fleet sim not publishing | Check fleet-sim stdout for MQTT errors |
| Grafana shows "No data" | Prometheus scrape delay | Wait 15–30 s; check Prometheus targets page |
| Port conflict (1883/9090/9091/3000) | Another service on the port | `lsof -i :<port>` and stop the conflicting process |

## Security note

MQTT runs **without TLS** in this demo (documented limitation). Do not expose
these ports outside localhost. See [`DATA_MODEL.md`](DATA_MODEL.md) §4 for the
topic conventions.
