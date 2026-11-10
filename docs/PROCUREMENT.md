# Hardware Procurement & End‑to‑End Validation Guide

**Scope:** everything you need to buy (and where to buy it in Canada) to take this
repository from the `native_sim` CI baseline to a physically validated and deployed
platform: nRF52840‑DK flashing + HIL tests, real BME280 bench validation, real
power measurement, live MQTT/TLS/OTA from the device, and a 24/7 deployment.

**Price/link check date:** 2026‑11‑10. Prices fluctuate — re‑check at checkout.
**Repo baseline:** Zephyr `v4.1.0` (`firmware/west.yml`), board `nrf52840dk/nrf52840`,
MCUboot sysbuild, custom BME280 driver, MQTT+TLS CBOR telemetry, Go cloud‑ingest +
Mosquitto/Prometheus/Grafana compose (`infra/`), HIL scripts (`tests/hil/`).

**Currency assumptions:** USD→CAD **1.4123** (Bank of Canada daily rate, 2026‑11‑09);
Ontario **HST 13%**. CAD totals below are *estimates before shipping*, unless a CAD
price is quoted directly by a Canadian seller. Everything is flagged **UNVERIFIED**
where it could not be confirmed from a seller/manufacturer page.

---

## 0. TL;DR — what to buy

The project has exactly four hardware‑gated gaps (from `docs/ROADMAP.md` §"Next:
hardware validation" and `docs/AUDIT.md` §"Residual risks"): a real DK, a real BME280,
a power monitor (Nordic PPK2) for µA measurement, and an IP transport for live
MQTT/OTA. Buy in tiers — each tier is independently useful:

| Tier | What it buys | Unlocks | Est. one‑time CAD |
|------|--------------|---------|-------------------|
| **0 — Touch silicon** | nRF52840 DK + 2 USB data cables | flash, HIL ztests, real MCUboot swap/signature rejection, serial verdicts | **≈ $100–115** |
| **1 — Sensor + live IP** *(recommended)* | + BME280 breakout (Pimoroni — factory 0x76, no mod), SHT45 reference sensor, jumpers, Seeed W5500 Ethernet shield, Cat‑5e cable | real BME280 readings, live MQTT/TLS/OTA from the device, end‑to‑end telemetry into Grafana | **≈ $235–300** |
| **2 — Real power** | + Nordic Power Profiler Kit II (PPK2) + spare micro‑USB cable | µA/mW sleep‑current measurement (closes the last ROADMAP item) | **≈ $390–430** |
| **3 — 24/7 deploy + HIL CI** | + Raspberry Pi 5 4 GB kit (PSU/cooler/case/storage), optional USB relay | always‑on compose stack, self‑hosted GitHub Actions runner with the DK attached | **≈ $640–690** (+$25–90 optional) + optional **≈ $9–12/mo** VPS |

*Tier 3 is optional if you already have a spare Linux PC/server — the same stack runs
on it, and a remote deployment can be account‑only (one small VPS).*

**Where to buy (short version):** the two Nordic items from **Digi‑Key Canada** (free
shipping at **CAD $100+**, DDP, ~24 h delivery to Toronto); the BME280 + Raspberry Pi kit
from **PiShop.ca** (Canadian Adafruit/Pimoroni reseller, CAD prices, ships Canada‑only
from Ottawa); the reference SHT45 from **Adafruit** (US, not stocked in Canada) or
Digi‑Key if available; the W5500 Ethernet shield either from **Seeed Studio** (the
upstream‑verified Seeed shield) or **PiShop.ca** (W5500‑based Arduino Ethernet Shield 2,
with a pinout caveat — §3.2); the Raspberry Pi and cables from PiShop / local Toronto
shops (Sayal, Creatron); cloud deployment is account‑only. Details in §4.

---

## 1. What hardware closes which gap

| # | Gap in the repo | Source of truth | What physically unlocks it | Buy |
|---|-----------------|-----------------|----------------------------|-----|
| G1 | No physical DK was available; HIL is build‑only | `tests/hil/README.md`, `docs/AUDIT.md` A‑11/A‑13 | nRF52840 DK over USB (on‑board SEGGER J‑Link OB + CDC serial) | **DK** |
| G2 | Real BME280 readings never validated against a reference; `bme280` ztest asserts the native_sim fixture only | `docs/ROADMAP.md` item 2 | BME280 breakout on I²C addr 0x76 + a better reference sensor + wiring | **DK + BME280 + reference sensor** |
| G3 | Power scheduler only measures scheduler time, not µA/mW | `docs/ROADMAP.md` item 3, `firmware/prj.conf` note | Nordic PPK2 on the DK's P‑22 current‑measurement header (SB40) | **PPK2** |
| G4 | MCUboot swap‑move and invalid‑signature rejection not exercised on real flash | `docs/ROADMAP.md` item 4, `tests/hil/README.md` | Same DK + serial + `tests/ota-sim` host publisher | **DK** (+ host) |
| G5 | The DK has no wired IP interface; live MQTT/OTA can't run | `tests/hil/README.md`, `docs/ROADMAP.md` item 1 | SPI Ethernet shield (W5500) on the DK's Arduino header + LAN cable | **W5500 shield** |
| G6 | Deployment is local Docker/Podman only | `docs/ROADMAP.md` §"Production hardening" | A small always‑on host (Pi or existing PC) and/or a VPS account | **Pi (optional) + VPS** |
| G7 | CI cannot flash hardware (GitHub‑hosted runners are cloud VMs) | `.github/workflows/ci.yml`, GitHub docs | Self‑hosted runner on the gateway with the DK on USB + power‑cycle relay | **Pi/mini‑PC + relay** |
| G8 *optional* | Fleet/multi‑device behaviour is synthetic | `docs/ROADMAP.md` non‑goals | A second DK or an nRF52840 Dongle | **2nd board** |

---

## 2. Bill of materials

All prices are **unit price at the linked seller on 2026‑11‑10** unless marked
*"snapshot"* (retrieved from an Internet Archive capture because the seller's live
page blocks automated fetches; still useful as a price anchor — verify at checkout).

### 2.1 Tier 0 — flash, HIL, MCUboot (must‑have)

| Item | Exact part | Qty | Price | Est. landed CAD | Buy from | Why / notes |
|------|-----------|-----|-------|-----------------|----------|-------------|
| nRF52840 DK | Nordic **PCA10056**, order code **NRF52840‑DK** | 1 | **CAD 79.38** *(snapshot 2025‑10‑13, InStock)*; live cross‑check **€43.45 excl. VAT** (Digi‑Key LU, 412 in stock) | ≈ 80 + HST | [Digi‑Key Canada](https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/8593726) | The repo's pinned board `nrf52840dk/nrf52840`; on‑board J‑Link OB means **no external probe needed**; Arduino‑R3 headers; P‑22 current‑measurement pads. Nordic's own site publishes no price. |
| USB‑A→micro‑USB **data** cables | any reputable 2.0 cable, e.g. 2‑pack | 2 | ≈ CAD 10–15 | ≈ 12 | Amazon.ca / local shop | The DK has **two** micro‑USB ports (J‑Link/OB + nRF USB). Charge‑only cables are the #1 "board won't enumerate" cause. |
| *(already owned)* Linux/macOS/Windows host | — | 1 | $0 | — | — | Runs Zephyr SDK, `west`, Docker/Podman, `just`, serial capture. |

**Tier 0 total: ≈ CAD 100–115** including HST (DK ≈ $89.7 taxed + cables), before
shipping. **Shipping note:** a DK‑only Digi‑Key order is below the CAD $100 free‑shipping
threshold, so it attracts ≈ CAD $15 delivery — either add the PPK2 (Tier 2) or a cheap
in‑stock filler to the same order, or accept the delivery charge. Software is all free:
Zephyr SDK + west (host toolchain), SEGGER J‑Link software, nRF Connect for Desktop
(Power Profiler), Docker/Podman, `just`.

> Note: Digi‑Key lists the DK "Contents: Board(s)" — do **not** assume a USB cable is
> in the box; the PPK2 explicitly does **not** include its micro‑USB cable either.

### 2.2 Tier 1 — real sensor + live IP transport (recommended)

| Item | Exact part | Qty | Price | Est. landed CAD | Buy from | Why / notes |
|------|-----------|-----|-------|-----------------|----------|-------------|
| BME280 breakout *(recommended)* | **Pimoroni PIM472** — "BME280 Breakout" | 1 | **CAD 20.95** (in stock) | ≈ 21 + HST | [PiShop.ca](https://www.pishop.ca/product/bme280-breakout-temperature-pressure-humidity-sensor/) | **Factory I²C address 0x76 — matches the repo overlay, no jumper mod.** 3.3 V or 5 V, includes male + right‑angle female headers (you still solder one header). This is the lowest‑friction match to `firmware/boards/nrf52840dk_nrf52840.overlay`. |
| BME280 alternative | Adafruit **PID 2652** (STEMMA QT) | 1 | **CAD 23.95** at PiShop; **USD 14.95** at Adafruit | ≈ 24 + HST | [PiShop.ca](https://www.pishop.ca/product/adafruit-bme280-i2c-or-spi-temperature-humidity-pressure-sensor/) / [Adafruit](https://www.adafruit.com/product/2652) | Factory address **0x77** → solder the **ADDR jumper** (SDO→GND) to get 0x76, or change the overlay's `reg` to 0x77. Also works; one extra mod step. |
| BME280 alternative (US) | SparkFun **SEN‑15440** (Qwiic) | 1 | **USD 16.95** | ≈ 25–30 | [SparkFun](https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280-qwiic.html) | **3.3 V only**; factory 0x77, selectable 0x76 via ADR jumper — inspect the jumper silkscreen on arrival. |
| Reference sensor | Sensirion **SHT45**, Adafruit **PID 5665** | 1 | **USD 12.50** (in stock) | ≈ 20–25 | [Adafruit](https://www.adafruit.com/product/5665) (check Digi‑Key Canada stock) | ±0.1 °C / ±1.0 %RH, ISO‑17025 calibration certificate — a credible reference for the ±0.5 °C / ±3 %RH acceptance check; I²C **0x44**, shares `&i2c0` with the BME280 without conflict. |
| *(budget reference alternative)* | Adafruit SHT40 / SHT41, PID 4885 / 5776 | 1 | **USD 5.95** each | ≈ 9–12 | Adafruit | Cheaper temp/RH reference; lower accuracy — check the datasheet before relying on it for the ±0.5 °C window. |
| Header + jumper wires | 1×5 male header (included with Pimoroni/Adafruit) + **male‑to‑female** dupont jumpers | 1 set | ≈ CAD 10 | ≈ 10 | Amazon.ca / Creatron / Sayal | The DK's Arduino headers are **sockets**; M‑F jumpers go male‑end into the DK socket and female‑end onto the breakout's soldered male header. (Or use the DK's through‑hole P13–P17 male pins with F‑F jumpers.) |
| W5500 Ethernet shield *(verified path)* | Seeed Studio **SKU 103030021** (W5500 Ethernet Shield v1.0) | 1 | **USD 35.00** (≈ CAD 49.4) | ≈ 55–65 | [Seeed Studio](https://www.seeedstudio.com/W5500-Ethernet-Shield.html) | Upstream Zephyr v4.1 has a **`seeed_w5500` shield** whose docs show `west build -b nrf52840dk/nrf52840 --shield seeed_w5500`; this is the lowest‑friction live‑IP path (§3.2). |
| W5500 shield *(domestic alternative)* | Arduino **Ethernet Shield 2** (W5500), PiShop SKU 1718 | 1 | **CAD 41.95** (in stock) | ≈ 42 + HST | [PiShop.ca](https://www.pishop.ca/product/arduino-ethernet-shield2/) | W5500‑based Arduino R3 shield, no import math. **Caveat:** Zephyr's W5500 binding requires `int-gpios`; the Seeed shield documents INT = D2, while the Arduino shield's INT routing and 5 V‑oriented level shifting are not verified for the DK — only choose this if you can confirm/patch a D2 INT wire; otherwise use the Seeed shield. |
| Ethernet patch cable | Cat‑5e/6, 1–2 m | 1 | ≈ CAD 8 | ≈ 8 | local / Amazon.ca | Connects shield → switch/router with the broker host. |
| *(optional)* soldering iron | basic temp‑controlled, e.g. Pinecil/TS101 or a makerspace loaner | 1 | CAD 40–60 | — | Amazon.ca / local | Only if you don't already have one and want to solder the breakout header yourself. |

**Tier 1 total: ≈ CAD 235–300** (DK + sensors + shield + wiring + HST), depending on
which shop the sensors come from. This is the tier that lets you close **all four**
ROADMAP hardware items except the PPK2 measurement, and run the full
device→broker→Grafana pipeline.

### 2.3 Tier 2 — real power measurement

| Item | Exact part | Qty | Price | Est. landed CAD | Buy from | Why / notes |
|------|-----------|-----|-------|-----------------|----------|-------------|
| Power Profiler Kit II | Nordic **NRF‑PPK2** — Adafruit **PID 5048**; also Digi‑Key `NRF-PPK2` | 1 | **USD 99.95** in stock (Adafruit); **CAD 136.50** *(snapshot 2025‑08‑08, InStock)*; live cross‑check **€95.25** (Digi‑Key LU) | ≈ 140 + HST | [Adafruit](https://www.adafruit.com/product/5048) or [Digi‑Key Canada](https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF-PPK2/13557476) | 200 nA–1 A, 100 kS/s, Amp‑meter **and** source modes, 8 digital inputs (low‑end logic analyzer), works with all Nordic DKs + the DK's P‑22 header. Free software (nRF Connect for Desktop → Power Profiler). |
| micro‑USB **data** cable for PPK2 | any 2.0 cable | 1 | ≈ CAD 5 | ≈ 5 | Amazon.ca / local | **Not included in the PPK2 box.** |
| *(optional)* CR2032 coin cell | e.g. Panasonic CR2032 | 1–2 | ≈ CAD 3 | ≈ 3 | Amazon.ca / local | The DK user guide's lowest‑noise supply for current measurements (CR2032 holder is on the DK). |

**Tier 2 total: ≈ CAD 390–430.** The PPK2 is the one "nice" instrument that pays for
itself here: it measures the 1–10 µA sleep states the firmware's software counters
cannot see. A cheap INA228/INA219 module (≈ CAD 10–20) can measure mA‑level active
current but not the µA floor — see §7 for the budget alternative.

### 2.4 Tier 3 — 24/7 deployment + HIL CI host (optional)

Everything here is optional: the bench steps run on the development PC. A gateway
turns the local compose stack into an always‑on deployment and makes HIL CI possible.

| Item | Exact part | Qty | Price | Est. landed CAD | Buy from | Why / notes |
|------|-----------|-----|-------|-----------------|----------|-------------|
| SBC / gateway | **Raspberry Pi 5 4 GB** (preferred value; 8 GB = CAD 249.50 if you also want desktop use) | 1 | **CAD 156.85** (4 GB) / **CAD 249.50** (8 GB) | ≈ 157–250 + HST | [PiShop.ca](https://www.pishop.ca/product/raspberry-pi-5-4gb/) | Runs Mosquitto + cloud‑ingest + Prometheus + Grafana, plus the GitHub Actions self‑hosted runner. (2026 DRAM‑market inflation: Pi 5 4 GB is $130 at Adafruit, CAD 156.85 at PiShop.) |
| PSU | Official 27 W USB‑C PSU (Pi 5) / 15 W for Pi 4 | 1 | **CAD 16.95** | ≈ 17 + HST | PiShop.ca | Unofficial/undersized supplies cause brown‑outs with a USB SSD or the DK attached. |
| Cooling / case | Official Pi 5 case (**CAD 14.45**) + Active Cooler (USD 13.50 ≈ CAD 19) or case+fan | 1 | ≈ CAD 15–33 | ≈ 15–33 | PiShop.ca / Adafruit | 24/7 operation; throttling ruins CI timing. |
| OS storage | 32 GB A1/A2 microSD (SanDisk Ultra **CAD 27.95**) or USB 3 SSD | 1 | **CAD 27.95** | 28 | PiShop.ca | microSD is fine at first; an SSD avoids card wear from Docker/CI logs (NVMe HAT+ Compact = CAD 21.25, SSD itself UNVERIFIED ~CAD 60–110). |
| *(optional)* UPS HAT or small UPS | Witty Pi 5 HAT+ (USD 59.95) or any 5 V mini‑UPS | 1 | ≈ CAD 70–90 | — | Adafruit/Amazon | Protects against brownouts/reboots of a 24/7 HIL runner. |
| *(optional)* USB relay / switchable hub | `uhubctl`‑compatible hub or USB relay / smart plug for the DK supply | 1 | ≈ CAD 25–40 *(UNVERIFIED)* | — | Amazon.ca / Adafruit | Lets CI power‑cycle the DK to recover from flash hangs and to cold‑boot into a swapped MCUboot image. |
| Remote deployment | small VPS (Hetzner shared from €5.49/mo; Fly.io ≈ $2.19–8.78/mo) + domain | — | **≈ CAD 9–12/mo** + ≈ CAD 15/yr domain | — | Hetzner / Fly.io / registrar | Account‑only; no hardware to buy. Let's Encrypt certs are free. |

**Tier 3 one‑time: ≈ CAD 640–690** (Pi 4 GB kit ≈ CAD 245–265 with HST + shipping;
add ~25–90 for an optional UPS/relay); **recurring ≈ CAD 9–12/mo** only if you add a
VPS. Reusing an existing Linux PC/server or a used x86 mini PC (eBay/Kijiji,
CAD 150–300) is a valid — sometimes cheaper — substitute for the Pi.

### 2.5 Optional / nice‑to‑have

| Item | Why | Est. price |
|------|-----|-----------|
| Second nRF52840 DK (NRF52840‑DK) | True multi‑device fleet/OTA tests; HIL scripts are single‑board today | CAD 79.38 *(snapshot)* |
| nRF52840 Dongle (**PCA10059**, NRF52840‑DONGLE) | Cheap second node / Thread RCP / BLE sniffer; **no on‑board debugger** | CAD 15.34 *(snapshot, Digi‑Key CA)* |
| SEGGER J‑Link EDU Mini | External debug probe if you later build a custom board; **not needed for the DK**; EDU = non‑commercial license | USD 76.00 in stock ([SEGGER](https://shop-us.segger.com/product/j-link-edu-mini-8-08-91/)) |
| Logic analyzer | I²C debugging beyond `i2c scan`; cheap 8‑ch 24 MHz clone + `sigrok`/PulseView works; Saleae/DSLogic for serious work | CAD 20–40 (clone); professional units UNVERIFIED |
| Multimeter | Continuity/power sanity checks | CAD 25+ |
| Spare BME280 module | Cross‑check / replacement; cheap generic modules may be counterfeit — verify chip ID and humidity registers | CAD 8–15 |
| Isolated test network | 5/8‑port gigabit switch or travel router so HIL traffic stays off the home LAN | CAD 20–40 *(UNVERIFIED)* |
| Case/standoffs, anti‑static mat | Bench tidiness, ESD safety | CAD 10–40 |

### 2.6 Do **not** buy / avoid

- **nRF5340 DK / nRF9160 DK / nRF54 DK** — wrong Zephyr board target; the repo's
  MCUboot partition layout and overlays are for the nRF52840.
- **W5100/W5200 Ethernet shields** — Zephyr v4.1 has a W5500 driver only (`Kconfig.w5500`);
  there is no W5100 driver in `drivers/ethernet/`. Buy the **W5500** Seeed shield.
- **"BMP280" boards** — same footprint, **no humidity sensor**; the repo's driver expects
  BME280 chip ID.
- **Charge‑only USB cables** and **no‑name Pi PSUs** — flaky bring‑up and brown‑outs.
- **nRF52840 Dongle as your only board** — no on‑board debugger; you cannot flash the
  HIL ztests with `west flash` as easily.
- **A second microcontroller platform** (ESP32/RP2040) expecting the firmware to run —
  this repo is a Zephyr app pinned to the nRF52840 DK.

---

## 3. Compatibility & wiring notes (read before powering)

### 3.1 BME280: the project hard‑codes I²C address **0x76**

`firmware/boards/nrf52840dk_nrf52840.overlay` declares `bme280@76 { reg = <0x76>; }`
on `&i2c0`. Breakouts differ in their factory address:

- **Pimoroni PIM472 (recommended)**: **factory 0x76 — matches the overlay, no mod.**
  The ADDR trace is cut only if you want 0x77. Includes male + right‑angle female
  headers (you solder one 1×5 strip). 3.3 V or 5 V; onboard I²C pull‑ups.
- **Adafruit PID 2652**: factory **0x77**. To get 0x76, "add a jumper from SDO to GND
  with a wire, or solder the ADDR jumper on the back closed" (Adafruit learn guide).
- **SparkFun BME280 (Qwiic)**: factory **0x77**; the back‑side **ADR** jumper selects
  0x76 by cutting the trace/bridging the '0' side. **3.3 V only** — never feed 5 V.
- **No‑solder alternative if you buy a 0x77 board**: change the devicetree overlay
  `reg = <0x77>` (one line) and rebuild — functionally identical, but it diverges from
  the committed 0x76 bench/HIL expectations. Prefer the Pimoroni board for fidelity.
- **Bus sharing**: the SHT45 sits at fixed 0x44, so BME280@0x76 and SHT45@0x44 can share
  the same `&i2c0` bus simultaneously (useful for the reference comparison).
- Wiring (DK side): `&i2c0` is pinned to **SDA = P0.26, SCL = P0.27** in Zephyr v4.1.0's
  `nrf52840dk_nrf52840-pinctrl.dtsi`. These are the DK's Arduino‑header **SDA/SCL** pins
  (Zephyr's connector map calls them D14/D15) — **not** A4/A5 (A4 = P0.30, A5 = P0.31,
  analog‑only per the DK user guide). Also connect **3V3** and **GND**.
- The DK's Arduino headers are **sockets**, so use **male‑to‑female** dupont jumpers
  (male end into the DK, female end onto the breakout's soldered male header), or use
  the DK's through‑hole **P13–P17** male pins with F‑F jumpers. Solder the breakout's
  header first; a soldering iron or a makerspace visit is the simplest route.
- The DK's Arduino header has shield‑detect switches for I²C pull‑ups; all three
  breakouts carry their own pull‑ups, so no external resistors are needed (Zephyr's
  `i2c0_default` intentionally has no internal `bias-pull-up`).

### 3.2 W5500 Ethernet shield: use the upstream Zephyr `seeed_w5500` path

- Zephyr v4.1.0 has `wiznet,w5500` devicetree binding + `CONFIG_ETH_W5500` driver and a
  **`seeed_w5500` shield** whose documentation literally builds
  `west build -b nrf52840dk/nrf52840 --shield seeed_w5500 samples/net/dhcpv4_client`.
- Pin mapping is the standard Arduino SPI header: **D10 CS, D11 MOSI, D12 MISO,
  D13 SCK, D2 INT** on `&arduino_spi` (= `spi3`, P1.13–P1.15). This does **not** collide
  with the BME280's `i2c0` pins P0.26/P0.27.
- The Seeed shield is **3.3 V operation with 5 V‑tolerant I/O**; the DK's Arduino header
  supplies 5 V/3.3 V/GND. Check the shield's regulator/level‑shifter rails before first
  power‑up (also confirm it isn't an old W5100 board — buy the W5500 SKU).
- This is a **firmware change**, not only a purchase: add `--shield seeed_w5500` to the
  HIL build, override the native_sim TEST‑NET addresses
  (`CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.0.2.1"`/peer `192.0.2.2` in `firmware/prj.conf`),
  restore a real entropy source (`CONFIG_TEST_RANDOM_GENERATOR=n` is set for the board),
  and choose DHCP or a static LAN IP. Expect ~25–40 lines of conf/overlay. The
  driver's RX thread defaults are small (`CONFIG_ETH_W5500_RX_THREAD_STACK_SIZE=800`);
  keep an eye on RAM (256 KB total, mbedTLS heap already 30 KB, main stack 16 KB).
- **Zero‑BOM alternative** if the shield is out of stock: USB CDC‑ECM gadget
  (`CONFIG_USB_DEVICE_STACK=y` + `CONFIG_USB_DEVICE_NETWORK_ECM=y`) creates a `usb0`
  interface on the host with no extra hardware — but it is not upstream‑tested on this
  board combination, so treat it as a 1‑hour spike, not the demo path. (`CONFIG_NET_PPP`
  does **not** exist in v4.1; the real option is the L2‑only `CONFIG_NET_L2_PPP`, which
  needs a serial transport you'd implement — harder.)

### 3.3 PPK2: how it attaches to the DK

- The DK user guide (§5 "Current measurement") lists supported instruments:
  oscilloscope, ampere meter, **PPK2**, power analyzer. **P‑22** is the VDD current
  measurement connector (P‑23 for VDDH). To insert the meter, **cut solder bridge
  SB40** (SB41 for VDDH); to restore normal kit function, re‑solder or put a jumper on
  P‑22. The PPK2 kit includes jumper cables; its user guide covers the DK hookup.
- Do **not** power the DK from USB during current measurements (USB supply noise), and
  note that a serial terminal on the virtual COM port makes measurements unreliable.
  Power the DK from a coin cell, the P‑21 external supply (1.7–3.6 V), Li‑Po, or P‑27
  while profiling. Reprogramming requires removing the measurement device and
  reconnecting USB.
- The PPK2 can **source** 0.8–5 V (up to 1 A with two USB cables) or act as an
  **ampere meter** with an external supply. Its micro‑USB cable is **not included**.
  The bench‑low‑friction hookup is **source (SMU) mode**: PPK2 `VOUT` → the P‑22 pin on
  the SoC side, PPK2 `GND` → DK `GND`, source set to **3.3 V**; verify the exact
  `VOUT`/`GND` silkscreen and polarity against the PPK2 user guide figure at first use.
  (Or use ampere‑meter mode with a coin‑cell/P‑21 supply in series with P‑22.)
- **Do this in two passes** — serial verdicts and accurate µA measurement conflict on
  the DK: first flash and run `tests/hil/run_tests.sh -t /dev/ttyACM0` with USB/serial
  attached, then disconnect USB, prepare the measurement (SB40/P‑22 + PPK2 source),
  and record the current trace in nRF Connect for Desktop → Power Profiler. Use the
  PPK2's 8 digital inputs (wired to a spare DK GPIO) if you want to correlate firmware
  phases with current without serial output.
- Firmware caveat: `firmware/prj.conf` notes `CONFIG_PM` is compile‑gated on this target,
  so the "sleep" windows are scheduler‑level only. If the PPK2 shows no µA‑level sleep,
  the next step is enabling Zephyr power management/tickless — a firmware task, not a
  reason to buy different hardware.
- The SB40 cut is a real bench modification: it is restorable with solder or a jumper on
  P‑22, but if the DK must stay pristine, order a second DK (≈ CAD 79) for the power work.

### 3.4 Cables and ports

- The nRF52840 DK has **two micro‑USB ports**: one for the on‑board J‑Link (flashing +
  virtual COM port) and one routed to the nRF52840's USB device controller. The HIL
  scripts use the J‑Link CDC port (`/dev/ttyACM0` on Linux; add your user to `dialout`).
- Buy **data** cables; label them. A known‑good short cable + powered USB hub reduces
  bring‑up pain (the DK + PPK2 + debugger can draw a lot from bus‑powered hubs).

### 3.5 Work you must do *after* the hardware arrives (not purchasable)

- Regenerate `infra/tls` with a SAN covering the broker hostname/IP you actually use
  (the committed `server.ext` covers only `mosquitto`, `localhost`, `broker`,
  `127.0.0.1`, `192.0.2.2`), regenerate `firmware/src/telemetry/telemetry_ca_cert.h`
  via `infra/tls/generate.sh`, and rebuild — the firmware **pins the CA in code**.
- Set `CONFIG_MQTT_BROKER_HOST` and `CONFIG_MQTT_TLS_SERVER_NAME` for your LAN/VPS.
- Replace the **MCUboot default debug signing key** (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`)
  before trusting OTA, and rotate the dev MQTT `change-me` passwords for any shared host.
- Add the `seeed_w5500` shield configuration described in §3.2.

### 3.6 How to actually validate the BME280 against the reference

- **Temperature / humidity:** put the BME280 and SHT45 **side by side in still air**, at
  the same height, away from the DK's regulator/USB heat and out of sunlight/drafts
  (open‑bench gradients alone can exceed 1 °C). Let both run **≥20 min** (the BME280
  needs burn‑in; readings stabilise over a few minutes). Log at 1 Hz for 30–60 min and
  compare 1‑minute averages: `ΔT = BME280 − SHT45`, `ΔRH = BME280 − SHT45`.
  Accept when **median |ΔT| ≤ 0.5 °C and median |ΔRH| ≤ 3 %RH with no systematic
  drift**; the SHT45 (±0.1 °C, ±1.0 %RH, ISO‑17025 calibration certificate) is inside
  the window, so any excess delta is attributable to the BME280/unit/environment.
  Remember the BME280 datasheet accuracy is ±1.0 °C — the roadmap numbers are
  *comparison tolerances*, not a guaranteed sensor spec.
- **Pressure (±1 hPa) is the hard one:** absolute pressure needs a *same‑height,
  traceable* reference. Environment and Climate Change Canada station pressure for
  Toronto is rounded to 0.1 kPa (= 1 hPa) — the same size as the tolerance — so it can
  only support a "not obviously wrong / within combined uncertainty" verdict, not a tight
  ±1 hPa proof. At Toronto's elevation ~1 hPa ≈ 8–9 m of height difference, so the
  sensor and reference must be within a few metres vertically. Practical bench verdict:
  24‑h trend/noise comparison against a station plus a second BME280 for repeatability,
  and **document the tight‑±1 hPa limitation as residual uncertainty** (a calibrated
  barometer or barometric chamber is out of scope for this budget).

---

## 4. Where & how to buy (Toronto / Canada)

### 4.1 Channel comparison

| Channel | Best for | Currency & tax | Shipping to Toronto | Notes |
|---------|----------|----------------|---------------------|-------|
| **Digi‑Key Canada** ([digikey.ca](https://www.digikey.ca/)) | Nordic DK, PPK2, dongle, Adafruit/SparkFun parts, passives, connectors | **CAD**, HST at checkout; **DDP** (duty/customs paid by Digi‑Key) | **Free delivery ≥ CAD $100** (≈ CAD $15 below); typically ~24 h to Canada | One‑stop for the Tier‑0/2 core. Search by part number (`NRF52840‑DK`, `NRF‑PPK2`). Marketplace items don't count toward the threshold. |
| **Mouser Canada** ([mouser.ca](https://www.mouser.ca/)) | Same distributor alternative | CAD, tax at checkout; DDP on CAD pricing | Free shipping on most orders **> CAD $100** *(snapshot; live site CAPTCHA)* | Useful for stock/price comparison. |
| **Newark / element14 Canada** ([canada.newark.com](https://canada.newark.com/)) | Alternative distributor | CAD; **air shipments include brokerage, duties, taxes** | Free delivery **≥ CAD $200** ($11.99 below); 2–3 business days from US stock | Good backup if Digi‑Key is backordered. |
| **Future Electronics** ([futureelectronics.com](https://www.futureelectronics.com/)) | Nordic parts with a Canadian branch network + FAE support | CAD, commercial terms | Ships from Canada/US; local branches (Montreal HQ) | Strongest route for **loaner/eval kit** requests and technical FAE help (§4.5); Canada free‑shipping threshold UNVERIFIED. |
| **RS / Arrow / Avnet** | Business purchasing, larger orders | CAD/USD, varies | Varies | Overkill for a one‑off personal bench; RS Canada terms UNVERIFIED (site blocked). |
| **PiShop.ca** ([pishop.ca](https://www.pishop.ca/)) | Raspberry Pi, Adafruit/SparkFun/**Pimoroni** breakouts, W5500 shield, cables | **CAD**, HST at checkout | **Canada‑only**; weight‑based Canada Post/UPS from Ottawa; same/next business day; pickup **Ottawa only** | Verified prices: **Pimoroni BME280 CAD 20.95**, Adafruit BME280 CAD 23.95, Arduino Ethernet Shield 2 (W5500) CAD 41.95, Pi 5 4 GB CAD 156.85 / 8 GB CAD 249.50, 27 W PSU CAD 16.95, case CAD 14.45, microSD CAD 27.95. BuyaPi.ca redirects here. |
| **Canada Robotix** ([canadarobotix.com](https://www.canadarobotix.com/)) | Adafruit/SparkFun/Raspberry Pi brands | CAD | Ships from Canadian + US warehouses; **Markham office is mail‑order only — no pickup** | Good fallback for BME280/SHT4x/breakouts; free‑shipping threshold UNVERIFIED. |
| **Creatron Inc.** (349 College St, Toronto · 647‑349‑9258) | Local walk‑in: Adafruit/SparkFun parts, solder, headers, wire, tools | CAD | **Local pickup** (M–F 11–6, Sat 11–5, Sun closed) | Best for "I need it today" items. **Warning:** `creatroninc.com` was serving gambling spam on 2026‑11‑10 — phone/walk in, do not enter payment details on that domain. |
| **Sayal Electronics** ([sayal.com](https://www.sayal.com/)) | Local walk‑in: components, cable, tools, PSUs, breadboards | CAD | **5 GTA stores** (Scarborough 3791 Victoria Park; Vaughan 7500 Keele St; Mississauga; Pickering; Markham); **free shipping ≥ CAD $50** online | Best same‑day consumables (jumpers, headers, micro‑USB data cables, multimeter, soldering). |
| **Adafruit / SparkFun / Seeed direct** (US/CN) | Parts not stocked in Canada (SHT45; the verified Seeed shield) | USD; HST + possible duty/brokerage | Adafruit: **DHL Express 1–3 days (may be DDP) or USPS (slow; Canada Post $9.95 handling when tax owed)**; Seeed direct | Consolidate into one small order; prefer DDP shipping. |
| **US parcel forwarders** (Shippsy, CrossBorder Pickups) | US retailer that only ships USPS/ground with no DDP | USD purchase; forwarder bills HST + flat fee | Shippsy **CAD 10.99 + tax**/package; CrossBorder Pickups **CAD 11.00** regular; GTA pickup available | Cheaper than UPS Standard brokerage; use for one consolidated US order. |
| **Amazon.ca** | Cables, jumpers, PSUs, tools, microSD | CAD | 1–2 days | Convenient but **counterfeit risk** for sensors/ICs/PSUs; avoid unbranded "BME280" modules for the reference measurement. |
| **AliExpress** | Cheap generic modules (GY‑BME280, W5500 modules, jumper kits, logic‑analyzer clones) | USD | 2–6 weeks, no DDP | Fine for spares/tools; not for the calibrated reference or the demo‑critical shield. |
| **eBay / Kijiji / Facebook Marketplace** | Used Raspberry Pi, mini PC, used DK | CAD | Local pickup / shipped | Great for the Tier‑3 gateway on a budget; test before paying, no warranty. |

### 4.2 Best channel per item (recommended order plan)

| Item | Buy at | Why |
|------|--------|-----|
| NRF52840‑DK + NRF‑PPK2 (+ optional dongle) | **Digi‑Key Canada**, one order | CAD invoice, **DDP**, ~24 h; DK + PPK2 = CAD 215.88 clears the **CAD $100 free‑shipping** threshold and pays one HST charge. A DK‑only order (CAD 79.38) pays ≈ CAD 15 shipping — add the PPK2 now or a cheap filler. |
| BME280 | **PiShop.ca** — Pimoroni PIM472 (CAD 20.95, factory 0x76, headers included) | Canadian stock, no import math, and **no address‑jumper mod** for the repo's overlay. |
| SHT45 reference | **Adafruit** small order (USD 12.50; choose **DHL DDP**), or **Canada Robotix** if stocked | Not stocked at PiShop; goods under CAD $40 → HST relief, but shipping often dominates a tiny order, so combine it with anything else you need from Adafruit. |
| W5500 Ethernet shield | **Seeed Studio** SKU 103030021 (verified path) — or **PiShop.ca** Arduino Ethernet Shield 2 (CAD 41.95) if you can verify the INT wiring | Seeed is the exact shield Zephyr documents; the Arduino Shield 2 is W5500‑based and domestic but its INT/level‑shifting for the DK is unverified (§3.2). |
| Raspberry Pi 5 + PSU + case + microSD | **PiShop.ca** | CAD, Canadian stock; avoid US import fees on the Pi. 4 GB is enough; 8 GB only if you want desktop use. |
| Cables, headers, jumpers, solder, multimeter | **Sayal / Creatron** (GTA pickup) or Amazon.ca | Same/next‑day, cheap; shipping on a CAD 3 part is otherwise a loss. |
| Domain / VPS | Registrar + Hetzner/Fly.io | Account‑only; no hardware |
| Soldering iron / logic analyzer / oscilloscope | Sayal, or **borrow** at a makerspace/lab | Avoid buying if you'll use it once. |

**Consolidation:** the near‑optimal basket is **two domestic orders + one small US order**:
Digi‑Key Canada (Nordic, DDP) + PiShop.ca (sensor, Pi kit, cables) + Adafruit (SHT45 only,
if needed). Seeed direct is a third order only if you want the verified shield; the
PiShop W5500 shield avoids it at the cost of a pinout check.

### 4.3 Landed‑cost math for a Toronto buyer

- **FX:** Bank of Canada USD/CAD = **1.4123** (2026‑11‑09): `CAD ≈ USD × 1.4123`, plus a
  ~2–3 % card FX spread on USD charges.
- **Ontario HST:** **13 %**, charged at checkout by Canadian sellers (Digi‑Key/PiShop/
  Mouser) or collected by the carrier on imports.
- **Courier import rules (US/Mexico goods):** under the *Courier Imports Remission
  Order* s. 4.1, for courier shipments from the US/Mexico, **duty is remitted at value
  for duty ≤ CAD $150** and **GST/HST is remitted at ≤ CAD $40**. Practical result:
  goods ≤ CAD $40 → no HST; CAD $40–150 → **13 % HST, no duty**; > CAD $150 → HST plus
  duty unless the goods qualify under CUSMA (many electronics are MFN‑free anyway).
  Non‑US courier goods (e.g. AliExpress) get much narrower relief — assume HST above
  ~CAD $40. Mail has a low‑value exemption (commonly CAD $20) plus a **CAD $9.95**
  Canada Post handling fee when tax is owed *(current‑rule check recommended)*.
- **DDP vs DAP is the real cost driver.** DDP (Digi‑Key, Mouser CAD, Newark air,
  Adafruit's DHL option) = tax at checkout, **no brokerage at the door**. DAP
  (USPS/FedEx Ground/UPS Standard) = carrier invoices HST **plus** brokerage:
  Canada Post CAD 9.95; FedEx ≈ 3.1 % (min CAD $12); DHL ≈ 2.75 % (min CAD $18; min $12
  with account); **UPS Standard is the trap** — value‑based entry fees (e.g. CAD 70.25
  for CAD 200–350) + 3.7 % disbursement (min CAD 7.75).
- **Forwarders** (Shippsy ≈ CAD 10.99 + tax/package; CrossBorder Pickups ≈ CAD 11
  regular) replace courier brokerage with a flat fee and beat non‑DDP courier for a
  consolidated US order.
- **Worked example — USD $160 US order** (≈ CAD 225.97 goods, HST CAD 29.38): forwarder
  ≈ **CAD 266–268**, DHL‑DDP ≈ **CAD 277**, UPS Express ≈ **CAD 289**, USPS ≈ **CAD 301**,
  UPS Standard ≈ **CAD 355**. **Buying the Nordic items from Digi‑Key Canada in CAD and
  the Pi kit/sensors from PiShop.ca means zero import math** — reserve US‑direct for the
  SHT45 or a niche module.
- **Project‑specific example:** Adafruit SHT45 alone (USD 12.50 ≈ CAD 17.66) is under
  the CAD $40 HST relief line, so the landed cost is dominated by shipping; if you order
  it solo, use a DDP method and accept ≈ CAD 35–45 all‑in, or check Canada Robotix first.

### 4.4 Suggested ordering sequence

1. **Day 1 — Digi‑Key Canada:** cart with `NRF52840-DK` + `NRF-PPK2` (+ optional
   `NRF52840-DONGLE`). This clears the CAD $100 free‑shipping threshold; DDP means HST
   at checkout and no brokerage; delivery to Toronto is typically ~24 h. Re‑check the
   live CAD price at checkout (the CAD figures here are 2025 snapshots).
2. **Day 1 — PiShop.ca:** Pimoroni BME280 (CAD 20.95), Pi 5 4 GB kit if going Tier 3
   (PSU CAD 16.95, case CAD 14.45, microSD CAD 27.95), 40‑pin jumper bundles
   (CAD 2.75–4.45), micro‑USB **data** cables (CAD 2.95–10.15). Weight‑based Canada
   Post/UPS shipping; combine everything into one order to pay one shipping charge.
3. **Day 1 — transport shield:** either add the **Arduino Ethernet Shield 2**
   (CAD 41.95) to the PiShop order (verify INT wiring per §3.2 first), or order the
   **Seeed W5500 shield** (USD 35.00) direct and let it ship while you do Tier 0/1 work.
4. **Day 1–2 — Adafruit (only if you want the SHT45):** small order, choose **DHL** (the
   DDP option is the reason); combine with any other Adafruit-only part to make the
   shipping worthwhile, or check Canada Robotix first.
5. **Day 2 — local same‑day:** Sayal (5 GTA stores) or Creatron (349 College St; **call
   first** — the website was compromised) for solder, headers, a multimeter, a Cat‑5e
   cable, or an emergency micro‑USB data cable.
6. **When you're ready to deploy remotely:** register a domain, create a Hetzner/Fly.io
   account, set up Let's Encrypt — no hardware purchase.

### 4.5 Local, community, and loaner options (Toronto)

- **Walk‑in / pickup:** **Sayal Electronics** has 5 GTA stores (Scarborough — 3791
  Victoria Park Ave; Vaughan — 7500 Keele St; Mississauga; Pickering; Markham) and free
  shipping ≥ CAD $50 online: best for same‑day consumables. **Creatron Inc.** (349
  College St; M–F 11–6, Sat 11–5; 647‑349‑9258) carries Adafruit/SparkFun parts and
  tools — **but its website was serving gambling spam on 2026‑11‑10**, so phone or walk
  in and never enter payment data there. **Canada Robotix** (Markham) is mail‑order only
  (no pickup). PiShop pickup is Ottawa‑only; Digi‑Key/Mouser/Newark have no Canadian
  counters.
- **Makerspaces:** **Site 3 coLaboratory** (CAD $60/mo + HST, first+last ≈ $120 at
  signup) and **Hacklab.TO** (CAD $75/mo, 3‑day onboarding) have soldering stations and
  often scopes/logic analyzers; a guest visit or a month membership is cheaper than
  buying test gear for one session. Toronto Tool Library is more general tools than
  electronics (membership fee UNVERIFIED).
- **University/lab route:** if you have a U of T / TMU / York affiliation, a lab
  Digi‑Key/Mouser account (PO, consolidated billing) and a borrowed DK/PPK2 from a
  teaching lab are the cheapest realistic paths to the power‑measurement step. A
  student collaborator can often borrow a spare DK for a term.
- **Vendor eval/loaner path (realistic version):** Nordic does not run a public
  individual loaner program — dev kits flow through distributors/sales. The workable
  route is a **Future Electronics FAE** (Montreal HQ, Canadian branches) with a
  one‑paragraph project brief, timeline and any volume story; as an individual expect a
  polite "no", as a company/university account a short demo loan is normal. Otherwise:
  used DK on Kijiji/eBay (≈ CAD 40–70, verify it enumerates as a J‑Link and can flash a
  test image), or borrow from a local Zephyr/embedded meetup. **PPK2 is rarely loaned**;
  a PPK1 or a bench supply + shunt + DMM can cover mA‑level checks, but µA sleep numbers
  need the PPK2‑class instrument.

### 4.6 How to place the orders

1. **Accounts & currency:** create `digikey.ca` (set **CAD**), `pishop.ca`, and
   optionally `mouser.ca` / `canada.newark.com` / `adafruit.com`. As an individual you
   do not need a GST/HST number; a university/business account can add one for input
   tax credits. Expect a ~2–3 % card FX fee on USD charges.
2. **Order 1 — Digi‑Key Canada:** nRF52840 DK CAD 79.38 + PPK2 CAD 136.50 (+ dongle
   CAD 15.34) = **CAD 231.22**, free shipping (≥ CAD 100), DDP. Verify the live CAD
   price at checkout. Watch for split shipments/Marketplace items, which can drop you
   below the free‑shipping threshold — add an in‑stock filler (headers/jumpers) if so.
3. **Order 2 — PiShop.ca:** sensors + Pi kit + consumables in one cart; shipping is
   weight‑based (exact rate at cart), Canada‑only, signature required over CAD $300 —
   combine to pay one shipping charge.
4. **Order 3 (optional) — Adafruit:** SHT45 USD 12.50 (plus any Adafruit‑only extras);
   pick DHL (DDP option) for tax at checkout and 1–3 day delivery; USPS is cheaper up
   front but slow and adds Canada Post's CAD 9.95 handling when tax is owed.
5. **Local same‑day:** Sayal/Creatron for solder, headers, jumpers, cables, multimeter —
   call Creatron first.
6. **US retailer without DDP (SparkFun etc.):** consolidate into one order and use
   Shippsy (~CAD 10.99 + tax/package) or CrossBorder Pickups (~CAD 11) rather than
   UPS Standard.
7. **Payment:** credit cards are accepted everywhere above; PayPal at several retailers;
   foreign‑currency charges may add a card FX fee.
8. **Address & backorders:** use a street address (couriers; PiShop cancels unapproved
   freight‑forwarder addresses). If the PPK2 or a Pi is backordered, order the rest
   first, set a stock alert, and don't block bring‑up on the missing item.
9. **Keep the paperwork:** invoices/packing slips for warranty and tax credits; if a
   courier bills brokerage unexpectedly, request the commercial invoice and the
   seller's DDP terms before paying.

### 4.7 Cost summary by tier (CAD, landed estimates)

Assumes Digi‑Key order ≥ CAD $100 (free shipping), PiShop weight‑based shipping
≈ CAD $15–20 for a mixed basket, 13 % HST at checkout, and no US import except an
optional SHT45. These are **estimates**; re‑check at checkout.

| Tier | Goods (main items) | Est. landed CAD |
|------|--------------------|-----------------|
| 0 — touch silicon | DK 79.38 + 2 USB data cables ≈ 12 | **≈ 105–120** (Digi‑Key under‑$100 shipping if DK alone ≈ +15) |
| 1 — + sensor + live IP | BME280 20.95 + SHT45 ≈ 18 + W5500 shield 41.95 (PiShop) + jumpers/cable ≈ 18 | **≈ 235–260** |
| 2 — + PPK2 | PPK2 136.50 + spare micro‑USB cable ≈ 5 | **≈ 385–410** |
| 3 — + deploy/CI host | Pi 5 4 GB 156.85 + PSU 16.95 + case 14.45 + microSD 27.95 (+ PiShop shipping) | **≈ 640–690** one‑time; optional UPS/relay +25–90; VPS ≈ 9–12/mo |
| *cross‑check* | fuller basket (Pi 5 8 GB, NVMe HAT, extra cables/spares) gathered by the research pass | **≈ 780–830** all‑in |

A used x86 mini PC (≈ CAD 150–300) instead of the Pi, and reuse of an existing PC as the
broker host, are both valid ways to cut Tier 3.

---

## 5. Deployment hardware (beyond the bench)

| Option | Hardware | Cost | When to use |
|--------|----------|------|-------------|
| **Bench‑only (start here)** | your existing Linux PC + DK + W5500 shield + LAN | **CAD 0 extra** | Validates the full firmware→broker→cloud→Grafana path locally; `just demo`/`just e2e` already do the software half. |
| **Always‑on local deploy** | Raspberry Pi 5 4 GB kit (§2.4) on the LAN | ≈ CAD 250–350 one‑time | A 24/7 "edge gateway + broker + observability" box, and the host for HIL CI. |
| **Remote deploy** | small VPS (Hetzner/Fly.io) + domain + Let's Encrypt | ≈ CAD 9–12/mo + ≈ CAD 15/yr | Put the same compose stack on the public internet; firmware connects over TLS 8883 with a cert whose SAN matches `CONFIG_MQTT_TLS_SERVER_NAME`. |
| **Managed MQTT (later)** | EMQX Serverless free tier (or HiveMQ/AWS IoT) | $0–modest | Deletes broker ops but changes firmware auth/credential plumbing; not needed for the first end‑to‑end deploy. |
| **HIL CI** | self‑hosted GitHub Actions runner on the gateway + DK on USB (+ relay for power‑cycling) | included in Tier 3 | **Required** for automated hardware tests: GitHub‑hosted runners are ephemeral cloud VMs with no USB attach path, so the DK can never enumerate there. |

The device path is identical for all of them — only the broker hostname, CA/cert and
credentials change. Budget a firmware rebuild for each new TLS identity because the CA
is embedded.

---

## 6. After the boxes arrive — end‑to‑end runbook

Everything below is already scripted in the repo; this is the order to run it in, and
what each step proves.

| # | Step | Command / action | Hardware used | Pass criterion |
|---|------|------------------|---------------|----------------|
| 0 | Host baseline | `devenv shell` then `just build`, `just test`, `just demo` | none | 53 ztests pass; Grafana shows fleet telemetry; `cloud-ingest /health` is 200 |
| 1 | Cross‑build for hardware | `export ZEPHYR_SDK_INSTALL_DIR=…` (Zephyr SDK download — free, no hardware) then `just build-hil` | none | signed nRF52840 image + MCUboot; `imgtool verify` OK |
| 2 | Flash + HIL ztests | `tests/hil/run_tests.sh -t /dev/ttyACM0` (or `tests/hil/flash.sh --sysbuild`) | **DK + USB cable** | `sensor_log power_sched telemetry fault_inject` report `PROJECT EXECUTION SUCCESSFUL` on serial |
| 3 | Real sensor | wire BME280 per §3.1; flash `firmware`; read the sensor log/telemetry; run the §3.6 method | **DK + BME280 + jumpers (+ SHT45)** | temp/humidity: median \|ΔT\| ≤ 0.5 °C and \|ΔRH\| ≤ 3 %RH vs SHT45 with no drift; pressure: 24‑h trend consistent with station pressure (tight ±1 hPa not provable — see §3.6) |
| 4 | Power profile | prepare the DK (SB40 cut, P‑22), connect PPK2 in source mode, run the `power_sched` workload **after** a serial‑verdict pass | **DK + PPK2** | measurable ACTIVE/IDLE/SLEEP current profile; µA‑range sleep recorded (or documented firmware follow‑up) |
| 5 | MCUboot real swap | `./flash.sh --sysbuild`, publish frames with `tests/ota-sim`, reboot | **DK + USB + host** | swap‑move completes; new image boots; deliberately invalid signature is rejected |
| 6 | Link up | rebuild with `--shield seeed_w5500` + LAN IP config; `net iface`/ping (net‑shell) or DHCP log | **DK + W5500 + cable** | Ethernet interface up, IP acquired/assigned, broker host reachable |
| 7 | Live MQTT/TLS/OTA | regenerate TLS for your broker hostname, rebuild, flash; `infra/docker-compose.yml` on the LAN host | **DK + shield + host** | device publishes CBOR to `telemetry/<id>`; `cloud-ingest` `/metrics` increments; Grafana updates; broker logs TLS 1.2+ |
| 8 | Fleet (optional) | add the second DK/dongle; point the fleet‑sim + devices at the same broker | **2nd board** | multiple devices visible per‑device in Grafana |
| 9 | HIL CI | install a self‑hosted runner on the gateway, label it `nrf52840dk`, add a flash→serial→OTA job | **DK + gateway + relay** | workflow green; logs show serial PASS; failures fail the job |
| 10 | Remote deploy | copy `infra/` to a VPS (or run it on the Pi), DNS + Let's Encrypt, update device CA/host, flash | **VPS/Pi** | device publishes to the remote broker over TLS; Grafana reachable over HTTPS; remote OTA completes |

Notes: steps 0–1 need **no purchased hardware**; steps 2–5 need the DK; 3 adds the
BME280; 4 adds the PPK2; 6–7 add the W5500 shield; 9–10 add the gateway/VPS. Steps 0 and
the TAP variant of 7 (`just firmware-tap`) can run on `native_sim` today but need
`/dev/net/tun` + `CAP_NET_ADMIN`.

---

## 7. Risks, compliance, and money‑saving notes

**Sourcing risks**
- **Counterfeit/relabelled sensors:** cheap "BME280" modules may be BMP280 (no humidity)
  or fake. Read the chip‑ID register / compare humidity against a reference. Buy the
  demo/reference parts from Adafruit/SparkFun/PiShop/Digi‑Key.
- **W5100 vs W5500:** only W5500 has a Zephyr v4.1 driver. Verify the shield SKU before
  buying.
- **Charge‑only USB cables and weak PSUs:** cause most "dead board" reports.
- **Stock/price volatility (2026):** DRAM/memory‑market inflation has pushed Pi prices up
  and thinned 1/2 GB and Zero 2 W stock; PPK2 and some Adafruit parts go in/out of
  stock. Order early, set stock alerts, don't block bring‑up on the PPK2.
- **Import/brokerage:** prefer DDP Canadian channels (Digi‑Key/Mouser/Newark air,
  Adafruit DHL); avoid **UPS Standard** on US parcels (entry fee up to CAD 70 + 3.7 %
  disbursement). For a consolidated US order, a forwarder (Shippsy/CrossBorder
  Pickups) beats non‑DDP courier.
- **Creatron website compromise (2026‑11‑10):** `creatroninc.com` was serving gambling
  spam — phone/walk in, don't trust the domain for payment.
- **Marketplace fakes:** fake microSD capacity, no‑name USB‑C PSUs (brown‑outs), and
  "BME280" modules that are BMP280. Buy PSU/SD from PiShop and cables from Sayal; label
  your data cables.

**Technical risks**
- The W5500 shield is a **firmware‑integration task** (config/overlay + TLS identity
  work), not a plug‑and‑play demo: budget 1–2 days including the TLS/cert rebuild.
- Electrical stacking of the Seeed shield on the DK was not measured here; verify the
  shield's 3.3 V rail and level shifters before connecting the DK to Ethernet.
- `CONFIG_PM`/tickless may need enabling before the PPK2 shows µA‑level sleep — firmware
  work, not a hardware shortfall.
- **±1 hPa pressure cannot be tightly proven** against Environment Canada station
  pressure (0.1 kPa resolution = 1 hPa = the tolerance itself) — plan for a documented
  residual uncertainty or a calibrated barometer/barometric chamber if a hard claim is
  required (§3.6).
- **SB40 must be cut** on the DK to insert the PPK2 in series with P‑22; it is
  restorable (solder/jumper), but if the board must stay pristine, buy a second DK for
  the power work.
- The committed dev CA/certs and `change-me` MQTT passwords, and MCUboot's default
  debug signing key, must be replaced before any shared/remote deployment.

**Compliance**
- A **wired** bench/demo (DK + W5500 shield) needs **no radio certification** — it is a
  development kit plus a wired shield.
- The moment you ship a product built around the nRF52840 **radio** (Thread/BLE/Wi‑Fi or
  a custom board), the end product needs **FCC Part 15** (US) and **ISED RSS** (Canada)
  certification; dev kits/modules are for evaluation and modular grants only transfer
  under their conditions. Budget a test house before any field pilot.

**Money savers**
- Reuse the existing PC, switch/router, monitor, cables, and a spare microSD — the Pi is
  only for 24/7 + HIL CI, not for the first validation.
- Two domestic orders (Digi‑Key Canada + PiShop.ca) get the DK, PPK2, sensor, Pi kit and
  consumables with **zero import math**; add Adafruit only for the SHT45.
- Buy the BME280/PiShield/Pi kit from PiShop in CAD rather than US‑direct; use a
  forwarder only if a US retailer has no DDP option.
- Borrow a soldering station/logic analyzer/multimeter at a makerspace or lab; the PPK2
  is the only instrument worth buying outright for this project (a PPK1/shunt+DMM is a
  budget fallback for mA‑level checks).
- If the full budget is tight: Tier 0 + the W5500 shield + BME280 still closes most of
  the ROADMAP; add the PPK2 and Pi later.

---

## 8. Sources & price‑check log

Checked **2026‑11‑10** (all seller pages, plus Internet Archive snapshots where live
pages block bots). Re‑check prices/stock before ordering. Raw per‑item evidence notes
from the research pass are kept in `research/bench.md` (bench/instruments),
`research/deploy-net.md` (transport/gateway/deployment) and
`research/canada-vendors.md` (Canadian channels/landed cost).

**Nordic (Digi‑Key Canada CAD snapshots; live EUR cross‑check)**
- nRF52840 DK, `NRF52840-DK`: **CAD 79.38** — <https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/8593726>
  *(price from archive snapshot 2025‑10‑13; page InStock then; live cross‑check **€43.45 excl. VAT**, 412 in stock, [Digi‑Key LU](https://www.digikey.lu/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/1490-1072-ND/8593726))*
- Power Profiler Kit II, `NRF-PPK2`: **CAD 136.50** — <https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF-PPK2/13557476>
  *(snapshot 2025‑08‑08; InStock then; live cross‑check **€95.25**, Digi‑Key LU)*
- nRF52840 Dongle, `NRF52840-DONGLE`: **CAD 15.34** *(snapshot 2025‑08‑08)* — <https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF52840-DONGLE/9491124>
- nRF52840 DK product overview (on‑board J‑Link, Arduino R3, current‑measurement pins,
  "interfaces directly with the PPK2"): <https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK>
- nRF52840 DK user guide §5 current measurement (P‑22/P‑23, SB40/SB41, PPK2, USB‑noise
  and serial‑terminal caveats): <https://docs.nordicsemi.com/bundle/ug_nrf52840_dk/page/UG/dk/intro.html>
- PPK2 product overview (200 nA–1 A, 100 kS/s, AMP/SMU modes, box has no USB cable):
  <https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2>

**Adafruit / SparkFun / Pimoroni / Seeed (USD/GBP)**
- Adafruit BME280 PID 2652: **$14.95**, 0x77 default, ADDR jumper → 0x76 —
  <https://www.adafruit.com/product/2652> and
  <https://learn.adafruit.com/adafruit-bme280-humidity-barometric-pressure-temperature-sensor-breakout/pinouts>
- **Pimoroni BME280 PIM472: £11.50, factory 0x76** (cut trace for 0x77), headers
  included — <https://shop.pimoroni.com/products/bme280-breakout>
- SparkFun BME280 (Qwiic) **SEN‑15440: $16.95**, 0x77 default/0x76 selectable,
  **3.3 V only** — <https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280-qwiic.html>;
  ADR jumper documented at <https://learn.sparkfun.com/tutorials/sparkfun-bme280-breakout-hookup-guide/hardware-overview>
- **Nordic PPK2 via Adafruit PID 5048: $99.95 in stock** —
  <https://www.adafruit.com/product/5048>
- Adafruit SHT45 PID 5665: **$12.50**, ±0.1 °C/±1.0 %RH, ISO‑17025 calibration —
  <https://www.adafruit.com/product/5665>
- Adafruit SHT40 / SHT41 PID 4885 / 5776: **$5.95**, cheaper temp/RH references —
  <https://www.adafruit.com/product/4885>, <https://www.adafruit.com/product/5776>
- STEMMA QT/Qwiic to male headers cable PID 4209: **$0.95** (often out of stock) —
  <https://www.adafruit.com/product/4209>
- Seeed W5500 Ethernet Shield SKU 103030021: **$35.00**, 3.3 V operation/5 V‑tolerant
  I/O — <https://www.seeedstudio.com/W5500-Ethernet-Shield.html>
- SEGGER J‑Link EDU Mini: **$76.00** in stock — <https://shop-us.segger.com/product/j-link-edu-mini-8-08-91/>

**Canadian reseller (CAD)**
- PiShop.ca **Pimoroni** BME280 breakout: **CAD 20.95**, **factory 0x76**, headers
  included, in stock — <https://www.pishop.ca/product/bme280-breakout-temperature-pressure-humidity-sensor/>
- PiShop.ca Adafruit BME280: **CAD 23.95** —
  <https://www.pishop.ca/product/adafruit-bme280-i2c-or-spi-temperature-humidity-pressure-sensor/>
- PiShop.ca Raspberry Pi 5/4GB: **CAD 156.85** —
  <https://www.pishop.ca/product/raspberry-pi-5-4gb/> (ships Canada only; Canada Post/UPS;
  order pickup available in Ottawa; shipping policy at
  <https://support.pishop.ca/article/5-where-do-you-ship-orders-to-and-how>)
- Adafruit (US): Raspberry Pi 5 4 GB **$130.00**, 8 GB **$200.00**, Pi 4 4 GB **$120.00**,
  27 W PSU **$14.04**, Active Cooler **$13.50**, Case+Fan **$12.00** —
  <https://www.adafruit.com/product/5812>, <https://www.adafruit.com/product/5813>,
  <https://www.adafruit.com/product/4296>, <https://www.adafruit.com/product/5814>,
  <https://www.adafruit.com/product/5815>, <https://www.adafruit.com/product/5816>

**Zephyr v4.1.0 (board/transport evidence)**
- Board DTS + pinctrl (I²C0 = P0.26/P0.27, `arduino_spi` = `spi3`, SPI3 = P1.13–P1.15):
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts>,
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840-pinctrl.dtsi>
- W5500 driver/binding: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/drivers/ethernet/Kconfig.w5500>,
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/drivers/ethernet/Kconfig>
- Seeed W5500 shield doc (explicit `nrf52840dk/nrf52840` build):
  <https://docs.zephyrproject.org/4.1.0/boards/shields/seeed_w5500/doc/index.html>,
  overlay: <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/shields/seeed_w5500/seeed_w5500.overlay>
- USB CDC‑ECM alternative:
  <https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/samples/net/sockets/echo_server/overlay-netusb.conf>
- Self‑hosted runners required for USB hardware:
  <https://docs.github.com/en/actions/concepts/runners/github-hosted-runners>,
  <https://docs.github.com/en/actions/concepts/runners/self-hosted-runners>

**Cloud / FX / tax**
- Bank of Canada USD/CAD 1.4123 (2026‑11‑09):
  <https://www.bankofcanada.ca/valet/observations/FXUSDCAD/json>
- CRA GST/HST rates (Ontario 13 %):
  <https://www.canada.ca/en/revenue-agency/services/tax/businesses/topics/gst-hst-businesses/charge-collect-which-rate.html>
- Courier Imports Remission Order s. 4.1 (US/Mexico courier: duty ≤ CAD $150, HST
  ≤ CAD $40), current to 2026‑10‑19:
  <https://laws-lois.justice.gc.ca/eng/regulations/SI-85-182/section-4.1.html>;
  CBSA memo D8‑2‑16: <https://cbsa-asfc.gc.ca/publications/dm-md/d8/d8-2-16-eng.pdf>
- Carrier/brokerage fee reference (UPS/FedEx/DHL/Canada Post, CAD):
  <https://othermile.com/courier-fees>
- Forwarders: Shippsy <https://shippsy.com/pricing/>; CrossBorder Pickups
  <https://crossborderpickups.ca/pricing/>
- Hetzner Cloud from €5.49/mo: <https://www.hetzner.com/cloud/>; Fly.io pricing:
  <https://fly.io/pricing/>; Let's Encrypt: <https://letsencrypt.org/>
- FCC equipment authorization: <https://www.fcc.gov/engineering-technology/laboratory-division/general/equipment-authorization>;
  ISED certification: <https://ised-isde.canada.ca/site/spectrum-management-telecommunications/en/equipment-certification>

**Canadian channels (verified 2026‑11‑10 unless noted)**
- Digi‑Key Canada delivery time & cost (free ≥ CAD $100; ≈ CAD $15 below; DDP; ~24 h):
  <https://www.digikey.ca/en/help-support/delivery-information/delivery-time-and-cost>
- Newark Canada delivery information (free ≥ CAD $200; $11.99 below; air includes
  brokerage/duties/taxes): <https://canada.newark.com/help-delivery-information>
- Mouser Canada help page (CAD, DDP, free most orders > CAD $100) — Wayback snapshot
  2025‑10‑17: <https://web.archive.org/web/20251017091450/https://www.mouser.ca/help/>
- PiShop.ca shipping policy (Canada‑only; weight‑based; Ottawa pickup; signature > $300):
  <https://support.pishop.ca/article/5-where-do-you-ship-orders-to-and-how>
- PiShop.ca product pages: Pimoroni BME280 <https://www.pishop.ca/product/bme280-breakout-temperature-pressure-humidity-sensor/>;
  Adafruit BME280 <https://www.pishop.ca/product/adafruit-bme280-i2c-or-spi-temperature-humidity-pressure-sensor/>;
  Arduino Ethernet Shield 2 <https://www.pishop.ca/product/arduino-ethernet-shield2/>;
  Raspberry Pi 5 4GB <https://www.pishop.ca/product/raspberry-pi-5-4gb/>
- Adafruit shipping (DHL 1–3 days to Canada; may be DDP; USPS slow):
  <https://www.adafruit.com/shipping>
- Sayal store locations & free shipping ≥ CAD $50:
  <https://secure.sayal.com/STORE4/storelocations.php>
- Canada Robotix contact (Markham; no pickup):
  <https://www.canadarobotix.com/pages/contact-us>
- Makerspaces: Site 3 coLaboratory <https://www.site3.ca/membership/>; Hacklab.TO
  <https://hacklab.to/membership/>
- Buyapi.ca → PiShop.ca merger:
  <https://www.pishop.ca/blog/legendary-buyapica-is-now-pishopca/>
- Creatron listing (2024 snapshot; live site compromised 2026‑11‑10):
  <https://web.archive.org/web/20240705052932/https://www.creatroninc.com/>

**Explicitly UNVERIFIED / re‑check before ordering**
- Current **live** Digi‑Key Canada CAD prices for the DK/PPK2/dongle (snapshots above are
  from 2025; CAD prices may have moved with FX/market conditions). Live EUR prices were
  read from Digi‑Key LU and are quoted as cross‑checks.
- **No verified USD price for the DK** from a US seller (Digi‑Key/Mouser block automated
  fetches); budget from CAD 79.38 / €43.45.
- SHT45 Canadian stock (PiShop search did not show it) — check Digi‑Key Canada and
  Canada Robotix.
- Exact SparkFun Qwiic‑board ADR jumper silkscreen (family hookup guide is the reference).
- Exact Arduino Ethernet Shield 2 INT‑pin routing / level‑shift behaviour on the DK
  (why the Seeed shield is the documented path).
- Future Electronics Canada free‑shipping threshold and live prices; RS Components
  Canada terms (site blocked); SparkFun international shipping rates.
- USB relay/switchable‑hub, NVMe SSD and used‑mini‑PC prices; logic‑analyzer prices;
  local soldering‑iron/multimeter prices.
- Seeed shield electrical stacking with the DK (verify 3.3 V rail/level shifters on the
  bench before plugging in); PPK2 `VOUT`/`GND` silkscreen labels at first use.
- DigitalOcean/AWS IoT/Oracle exact pricing (JS‑rendered pages); Saleae/Joulescope/
  Otii/INA‑module prices (optional items); Amazon.ca/AliExpress listings (bot‑blocked).
- Creatron live site (compromised on 2026‑11‑10) — verify by phone before trusting the
  domain or its listed prices.
