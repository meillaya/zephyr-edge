# Deploy & Networking Research — IP transport for the nRF52840 DK, gateway/HIL host, cloud deployment

Status: research note for `docs/ROADMAP.md` §"Next: hardware validation" and §"Next: production hardening".
Date of price/link checks: **2026-11-10**.
Repo baseline read: `README.md`, `docs/ROADMAP.md`, `docs/DEMO.md`, `docs/AUDIT.md` §Residual risks,
`tests/hil/README.md`, `infra/README.md`, `infra/docker-compose.yml`,
`firmware/boards/nrf52840dk_nrf52840.conf`, `firmware/prj.conf`, `firmware/west.yml`
(**Zephyr pinned to `v4.1.0`**), `.github/workflows/ci.yml`, `justfile`.

All Zephyr claims below were checked against the **`v4.1.0` tag** of
`github.com/zephyrproject-rtos/zephyr` (raw/Kconfig/dts/sample files) and
`docs.zephyrproject.org/4.1.0/…`. Prices are USD as displayed by the vendor page on 2026-11-10;
CAD is **not quoted** (no Canadian vendor price fetched — see §9); CAD ≈ USD × ~1.37 market rate is
UNVERIFIED. Nothing here was invented: where a page could not be read, the row says **UNVERIFIED**.

---

## 0. TL;DR (recommendation)

1. **Transport: W5500 SPI Ethernet shield on the DK's Arduino header.** Seeed W5500 Ethernet Shield
   (SKU 103030021, **USD $35.00**, <https://www.seeedstudio.com/W5500-Ethernet-Shield.html>) is the
   only option with a **full upstream Zephyr v4.1.0 path**: `wiznet,w5500` devicetree binding +
   `CONFIG_ETH_W5500` driver + a `seeed_w5500` shield whose documentation literally shows
   `west build -b nrf52840dk/nrf52840 --shield seeed_w5500 samples/net/dhcpv4_client`.
2. **Exact extra hardware to buy for "live MQTT/OTA from the device":** the W5500 shield above
   ($35.00) + one Cat-5e/6 patch cable (typical $4–8, **UNVERIFIED**; any cable on hand works) +
   the host PC / gateway on the same Ethernet switch/router. Total ≈ **$35–43 USD**. No new
   debugger, no gateway board, no cloud account needed for the bench step. The DK keeps its
   existing USB (J-Link + serial console) for flashing.
3. **Zero-BOM alternative (backup, not first choice):** USB CDC-ECM gadget mode
   (`CONFIG_USB_DEVICE_STACK=y` + `CONFIG_USB_DEVICE_NETWORK_ECM=y`) — Zephyr v4.1.0 ships the class
   and lists a `usbnet` test (`depends_on: usb_device`). No hardware, but extra host-side network
   setup and it is not exercised on `nrf52840dk` in upstream CI, so do it as a spike, not the demo.
4. **Gateway/always-on + HIL runner: Raspberry Pi 5 4 GB or Pi 4 4 GB is enough; Pi 5 8 GB is
   overkill.** 2026 pricing is inflated (Pi 5 8 GB = **$200**, Pi 4 4 GB = **$120**, Pi 5 4 GB =
   **$130** at Adafruit on 2026-11-10); the whole 4-service compose stack + `node_exporter`-style
   runner fits in 2–4 GB. Budget **Pi 5 4 GB ($130) + 27 W PSU ($14.04) + Active Cooler ($13.50)**
   = **$157.54**, or **Pi 4 4 GB ($120) + 15 W PSU ($8.74) + case fan ($7.25) = $135.99**.
   **A GitHub-hosted runner cannot see the DK over USB** — a **self-hosted runner on this gateway
   (or the bench PC) is mandatory** for HIL CI (confirmation in §5).
5. **Deployment: bench-first, then one small VPS** running the existing compose stack (Hetzner
   shared from **€5.49/mo excl. VAT**, <https://www.hetzner.com/cloud/>; or Fly.io
   `shared-cpu-*` from **$2.19–$8.78/mo**, <https://fly.io/pricing/>). Keep **self-hosted Mosquitto
   with our own CA/TLS**, because the firmware already does MQTT-over-TLS with an embedded CA and
   password auth (`firmware/src/telemetry/telemetry_ca_cert.h`, generated from `infra/tls/ca.crt`).
   Managed MQTT (EMQX Serverless free tier, HiveMQ from $0.34/h) forces different auth/credential
   plumbing and should be a later phase. **Local compose stays the validation path.**
6. Everything below is still **bench-only**: no radio certification (FCC/ISED) is needed for a
   wired W5500 demo, but becomes relevant the moment the product ships custom radio hardware
   (Thread/BLE/Wi-Fi) — see §8.

---

## 1. Why the device has no IP today (repo facts, not speculation)

- `firmware/boards/nrf52840dk_nrf52840.conf` disables the native-sim stack and emulators only:
  `CONFIG_NET_NATIVE=n`, `CONFIG_I2C_EMUL=n`, `CONFIG_FLASH_SIMULATOR=n`,
  `CONFIG_TEST_RANDOM_GENERATOR=n`. It enables **no L2 at all** — no Ethernet, no PPP, no Thread.
- `firmware/prj.conf` already turns on everything above the link layer: `CONFIG_NETWORKING=y`,
  `CONFIG_NET_SOCKETS=y`, `CONFIG_NET_TCP=y`, `CONFIG_NET_IPV4=y`, `CONFIG_MQTT_LIB=y`,
  `CONFIG_MQTT_LIB_TLS=y`, `CONFIG_TLS_CREDENTIALS=y`, mbedTLS with
  `CONFIG_MBEDTLS_HEAP_SIZE=30000`, `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=4096`, plus
  `CONFIG_NET_CONFIG_SETTINGS=y` with **TAP-only addresses**
  (`CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.0.2.1"`, peer `192.0.2.2`). Those are TEST-NET-1 native-sim
  values and **must be overridden** in any nRF52840 IP build.
- TLS trust is compiled in: `telemetry.c` registers `telemetry_ca_cert` (generated from
  `infra/tls/ca.crt` by `infra/tls/generate.sh`), `peer_verify = TLS_PEER_VERIFY_REQUIRED`, hostname
  from `CONFIG_MQTT_TLS_SERVER_NAME`. ⇒ **Any remote deployment needs a new server cert with the
  right SAN, a regenerated CA header, and a firmware rebuild** — plan for it.
- `tests/hil/README.md` already names the gap: *"the nRF52840 DK has no wired IP interface by
  default… needs a network transport (Ethernet/Wi-Fi shield or a Thread border router and a
  `CONFIG_NET_L2_OPENTHREAD` app configuration)."*
- CI (`.github/workflows/ci.yml`, job `hardware-build`, `runs-on: ubuntu-latest`) only **cross-builds**
  and verifies the MCUboot signature. It never touches USB — which is exactly the gap for HIL.

---

## 2. Transport option comparison (A)

All Zephyr v4.1.0 evidence links in §10. Difficulty 1 = hours, 5 = multi-week.

| # | Option | Extra BOM (USD) | Zephyr v4.1.0 support (exact names) | Firmware/host work | Pros | Cons | Diff. |
|---|--------|-----------------|--------------------------------------|--------------------|------|------|-------|
| 1 | **W5500 SPI Ethernet shield** (primary) | Seeed 103030021 **$35.00** + cable | binding `wiznet,w5500` (requires `int-gpios`, optional `reset-gpios`); Kconfig `CONFIG_ETH_W5500` (`depends on DT_HAS_WIZNET_W5500_ENABLED`, `select SPI`); shield `seeed_w5500` sets `CONFIG_NET_L2_ETHERNET=y` and docs show an `nrf52840dk/nrf52840` build | ~25–40 lines: `--shield seeed_w5500` on `just build-hil`; app overlay sets LAN IPv4/DHCP + entropy source, overrides native-sim `192.0.2.x`; verify MAC; host: broker on LAN, cert SAN = host/IP | Upstream driver+shield+docs; 100 Mbit full duplex; wired = deterministic CI; zero radio certification | Needs the shield on the DK header; consumes `arduino_spi` (= `spi3`, P1.13–P1.15) and D2 (INT); 5 V/3.3 V stacking to verify; not a "product" form factor | **2** |
| 2 | ENC28J60 SPI module | generic module ~**$6–12 UNVERIFIED** | binding `microchip,enc28j60` (requires `int-gpios`); Kconfig `CONFIG_ETH_ENC28J60` (`depends on DT_HAS_MICROCHIP_ENC28J60_ENABLED`, `select SPI`); 10BASE-T only per binding/doc | same shape as W5500 but you wire jumpers yourself (no standard shield); ~30–50 lines overlay+cabling | Cheapest possible wired IP; driver exists | **10BASE-T only** (10 Mbit, binding says "10BASE-T Ethernet controller"); no mainstream shield; clock-ready errata/timeout knobs (`ENC28J60_CLKRDY_INIT_WAIT_MS`); jumpers/3.3 V wiring; not worth it vs W5500 | **3** |
| 3 | **USB CDC-ECM gadget** (zero BOM) | $0 (one USB cable + free host port) | `CONFIG_USB_DEVICE_STACK=y`, `CONFIG_USB_DEVICE_NETWORK_ECM=y` (verbatim from upstream `samples/net/sockets/echo_server/overlay-netusb.conf`); sample test `echo_server.usbnet` has `depends_on: usb_device` (excludes native_sim only); DK board doc lists USB as supported | ~20–30 lines: conf fragment + optional `zephyr,cdc-ecm-ethernet`-style DT node (ECM needs no remote-mac DTS; NCM variant uses `zephyr,cdc-ncm-ethernet`); host: Linux `cdc_ether` creates `usb0`, configure static IPs or dnsmasq; no changes to telemetry.c (still zsock) | Zero purchase; same TLS/MQTT code path; 12 Mbit FS is plenty for 75-byte CBOR | Not CI-tested on `nrf52840dk` upstream (sample allows any `usb_device` board); depends on host kernel/driver; the DK's nRF-USB port must be cabled separately from the J-Link port; Windows/macOS drivers vary; do as spike | **2** |
| 4 | **PPP over serial/USB** | $0 | **Correction: `CONFIG_NET_PPP` does not exist in v4.1.0.** The v4.1.0 option is `CONFIG_NET_L2_PPP` (`subsys/net/l2/ppp/Kconfig`), documented only as an L2 (LCP/HDLC/IPCP/IPV6CP) with *no* bundled serial transport in the file list; testing docs point at external `pppd` via `net-tools` (`loop-pppd.sh`) and the cellular-modem sample | highest: implement/adapt a UART↔PPP transport, wire the DK USB-CDC or UART1 to it, host `pppd` with IPCP, test TLS over the link; ~60–100 lines + host config | Zero BOM; reuses `pppd`; no cert changes | More integration risk than ECM; PPP + mbedTLS over a 115200 UART is slow to bring up; debugging LCP/IPCP is its own project; docs steer to QEMU/pppd, not hardware | **4** |
| 5 | **Thread** (nRF52840 end device + RCP) | Pi (border router) + **nRF52840 Dongle RCP** (price **UNVERIFIED**, Nordic page Cloudflare-blocked; historically ~$10–15) + microSD/PSU if no Pi yet | `CONFIG_NET_L2_OPENTHREAD` (`subsys/net/l2/openthread/Kconfig`; selects `OPENTHREAD`, `NET_IPV6`); upstream `echo_server.openthread` test **allows `nrf52840dk/nrf52840`**; OTBR Docker image `openthread/border-router` with `OT_RCP_DEVICE=spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=1000000` and `--device=/dev/ttyACM0` | ~40–60 lines app conf (OT settings/credentials/IPv6/NAT64) + RCP firmware build/flash for the dongle (`-DOT_BOOTLOADER=USB` per OpenThread docs) + Pi OTBR Docker | Uses the nRF radio that's otherwise idle; real product-grade mesh path; 1 dongle suffices here (OpenThread's 2-dongle list assumes a dongle as end device — our end device is the DK) | IPv6-only: broker reachability needs OTBR NAT64 or an IPv6 broker; two more moving parts (Pi + dongle + OTBR); slowest to "live MQTT" | **4** |
| 6 | BLE (nRF52840 peripheral → gateway) | Pi or PC with BLE + custom bridge | BLE stack works (`RADIO` row in DK doc; BT tests), but **no IP**: no mainline BLE 6LoWPAN/IPSP path for this app | Custom GATT service + gateway daemon that relays to MQTT, or a BLE↔MQTT bridge; hundreds of lines, new protocol surface | Lowest power; no wiring; uses built-in radio | Not IP: the device cannot speak MQTT/TLS itself; roadmap already calls this "future work"; a bridge is a second data path to maintain | **5** |
| 7 | Wi-Fi | — | Options are weak, see below | — | — | — | **3–5** |

### 7. "Wi-Fi options with nRF52840 — what actually exists"

- **No on-chip Wi-Fi** in nRF52840 (radio is BLE + 802.15.4 only; DK doc lists `RADIO` = Bluetooth,
  ieee802154).
- **WINC1500 shield exists upstream**: `boards/shields/adafruit_winc1500/` with
  binding `atmel,winc1500`, overlay on `&arduino_spi` (IRQ D7, reset D5, enable D6), and
  `Kconfig.defconfig` defaulting `CONFIG_WIFI=y`. **But** WINC1500 is a legacy Microchip part,
  its Zephyr driver is low-throughput/test-grade, the Adafruit WINC1500 shields (P3653/P3654) are
  old stock — availability/price **UNVERIFIED**. Treat as a lab curiosity, not the demo path.
- **nRF7002 EK**: upstream shield exists (`boards/shields/nrf7002ek/`, `nordic,nrf7002`), but the
  nRF70 driver targets Nordic's Wi-Fi-capable hosts; running it from an nRF52840 is **UNVERIFIED /
  not recommended** (build the host part on `nrf5340dk` if Wi-Fi is ever a requirement).
- Verdict: for this project, **wired W5500 first, USB-ECM second, Thread third, Wi-Fi not now**.

### Detail — why W5500 on the nRF52840 DK works (pin/electrical mapping)

- The DK's Arduino SPI is `arduino_spi: &spi3` with `cs-gpios = <&arduino_header 16 GPIO_ACTIVE_LOW>`
  (D10) in `boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts`; pinctrl `spi3_default` maps
  **SCK = P1.15 (D13), MISO = P1.14 (D12), MOSI = P1.13 (D11)** (from
  `nrf52840dk_nrf52840-pinctrl.dtsi`). The shield pinout matches exactly (D10 CS, D11 MOSI,
  D12 MISO, D13 SCK, **D2 = INT**, RST) per Zephyr's shield documentation.
- The Seeed overlay is 10 lines: `&arduino_spi { status="okay"; eth-w5500@0 { compatible =
  "wiznet,w5500"; reg = <0x0>; spi-max-frequency = <DT_FREQ_M(20)>; int-gpios =
  <&arduino_header 8 GPIO_ACTIVE_LOW>; /* D2 */ }; };` — no `reset-gpios` needed.
- **Electrical caveat (verify before first power-up):** the Seeed shield is an Arduino Uno-shaped
  board documented as "3.3 V operation with 5 V I/O signal tolerance". The DK Arduino header runs
  3.3 V logic and supplies 5 V/3.3 V/GND. Zephyr's shield doc explicitly supports an
  `nrf52840dk/nrf52840` build, which implies the combination is intended to work, but **confirm the
  shield's level-shifter/regulator rails on the bench and measure that W5500 VDD = 3.3 V** before
  plugging in. Also: the DK's `i2c0` (BME280, P0.26/27) does **not** conflict with `spi3`
  (P1.13–P1.15).
- Build wiring: `west build -b nrf52840dk/nrf52840 --sysbuild --shield seeed_w5500 -d build-hil
  firmware` (add the shield flag to `just build-hil`/HIL scripts, keep the MCUboot sysbuild layout).
  App Kconfig additions: `CONFIG_NET_L2_ETHERNET=y` (shield defconfig does this),
  `CONFIG_NET_IPV4=y` (already), DHCP client (`CONFIG_NET_DHCPV4=y`) **or** static LAN IP overlay,
  an entropy source (board conf sets `CONFIG_TEST_RANDOM_GENERATOR=n`; enable the nRF RNG entropy
  driver — exact symbol to confirm on first build), and remove/override the native-sim
  `CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.0.2.1"`/peer values. Keep RAM head-room: 256 KB total,
  mbedTLS heap is 30 KB, W5500 RX thread defaults 800 B — watch `CONFIG_ETH_W5500_RX_THREAD_STACK_SIZE`
  and the main stack (`CONFIG_MAIN_STACK_SIZE=16384` today).

### Purchase reality check

- The shield Adafruit resells as **P2971 "Ethernet Shield for Arduino - W5500"** is **"No longer
  stocked"** at $39.95 (<https://www.adafruit.com/product/2971>) — do **not** order it.
- Buy the **Seeed Studio W5500 Ethernet Shield v1.0 (SKU 103030021)** direct: $35.00, page shows
  shipping/duties at checkout and local-warehouse shipping (checked 2026-11-10).
- If Seeed is unavailable, Zephyr also has an `arceli_eth_w5500` shield (INT D9 / RST D8) targeting
  the cheap ARCELI W5500 Arduino boards; the bare WIZnet W5500 module + jumpers is a fallback
  (**price/stock UNVERIFIED**). A spare **WIZ850io**-style 3.3 V W5500 module is worth having on the
  bench as a known-good SPI peripheral.

---

## 3. Recommended transport: exact shopping list

| Item | Part / SKU | Price | Vendor URL | Notes |
|------|-----------|-------|------------|-------|
| W5500 Ethernet shield | Seeed **103030021** (W5500 Ethernet Shield v1.0) | **USD $35.00** | <https://www.seeedstudio.com/W5500-Ethernet-Shield.html> | Arduino form factor, SPI, INT on D2, RST on RST pin; "3.3 V operation with 5 V I/O signal tolerance" |
| Ethernet cable | Cat-5e/6 patch, 1–2 m | ~$4–8 **UNVERIFIED** (any cable on hand) | — | 100BASE-TX link to the same switch as the broker host |
| *(optional)* spare W5500 module | WIZ850io / bare W5500 3.3 V module | **UNVERIFIED** | WIZnet / Digi-Key / Mouser | Known-good SPI peripheral for bench debugging |
| *(later)* gateway/HIL host | Raspberry Pi 5 4 GB + 27 W PSU + Active Cooler | **$130.00 + $14.04 + $13.50 = $157.54** | <https://www.adafruit.com/product/5812>, </product/5814>, </product/5815> | See §4 |

Estimated time to live MQTT/OTA from the device on the bench: **1–2 days** with the shield
(1–2 h build/config once the overlay exists, rest TLS-cert/host-network work; OTA validation is the
long pole because it needs the MCUboot swap run). The **firmware change is small**; the recurring
cost is the deployment TLS/cert work identified in §1.

---

## 4. Gateway / bench host / always-on hardware (B)

Purpose of the box: run `mosquitto` + `cloud-ingest` + `prometheus` + `grafana`
(the 4-service `infra/docker-compose.yml`), DNS/TLS termination, and the **GitHub Actions
self-hosted runner** with the DK attached for HIL. Measured need: 4 small containers, a Go binary,
Prometheus scrape of 2 targets, Grafana SQLite — **2 GB RAM is comfortable, 1 GB works with tuning;
ARM64 is fine** (all four images are multi-arch; `cloud-ingest` is built locally from `cloud/`).

Prices below: Adafruit (Raspberry Pi Approved Reseller) pages checked **2026-11-10**, USD. The
Raspberry Pi Foundation's own store was Cloudflare-blocked for this research; Adafruit is the
official-reseller proxy and its prices track MSRP. **2026 RAM-market inflation is visible** (Pi 4
4 GB is $120, not the historical $55) — re-check before ordering.

| Role | Model | Price (USD) | Vendor URL | Why |
|------|-------|-------------|------------|-----|
| **Gateway/runner (recommended value)** | **Raspberry Pi 4 Model B 4 GB** (P4296) | **$120.00** | <https://www.adafruit.com/product/4296> | 2–4 GB is plenty; Gigabit Ethernet; ARM64 self-hosted runner supported by GitHub docs; cheaper than Pi 5 |
| Gateway/runner (faster, longer support) | **Raspberry Pi 5 4 GB** (P5812) | **$130.00** | <https://www.adafruit.com/product/5812> | PCIe, faster CPU, 27 W PSU; needs Active Cooler for 24/7 |
| Gateway/runner (no budget) | Raspberry Pi 5 8 GB (P5813) | **$200.00** | <https://www.adafruit.com/product/5813> | Overkill for this stack; only if also running heavy local tooling |
| Minimal gateway only | Raspberry Pi Zero 2 W with header (P6008) | **$19.80** | <https://www.adafruit.com/product/6008> | **Out of stock** at check; 512 MB is tight for Prometheus+Grafana; micro-USB PSU; 2.4 GHz-only Wi-Fi (use the OTG Ethernet adapter or skip) |
| x86 alternative | Used/refurb N100 or Lenovo/Dell micro (eBay/refurb) | ~$120–250 **UNVERIFIED** | — | Best raw perf/dollar and x64 CI tooling, but no official single source/price; power draw higher |
| PSU (Pi 5) | Official 27 W USB-C PSU (P5814) | **$14.04** | <https://www.adafruit.com/product/5814> | Official; required for full 5 A |
| PSU (Pi 4) | Official 5.1 V 3 A USB-C PSU (P4298) | **$8.74** | <https://www.adafruit.com/product/4298> (price shown on P4296 page) | Official |
| Cooling (Pi 5) | Official Active Cooler (P5815) **or** Case+Fan (P5816) | **$13.50** / **$12.00** | </product/5815>, </product/5816> | Pick one; 24/7 case fan in the official case is fine for a compose host |
| Cooling (Pi 4) | Official Case Fan + Heatsink (P4794) | **$7.25** | <https://www.adafruit.com/product/4794> (price shown on P4296 page) | |
| Boot/OS storage | microSD 32 GB A1/A2 | ~$8–12 **UNVERIFIED** | Adafruit / PiShop.ca / BuyAPi.ca | CI logs + Docker images grow; prefer USB SSD |
| Fast storage (Pi 5) | Pimoroni NVMe Base (P5845) + Raspberry Pi NVMe SSD 256 GB (P6090) | **$19.95 + $75.60** (SSD **out of stock**) | </product/5845>, </product/6090> | **Expensive in 2026**; USB 3 SSD (UNVERIFIED ~$35–60) is the smarter buy |
| UPS | Witty Pi 5 HAT+ (P6449) + CR2032 (P654) | **$59.95 + $0.95** | <https://www.adafruit.com/product/6449> | Mode-1 Power HAT+ (5 A), RTC + clean shutdown; protects CI host from brownouts. Cheaper route: any 5 V mini-UPS **UNVERIFIED** |
| PoE (optional) | PoE+ HAT for Pi 5 | **UNVERIFIED** | — | Only if the bench/router has PoE; otherwise wall PSU |
| Switch | any 5/8-port gigabit unmanaged | ~$20–35 **UNVERIFIED** | — | Isolate test traffic; put DK + gateway + broker host on it |
| Isolated test AP/router (optional) | travel router / guest VLAN | **UNVERIFIED** | — | So HIL traffic cannot reach the home LAN; not required for first light |
| **CI power-cycle for the DK** | USB relay module **or** switchable USB hub (e.g. YKUSH 3, `uhubctl`-compatible hub) **or** smart plug feeding the DK's USB PSU | **UNVERIFIED** (Yepkit page fetch failed; generic relay ~$10–20, hub ~$25–70) | <https://github.com/mvp/uhubctl> (tool), vendor pages UNVERIFIED | Automates the "unplug/replug" step of MCUboot/flash-hang recovery; also lets CI cold-boot into the swapped image. Simplest reliable: smart plug on the DK USB supply + `uhubctl`-capable hub for the data port. |

**Minimum viable bench host:** the developer laptop/PC already used for `just demo` (Docker/Podman)
+ the DK on USB + the W5500 shield on the LAN. The Pi is only needed for 24/7 + HIL CI.

**Canadian sourcing (UNVERIFIED):** PiShop.ca, BuyAPi.ca, Lee's Electronics (Adafruit distributor,
Montreal), Digi-Key/Mouser Canada. No CAD price was fetched in this session — budget CAD ≈ USD × 1.37
plus 13% HST and possible duty, and re-check stock (Pi Zero 2 W and NVMe SSDs showed out-of-stock at
Adafruit on the check date).

---

## 5. Is a self-hosted runner required? Yes (and what it needs)

- **GitHub-hosted runners cannot access USB hardware on your desk.** GitHub's docs describe hosted
  runners as GitHub-managed **virtual machines**: *"{% data variables.product.prodname_dotcom %}
  hosts Windows runners and Linux runners … on virtual machines in Azure"* and standard Linux jobs
  run on *"a multi-cloud compute pool … Amazon Web Services (AWS) or Microsoft Azure"*
  (<https://docs.github.com/en/actions/concepts/runners/github-hosted-runners>). There is no
  mechanism in the service to attach a local USB device (J-Link/DK) to those VMs; the runner
  machine is remote and ephemeral. So the DK can never enumerate on a GitHub-hosted runner.
- **What is required instead:** a **self-hosted runner** — *"a system that you deploy and manage to
  execute jobs"*, which *"Give[s] you more control of hardware, operating system, and software
  tools than GitHub-hosted runners provide"* and can *"install software available on your local
  network"* (<https://docs.github.com/en/actions/concepts/runners/self-hosted-runners>).
- **Minimal runner host:** any Linux x64 or ARM64 machine (Raspberry Pi 4/5 is explicitly supported
  by the reference: `x64` and `ARM64` Linux are listed supported architectures) that (a) can run
  the Actions runner app, (b) has outbound HTTPS 443 to GitHub, (c) has **Docker** if the workflow
  uses container actions/service containers, and (d) has the **DK on USB** (udev rule +
  `dialout`/`plugdev` groups, or run with the J-Link udev rules installed). See
  <https://docs.github.com/en/actions/reference/runners/self-hosted-runners> for the exact
  requirements list. For this repo the runner needs:
  - `west`/`cmake`/`ninja`/`dtc`, Zephyr SDK (`ZEPHYR_SDK_INSTALL_DIR`), a cached west workspace
    (same as the existing CI cache), `go`, `just`, `docker`;
  - SEGGER J-Link software + USB permissions, DK serial (`/dev/ttyACM*`) readable;
  - the power-cycle device from §4 (relay/hub/smart plug) for flaky-flash recovery;
  - either `runs-on: [self-hosted, linux, nrf52840dk]` with a repo/org-level runner, or a separate
    runner *label* so normal cloud jobs never touch it.
- **Recommended CI split on the Pi:** keep the existing GitHub-hosted jobs (`build`, `unit`,
  `go-test`, `contract`, `compose-e2e`, `hardware-build`) exactly as-is, and add one
  `runs-on: [self-hosted, nrf52840dk]` job that builds+flashes the DK, runs
  `tests/hil/run_tests.sh -t /dev/ttyACM0`, then runs the OTA swap test. The DK image is already
  cross-built on hosted runners; the self-hosted job only needs the flash/serial steps.
- **Cost:** self-hosted runners are free to use with GitHub Actions; you pay for the machine
  (the Pi above) and electricity (UNVERIFIED, ~5–10 W idle for Pi 4/5 + PSU overhead).

---

## 6. Deployment options (C)

### 6.1 Options and what they need

| Option | What runs where | Hardware needed | Account/ops needed | Cost / month | Fit |
|--------|-----------------|-----------------|--------------------|--------------|-----|
| **Local compose (current)** | Laptop/Pi: `infra/docker-compose.yml` | bench PC/Pi, DK + W5500, switch | none | **$0** (+ electricity) | **P0 — validate end-to-end here first** |
| **VPS + self-hosted compose** (recommended for "remote") | Hetzner/DigitalOcean/Fly machine running the same 4 services (+Caddy/Traefik for LE) | VPS only | domain, DNS, SSH keys, firewall, backups | Hetzner shared **from €5.49/mo excl. VAT** (<https://www.hetzner.com/cloud/>); DO entry Droplet price **UNVERIFIED** (page JS-rendered); Fly VM $2.19–$8.78 + volume $0.15/GB-mo + egress $0.02/GB (<https://fly.io/pricing/>) → **≈$5–12/mo** | **P1** — closest to today's stack, TLS model unchanged (own CA/cert) |
| **Managed MQTT (EMQX Serverless)** | EMQX cloud broker; `cloud-ingest`+Prometheus+Grafana still on a small VPS/Fly | VPS (no broker) | EMQX account | **$0/mo free tier** (1 M session-minutes, 1 GB traffic, 1 M rule actions; <https://www.emqx.com/en/pricing>) + host ~$5–12 | P2 — deletes broker ops, but firmware must speak EMQX's auth (user/pass or cert) and DNS-verified TLS; extra work |
| **Managed MQTT (HiveMQ)** | HiveMQ Cloud Starter | VPS for ingest/obs | HiveMQ account | **From $0.34/hour + $0.80/million messages** (≈$248/mo if left running; <https://www.hivemq.com/pricing/>); self-managed Lab is free but only 25 conn / 25 msg/s, 30-day license | Not hobby-priced; skip |
| **AWS IoT Core** | AWS broker; ingest on EC2/Lambda | none (+ VPS for Prom/Grafana if kept) | AWS account, IoT policies, X.509 device certs | **UNVERIFIED** — the pricing page did not render numbers (<https://aws.amazon.com/aws-iot-core/pricing/>) | P2/P3 — X.509 + policy model means firmware TLS-credential rework (current firmware uses CA+password, not client certs) |
| **Oracle Always Free** | Ampere A1 / AMD micro VM | none | Oracle account, card for verification | **$0** Always Free tier; 30-day **US$300** credit (<https://www.oracle.com/cloud/free/>); exact free OCPU/RAM allowance **UNVERIFIED** (not in rendered page) | P2 — free but capacity/region gotchas; fine for demo |
| **Self-hosted Mosquitto (as today) on VPS** | included in VPS row | — | — | $0 licence | **Recommended compose variant** |

### 6.2 Recommended deploy path

1. **Bench (P0), now:** W5500 shield on DK + `infra/docker-compose.yml` on the dev host, same LAN.
   Regenerate `infra/tls` with a SAN that covers the host name/IP the firmware uses
   (`CONFIG_MQTT_TLS_SERVER_NAME`), regenerate `firmware/src/telemetry/telemetry_ca_cert.h` via
   `infra/tls/generate.sh`, rebuild, flash, watch `telemetry/fleet-sim-*`-equivalent live data in
   Grafana. This closes roadmap item "add an IP transport for live MQTT/OTA".
2. **Remote (P1), after bench:** one VPS (Hetzner CX/CPX shared or Fly 1–2 GB) running the *same*
   compose file, with:
   - **DNS** A/AAAA record for e.g. `mqtt.example.dev`; **Let's Encrypt** via Caddy/Traefik
     (`https://letsencrypt.org/` — free, 90-day certs, auto-renew). Replace the committed dev CA
     with a real one; keep the *device* CA separate from the public web cert if you want a
     long-lived fleet trust anchor (recommended: private device CA embedded in firmware; public LE
     cert for Grafana).
   - **Firewall:** expose only `8883/tcp` (MQTT TLS) and `443` (Grafana behind auth/Caddy), never
     `3000/9090/9091` publicly; rotate the `change-me` passwords in
     `infra/mosquitto/config/passwd` and `infra/tls` and move them out of git (Docker secrets /
     SOPS / systemd credentials).
   - **Secrets:** MQTT user/pass per device; `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` set to a private
     MCUboot key (currently the default debug key — AUDIT A-11/A-13 residual risk); never commit.
3. **Managed MQTT (P2), only if broker ops hurt:** EMQX Serverless free tier is the only
   hobby-priced managed option verified here; plan a firmware auth change and a DNS-verified
   hostname; HiveMQ/AWS IoT are not price-appropriate yet.
4. **Never deploy the current dev certs/passwords to a shared host** — same warning as
   `docs/DEMO.md` §Security note and `infra/README.md`.

### 6.3 Monthly cost table (verified numbers only; currency as shown)

| Stack | Compute | Broker | Observability | Domain/TLS | Total ≈ |
|-------|---------|--------|---------------|-----------|---------|
| Local compose (bench) | $0 (existing PC) / Pi electricity | self-hosted | self-hosted | $0 (dev certs) | **$0/mo** |
| Hetzner shared VPS, all-in-one | **from €5.49/mo excl. VAT** | self-hosted Mosquitto | Prometheus+Grafana in same VM | LE $0 + domain (~$10–15/yr, **UNVERIFIED** registrar quote) | **≈ €5.5–6.5/mo (~$6–7)** |
| Fly.io split apps | `shared-cpu-1x` 256 MB **$2.19/mo**, `shared-cpu-2x` 512 MB **$4.39/mo**, 1 GB **$8.78/mo** + volume **$0.15/GB-mo** + egress **$0.02/GB** | self-hosted | self-hosted | LE $0 + domain | **≈$5–12/mo** |
| EMQX Serverless | small VPS/Fly for ingest+obs ≈$2.19–5.49 | **$0** free tier | in same host | LE+domain | **≈$2.5–6/mo** |
| HiveMQ Cloud Starter | small VPS for ingest+obs | **$0.34/h + $0.80/M msgs** | in same host | LE+domain | **≈$250+/mo** (not recommended) |

USD figures as displayed at check time; EUR VAT/currency differences ignored. Exchange rate not
fetched. Re-check before committing budget: cloud prices change often.

### 6.4 Account-only vs hardware

- **Account-only:** all cloud/VPS/managed-MQTT rows, domain, Let's Encrypt.
- **Hardware:** the DK + W5500 shield + cable (device path), and a gateway/HIL host (Pi or reused
  PC). The device path is unchanged between local and remote deployment — only the broker
  hostname/SNI, CA cert, and credentials move.

---

## 7. End-to-end validation sequence (with hardware per step)

| # | Step | Command / action (repo) | Hardware needed | Pass criterion |
|---|------|------------------------|-----------------|----------------|
| 1 | Host/sim baseline | `just build`, `just test`, `just demo` | none (container runtime) | 53 ztests pass; Grafana sees fleet telemetry; `/health` 200 |
| 2 | Cross-build | set `ZEPHYR_SDK_INSTALL_DIR`; `just build-hil` | none (host SDK) | signed nRF52840 image + MCUboot; `imgtool verify` OK |
| 3 | Flash + HIL suites | `tests/hil/run_tests.sh -t /dev/ttyACM0` | **DK** + USB + J-Link | `sensor_log power_sched telemetry fault_inject` PASS on serial |
| 4 | Real sensor | wire BME280 on `i2c0` (SDA P0.26 / SCL P0.27, addr 0x76) per `boards/nrf52840dk_nrf52840.overlay` | **BME280 module** + jumpers | readings match reference ±0.5 °C / ±3 %RH / ±1 hPa (roadmap item 2) |
| 5 | Link up | `--shield seeed_w5500` build + flash; `net iface`/`net ping` (net-shell) or `dhcpv4_client`-style check | **W5500 shield + cable + switch/router** | Ethernet iface up, IP obtained/static, ARP/ping to broker host OK |
| 6 | Live MQTT/TLS | `infra/docker-compose.yml` up on LAN host; regenerate cert/CA header for that hostname; flash new firmware | DK+shield+host | device publishes CBOR to `telemetry/<id>`; `cloud-ingest` `/metrics` shows messages; Grafana panel updates; broker logs TLS 1.2+ |
| 7 | Power behaviour | `just power-bench` on host; PPK2 on DK for real µA/mW (roadmap item 3) | DK + **Nordic PPK2** (or power monitor) | non-zero ACTIVE/IDLE/SLEEP; measured current recorded |
| 8 | OTA swap | sign test image (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`), publish `cmd/<id>/ota` frames with `tests/ota-sim`; cold-boot the DK | DK + shield + J-Link/serial (+ relay if CI) | MCUboot swap-move observed, new version boots, slot1 erased; invalid image rejected (roadmap item 4) |
| 9 | HIL CI | self-hosted runner on gateway: build → flash → serial verdict → power cycle → OTA | **gateway host (Pi 4/5) + DK + shield + relay/hub** | workflow green; logs show `PROJECT EXECUTION SUCCESSFUL`; failure paths fail the job |
| 10 | Remote deploy | VPS + DNS + LE; update `CONFIG_MQTT_TLS_SERVER_NAME`, CA header, broker creds; flash | VPS (cloud) + device path | device publishes to remote broker over TLS; Grafana on `https://<domain>`; remote OTA completes |

Notes: steps 1–2 need no hardware; 3–9 need the DK; 4 needs a BME280; 5–10 need the W5500 shield;
9–10 additionally need the always-on gateway and a cloud account. On native_sim, only steps 1/6
(TAP variant) are possible today (`just firmware-tap` needs `CAP_NET_ADMIN`/`/dev/net/tun`, which CI
cannot run — AUDIT residual risk).

---

## 8. Certification caveat (FCC/ISED) for eventual product deployment

- A **wired W5500 build for bench/CI** needs no radio certification: it is a development kit plus a
  wired shield (still respect local EMC; the Seeed shield carries EU DoC/RoHS/UKDoC docs on its page).
- The moment custom **radio** hardware ships (Thread/802.15.4, BLE, Wi-Fi, or a custom board around
  the nRF52840), the end product needs **FCC equipment authorization (US, 47 CFR Part 15)** and
  **ISED certification (Canada, RSS standards)**. Development kits and modules are sold for
  evaluation and are **not** automatically certified as end products; modular grants only transfer
  if the module is used per its grant conditions (antenna, shielding, labeling).
- Practical implication for this roadmap: keep the **demo as dev-kit + wired shield**, do not
  advertise a wireless version as deployable, and budget certification + test-house time before any
  field pilot. Reference pages: FCC equipment authorization
  <https://www.fcc.gov/engineering-technology/laboratory-division/general/equipment-authorization>;
  ISED certification <https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/equipment-certification>
  (pages not fetched in this session — guidance-level citation only, **UNVERIFIED** wording).

---

## 9. Unverified / follow-up items

1. **nRF52840 Dongle (PCA10059) price** could not be read — `nordicsemi.com` returned Cloudflare
   HTTP 403. Historical ~$10–15, but **UNVERIFIED**; Digi-Key/Mouser also unread. This only affects
   the Thread option, not the recommendation.
2. **Raspberry Pi Foundation store prices** blocked (Cloudflare) — all Pi prices above are from
   Adafruit (approved reseller) on 2026-11-10, not the Foundation's own page. CAD prices and
   Canadian stock (PiShop.ca / BuyAPi.ca / Lee's Electronics) not fetched.
3. **GitHub-hosted runners "cannot use USB"** is an architectural inference from GitHub's docs
   (hosted = GitHub-managed cloud VMs, multi-cloud AWS/Azure; no USB passthrough feature documented)
   rather than one verbatim sentence. The docs do explicitly support the self-hosted workaround.
4. **W5500 shield + nRF52840 DK electrical stacking** — Zephyr documents the target build and Seeed
   documents 5 V-tolerant I/O, but the level-shifter/regulator rails were not verified on hardware
   here. Measure before first power-up.
5. **USB CDC-ECM on nrf52840dk** — supported Kconfig exists and the sample test only requires
   `usb_device`, but there is no upstream CI coverage of that board/class combo in the files checked.
   Needs a 1-hour spike; also confirm which DK USB port exposes the nRF52840 (separate from the
   J-Link debug port).
6. **PPP** — the originally assumed `CONFIG_NET_PPP` was not found in v4.1.0; the real option is
   `CONFIG_NET_L2_PPP`, which is L2-only. Whether a ready-made USB-CDC↔PPP transport exists for this
   board was not confirmed → high integration risk, deprioritised behind ECM.
7. **Fly.io as MQTT TLS endpoint**: Fly terminates TLS at its edge; a `[[services]]` port with TLS
   may prevent Mosquitto from doing its own client-cert/CA validation the way the firmware expects.
   Not verified — another reason to prefer a plain VPS for the first remote deploy.
8. **Cloud pricing gaps:** DigitalOcean droplet price (JS-rendered page) and Oracle Always-Free
   OCPU/RAM allowance were not readable; AWS IoT Core pricing page did not render either. All marked
   UNVERIFIED above; do not budget from memory.
9. **CI power-cycling hardware**: YKUSH product page fetch failed (404 at the URL tried); generic
   USB relay/hub/smart-plug prices are UNVERIFIED. `uhubctl` is the software reference.
10. **Throughput/CPU**: W5500 100 Mbit + mbedTLS on a 64 MHz Cortex-M4F will be the bottleneck, not
    the shield. Payloads here are 75-byte CBOR, so fine; if TLS handshake time or MQTT keepalives
    become a problem, tune `CONFIG_MBEDTLS_HEAP_SIZE`/`CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` and stack
    sizes on the nRF52840 (RAM 256 KB). Not measured.

---

## 10. Source index (primary evidence)

Zephyr v4.1.0 (all GitHub links are `blob/v4.1.0`):

- W5500 Kconfig: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/drivers/ethernet/Kconfig.w5500>
  — `menuconfig ETH_W5500`, `depends on DT_HAS_WIZNET_W5500_ENABLED`, `select SPI`.
- W5500 binding: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/dts/bindings/ethernet/wiznet,w5500.yaml>
  — `compatible: "wiznet,w5500"`, required `int-gpios`, optional `reset-gpios`.
- Seeed W5500 shield overlay: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/shields/seeed_w5500/seeed_w5500.overlay>
  and defconfig <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/shields/seeed_w5500/Kconfig.defconfig>
  (`SHIELD_SEEED_W5500` → `CONFIG_NET_L2_ETHERNET=y`).
- Zephyr shield doc (nrf52840dk build example, pin table): <https://docs.zephyrproject.org/4.1.0/boards/shields/seeed_w5500/doc/index.html>
- nRF52840 DK DTS: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts>
  (`arduino_spi: &spi3`, `cs-gpios = <&arduino_header 16 …>`); pinctrl SPI3 P1.13/14/15:
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840-pinctrl.dtsi>
- nRF52840 DK board doc (USB supported): <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/doc/index.rst>
- ENC28J60: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/drivers/ethernet/Kconfig.enc28j60>,
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/dts/bindings/ethernet/microchip,enc28j60.yaml>.
- PPP: Kconfig <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/subsys/net/l2/ppp/Kconfig>
  (`CONFIG_NET_L2_PPP`); docs <https://docs.zephyrproject.org/4.1.0/connectivity/networking/api/ppp.html>;
  test tooling <https://github.com/zephyrproject-rtos/net-tools/blob/master/README.md> (§PPP Connectivity).
- OpenThread: Kconfig <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/subsys/net/l2/openthread/Kconfig>
  (`CONFIG_NET_L2_OPENTHREAD`); upstream sample test allowing `nrf52840dk/nrf52840`:
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/samples/net/sockets/echo_server/sample.yaml>
  (`sample.net.sockets.echo_server.openthread`).
- USB ECM: overlay <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/samples/net/sockets/echo_server/overlay-netusb.conf>
  (`CONFIG_USB_DEVICE_STACK=y`, `CONFIG_USB_DEVICE_NETWORK_ECM=y`), NCM DTS example
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/samples/net/sockets/echo_server/usbd_next_ncm.overlay>,
  sample test `echo_server.usbnet` (`depends_on: usb_device`) in the sample.yaml above.
- WINC1500 shield (Wi-Fi): <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/shields/adafruit_winc1500/adafruit_winc1500.overlay>;
  nRF7002 EK shield: <https://github.com/zephyrproject-rtos/zephyr/tree/v4.1.0/boards/shields/nrf7002ek>.

OpenThread Border Router / RCP:

- OTBR Docker: <https://openthread.io/guides/border-router/build-docker> (`OT_RCP_DEVICE=spinel+hdlc+uart:///dev/ttyACM0`, `--device=/dev/ttyACM0`).
- OTBR prepare / dongle RCP: <https://openthread.io/guides/border-router/prepare> (nRF52840 dongles, `-DOT_BOOTLOADER=USB`).
- `ot-nrf528xx`: <https://github.com/openthread/ot-nrf528xx>.

GitHub Actions:

- Hosted runners are GitHub-managed VMs (Azure/AWS multi-cloud): <https://docs.github.com/en/actions/concepts/runners/github-hosted-runners>.
- Self-hosted runners: <https://docs.github.com/en/actions/concepts/runners/self-hosted-runners>,
  requirements: <https://docs.github.com/en/actions/reference/runners/self-hosted-runners>.
- USB hub control tool: <https://github.com/mvp/uhubctl>.

Products / prices (checked 2026-11-10, USD):

- Seeed W5500 Ethernet Shield SKU 103030021, **$35.00**: <https://www.seeedstudio.com/W5500-Ethernet-Shield.html>
  (product PDF: <https://files.seeedstudio.com/Bazaar/product_pdf/103030021.pdf>).
- Adafruit P2971 W5500 shield — **"No longer stocked"**, $39.95: <https://www.adafruit.com/product/2971>.
- Raspberry Pi 5 8 GB **$200.00** <https://www.adafruit.com/product/5813>; Pi 5 4 GB **$130.00**
  <https://www.adafruit.com/product/5812>; Pi 4 4 GB **$120.00** <https://www.adafruit.com/product/4296>;
  Pi Zero 2 W with header **$19.80, out of stock** <https://www.adafruit.com/product/6008>.
- Official 27 W PSU **$14.04** <https://www.adafruit.com/product/5814>; Pi 5 Active Cooler **$13.50**
  <https://www.adafruit.com/product/5815>; Pi 5 Case + Fan **$12.00** <https://www.adafruit.com/product/5816>;
  Pimoroni NVMe Base **$19.95** <https://www.adafruit.com/product/5845>; RPi NVMe SSD 256 GB
  **$75.60, out of stock** <https://www.adafruit.com/product/6090>; Witty Pi 5 HAT+ **$59.95**
  <https://www.adafruit.com/product/6449>.
- Hetzner Cloud, shared **from €5.49/mo excl. VAT** (cost-optimised from €5.99; dedicated from €36.99):
  <https://www.hetzner.com/cloud/>.
- Fly.io Machines: `shared-cpu-1x·256MB $2.19/mo`, `shared-cpu-2x·512MB $4.39/mo`,
  `shared-cpu-4x·1GB $8.78/mo`, volumes `$0.15/GB-mo`, egress `$0.02/GB` (NA/EU):
  <https://fly.io/pricing/>.
- EMQX Serverless **$0/mo free tier** (1 M session-minutes, 1 GB traffic, 1 M rule actions);
  Dedicated Flex from **$0.32/h ≈ $234/mo**: <https://www.emqx.com/en/pricing>.
- HiveMQ: self-managed Lab free (25 conn/25 msg/s, 30-day license), Launch **$299/mo**, Run
  **$499/mo**; managed Starter **from $0.34/hour + $0.80/million messages**:
  <https://www.hivemq.com/pricing/>.
- Oracle Cloud Free Tier (Always Free services incl. Arm Ampere A1; US$300/30-day trial):
  <https://www.oracle.com/cloud/free/>. Exact free allowances **UNVERIFIED**.
- AWS IoT Core pricing page (numbers not rendered) **UNVERIFIED**: <https://aws.amazon.com/aws-iot-core/pricing/>.
- DigitalOcean pricing (numbers not rendered) **UNVERIFIED**: <https://www.digitalocean.com/pricing/droplets>.
- Let's Encrypt (free TLS): <https://letsencrypt.org/>.
- FCC/ISED certification references (not fetched): <https://www.fcc.gov/engineering-technology/laboratory-division/general/equipment-authorization>,
  <https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/equipment-certification>.
