# infra/ — local observability stack

docker-compose stack for local development and CI:

| Service        | Image                    | Port  | Purpose                          |
|----------------|--------------------------|-------|----------------------------------|
| `mosquitto`    | `eclipse-mosquitto:2`    | 8883  | MQTT broker (TLS + password)     |
| `cloud-ingest` | built from `../cloud/`   | 9090  | Go telemetry ingestion + metrics |
| `prometheus`   | `prom/prometheus:v3.4.0` | 9091  | Metrics scraping + storage       |
| `grafana`      | `grafana/grafana:11.5.2` | 3000  | Dashboards (auto-provisioned)    |

## Quick start

```bash
cd infra/
docker compose up -d --build
# or: podman compose -f docker-compose.yml up -d --build
```

- **Grafana**: http://localhost:3000 (admin/admin)
- **Prometheus**: http://localhost:9091
- **MQTT**: `localhost:8883` over TLS; users `edge-device` / `edge-ingest`, password `change-me` (dev only)
- **cloud-ingest**: http://localhost:9090/metrics (and `/health` readiness)

`cloud-ingest` runs as a non-root user and its health check returns 503 until
the MQTT subscription is connected. It connects with `tls://mosquitto:8883`,
uses `/certs/ca.crt`, and authenticates as `edge-ingest`.

The committed `tls/` certificates and `mosquitto/config/passwd` are local
development credentials. They are intentionally in the repo so the demo works
out of the box; never reuse them for a shared deployment. See
[`tls/README.md`](tls/README.md) to regenerate and rotate.

## Auto-provisioned dashboard

The "Zephyr Edge Telemetry" dashboard is auto-provisioned via
`grafana/provisioning/` and shows:

- Temperature (°C), humidity (%RH), pressure (kPa) per device
- Message counts, decode/unknown-schema/invalid counters, out-of-range stats
- Latest sequence per device
- Devices seen in the last 5 minutes, topic/device mismatches, last-seen age

## Health checks

All services have docker-compose health checks. The startup order is:
`mosquitto` → `cloud-ingest` → `prometheus` → `grafana` (each waits for the
previous service to be healthy).

## Cleanup

```bash
docker compose down -v
```
