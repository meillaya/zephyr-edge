# infra/ — local observability stack

docker-compose stack for local development and CI:

| Service        | Image                    | Port  | Purpose                          |
|----------------|--------------------------|-------|----------------------------------|
| `mosquitto`    | `eclipse-mosquitto:2`    | 1883  | MQTT broker (no TLS — dev only)  |
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
- **MQTT**: localhost:1883
- **cloud-ingest**: http://localhost:9090/metrics (and `/health` readiness)

`cloud-ingest` runs as a non-root user and its health check returns 503 until
the MQTT subscription is connected.

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
