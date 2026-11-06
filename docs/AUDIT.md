# Audit and Remediation — 2026-11

Scope: the full tree at the 2026-07 quality-gate state, re-audited against the
running code and repository contracts. Historical reviews are archived under
[`reviews/`](reviews/).

## Findings and resolution

| ID | Severity | Finding | Resolution |
|----|----------|---------|------------|
| A-01 | High | OTA was compiled but unreachable: `main.c` never initialized it, telemetry's MQTT handler ignored `MQTT_EVT_PUBLISH`, and the OTA topic default contradicted `DATA_MODEL.md`. | Wired through telemetry publish/connection callbacks; payload is read from the socket and QoS1-acked; topic is `cmd/<device_id>/ota`; resubscribe on CONNACK; full frame/state tests. |
| A-02 | High | Telemetry was discarded while disconnected (`drain_and_publish` popped before publish), and the 55s sleep window serviced MQTT only once per ~60s. | Do not drain while disconnected; one-reading retry slot; publish failure keeps the reading; `power_sched` service callback calls `telemetry_process()` every 100 ms during sleep; default cycle shortened to 10s. |
| A-03 | High | Default `native_sim` is 32-bit and CI did not install multilib; the app overlay only had the legacy `native_sim.overlay` name; 64-bit builds expose `%lld`/`%llu` mismatches; `devenv.nix` used removed options (`languages.python.packages`, `libusb`). | Default `native_sim/native/64`; added `boards/native_sim_native_64.overlay`; 64-bit format casts fixed; `devenv.nix` uses `python.withPackages` and `libusb1`, and `devenv shell` evaluates. |
| A-04 | Medium | Fault injection compiled but no hook was called; Kconfig help overclaimed. | `fault_inject_init()` and sensor/drop/flash hooks are in the real paths; app defaults the framework off; Kconfig text corrected. |
| A-05 | Medium | OTA tests covered only state getters and falsely claimed integration coverage; the test overlay duplicated the board flash label; repeated sessions failed because slot1 was never erased. | Full start/data/finish/abort and reject-path tests; duplicate overlay removed; `CONFIG_IMG_ERASE_PROGRESSIVELY=y` erases slot1 pages as chunks arrive; false comments replaced. |
| A-06 | Medium | Cloud ignored connect/subscribe timeouts, trusted payload device IDs without topic validation, and had no `main.go` tests. | Explicit timeout handling; single-`+` topic filter validation; topic/device consistency gate; `/health` readiness; main-package metric tests; golden indefinite-CBOR fixture. |
| A-07 | Medium | `just power-bench` produced zero samples because it reused the 60s-cycle build; HIL `UNKNOWN` exited 0. | Short-cycle benchmark overlay, fail-on-no-data, and correct usage (`just power-bench` / `just power-bench 10`); HIL `UNKNOWN` fails; `bme280`/`ota` excluded from hardware suites with rationale. |
| A-08 | Medium | README listed a non-existent `bootloader/` repo dir and missing LICENSE, claimed the demo ran firmware, and stale reviews claimed 10/10 invariants. | MIT LICENSE; README simplified and corrected; demo docs distinguish fleet-sim vs firmware; historical reviews moved to `docs/reviews/` with banners; roadmap rewritten. |
| A-09 | Low | QoS1 packet id derived from `seq`; `main()` returned 0 on fatal sensor init; cloud tests only used definite-length CBOR; `sensor_log` accepted use before init; `just init` ran `west update` every build. | Dedicated 16-bit packet id; `main()` returns `-ENODEV`; 75-byte golden firmware payload decoded by the cloud tests; init guards; `init` is offline-safe and `just update` refreshes. |
| A-10 | Usability | No contract/e2e guard, fleet-sim tests absent from CI, no OTA publisher tool, no diagrams or unified check. | `check-contract.sh`, `tests/e2e`, `tests/ota-sim`, `just check`/`e2e`/`contract`/`ota-sim`; CI jobs; Excalidraw architecture/dataflow/demo/OTA diagrams. |
| A-11 | Hardware | HIL scripts claimed an unvalidated nRF52840 path; real MCUboot swap was not enabled. | Added nRF52840 MCUboot board/sysbuild layout, signed-image verification, `just build-hil`, and CI cross-build; HIL remains build-only because no physical DK is available. |
| A-12 | Security | MQTT was plaintext and unauthenticated. | TLS 1.2+ with CA verification and password authentication is now the default for firmware, cloud, fleet/OTA simulators, Compose, and e2e. Dev certs/passwords are clearly marked and rotate via `infra/tls/generate.sh`. |
| A-13 | Hardware | The advertised nRF52840/MCUboot path had no buildable configuration. | Added board overlays, `firmware/sysbuild.conf`, `just build-hil`, signed-image verification, and a CI cross-build job. Physical swap/flash validation remains HIL. |

## Wire/API changes

- OTA frames replace the old JSON-like strings; the topic is
  `cmd/<device_id>/ota` (see [`OTA.md`](OTA.md)). Pre-1.0 breaking change; OTA was
  never functional before.
- New C callbacks: `telemetry_set_publish_handler`,
  `telemetry_set_connection_handler`, `telemetry_client`; OTA exposes
  `ota_handle_frame`, `ota_matches_topic`, `ota_on_connected/disconnected`.
- Cloud `/health` now returns 503 until MQTT is connected; new metrics
  `telemetry_topic_mismatch_total` and
  `telemetry_last_seen_timestamp_seconds`.
- CBOR schema remains version 1; the 75-byte firmware payload is the shared
  golden fixture.

## Verification

All commands ran in the workspace with `devenv shell` on `native_sim/native/64`
(GCC 15.2, west v1.5.0, Go 1.26). Results:

| Check | Result |
|-------|--------|
| `just build` | PASS |
| `just test` | PASS — bme280 4, sensor_log 7, power_sched 7, telemetry 9, ota 12, fault_inject 14 (53 total) |
| `just check` | PASS — build, all ztests, cloud/fleet/ota Go tests, contract |
| `go test -race ./...` (cloud, fleet-sim) | PASS |
| `E2E_MODE=local ./tests/e2e/run.sh` | PASS — fleet telemetry + 75-byte golden payload observed in `/metrics` |
| `just power-bench` | PASS — non-zero ACTIVE/IDLE/SLEEP counters |
| `E2E_MODE=local` with TLS | PASS — TLS broker, password auth, fleet + golden payload |
| `just firmware-tap` | PASS — native_sim firmware completed TLS handshake, authenticated as `edge-device`, and published telemetry |
| `COMPOSE="podman compose" just demo` | PASS — 10 devices, 860 messages, TLS, Grafana/Prometheus healthy |
| `COMPOSE="podman compose" just e2e` | PASS — Docker-mode compose path via Podman, TLS fleet traffic observed |
| `just build-hil` (nRF52840 + MCUboot) | PASS — cross-built, app signed (385 KB signed hex), `imgtool verify` accepted |
| `shellcheck tests/...` | PASS (warning severity) |
| `docker compose config` | PASS |

Re-run with `just check`; any deviation is a blocking defect.

## Residual risks

- The committed CA/server certificates and `change-me` passwords are
  development material. They are sufficient to make the local/CI demo secure
  against passive sniffing and anonymous publishes, but must be replaced and
  kept in a secret store for any shared deployment.
- The nRF52840 image is signed with MCUboot's **default debug key** and is
  cross-built + signature-verified, not physically flashed/swap-tested. Set
  `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` to a private production key, add an IP
  transport (shield or Thread border router), and run the HIL checklist on a DK.
- `native_sim` still simulates OTA staging; the real bootloader path is the
  nRF52840 sysbuild target.
- Fleet telemetry is synthetic.
- Observability is local Docker/Podman only.
- `just firmware-tap` verifies the firmware TLS handshake/auth/publish path
  locally over a rootless TAP; CI cannot run it because it needs
  `CAP_NET_ADMIN`/`/dev/net/tun`.
