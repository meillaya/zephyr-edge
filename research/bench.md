# Bench Hardware Selection — nRF52840 DK / BME280 / PPK2

**Status:** research note, prices checked **2026-11-10** (America/Toronto). This feeds a
procurement doc for a buyer in Toronto, Canada. Prices are as *listed* by the vendor in the
vendor's currency; no FX conversion is applied. Anything not fetched/confirmed in this session is
marked **UNVERIFIED** with what to check at checkout.

**Project context read for this note:** `README.md`, `docs/ROADMAP.md` ("Next: hardware
validation"), `tests/hil/README.md`, `firmware/boards/nrf52840dk_nrf52840.conf|overlay`,
`docs/AUDIT.md` residual risks. Target is Zephyr v4.1 `nrf52840dk/nrf52840` (HWMv2), MCUboot
sysbuild, custom BME280 driver at devicetree `reg = <0x76>` on `&i2c0`.

---

## 1. Gap → item mapping

| Repo gap (source) | Physical bench item(s) | Why / notes |
|---|---|---|
| (a) Flash DK + run HIL ztests `sensor_log power_sched telemetry fault_inject`, serial verdict capture (`tests/hil/README.md`, `just build-hil`) | **nRF52840 DK (PCA10056) ×1**, micro-USB **data** cable, host PC (Linux/macOS/Win with J-Link OB + `/dev/ttyACM0`) | The DK includes the SEGGER J-Link OB debugger, VCOM serial over the same USB port, Arduino R3 headers, current-measurement header. No external probe required. |
| (b) Wire real BME280 at I2C **0x76** and validate vs reference ±0.5 °C / ±3 %RH / ±1 hPa (`docs/ROADMAP.md` item 2; overlay line 23) | **BME280 breakout ×1–2** (see §5 for 0x76 mod), **SHT45 reference ×1**, pressure-reference method (§7) | Zephyr build has `CONFIG_BME280_CUSTOM_EMUL=n` on this board; `bme280` ztest is excluded from the HIL loop because it asserts emulator fixtures, so a bench expected-value mode is still needed in-tree. |
| (c) Real sleep/active current in µA measured with Nordic PPK2 (`docs/ROADMAP.md` item 3; HIL README: "real power measurement requires a power monitor / PPK2 in series") | **Nordic PPK2 (nRF-PPK2) ×1**, 2nd micro-USB data cable, `nRF Connect for Desktop` + Power Profiler app | Requires one irreversible-ish bench mod on the DK: cut **SB40** to put **P22** in series with `VDD_nRF` (§8). |
| (d) MCUboot real swap-move + deliberately-invalid-signature rejection on real flash (`docs/ROADMAP.md` items 1/4; `docs/AUDIT.md` residual risks) | DK + onboard J-Link (already in (a)), alternatively external J-Link EDU Mini if the DK's OB is ever needed elsewhere | Recovery path if a bad image bricks the slot: `nrfjprog --recover` (**UNVERIFIED** command behaviour in this repo; it is the standard Nordic tool). No extra purchase needed for this gap. |
| (e) Live MQTT/TLS/OTA from device (IP transport) | **Out of scope for this note** — network transport is covered in `research/deploy-net.md` (Seeed W5500 Ethernet Shield SKU 103030021, **USD $35.00**, 3.3 V/5 V I/O tolerant). Add **2nd node (DK or nRF52840 Dongle)** for OTA/fleet target tests | A second DK is the frictionless OTA receiver; the PCA10059 dongle is much cheaper but has **no onboard debugger** (see §6). |

---

## 2. Minimum viable bench (all gaps a–d)

| # | Item | Qty | Listed price | Where | Why it is minimum |
|---|---|---|---|---|---|
| 1 | Nordic **nRF52840 DK**, order code `NRF52840-DK` (board PCA10056) | 1 | **€43.45** (EUR, excl. VAT) — DigiKey LU, in stock 412, 2026-11-10; **CAD $79.38** DigiKey CA snapshot 2025-10-13 (**snapshot**); Nordic does not publish price | DigiKey / Nordic distributors | Only target board that gives J-Link OB + VCOM + Arduino headers + P22 in one purchase |
| 2 | **Nordic PPK2**, order code `NRF-PPK2`, Adafruit PID **5048** | 1 | **US$99.95** (USD, in stock, 2026-11-10); DigiKey LU listing **€95.25** (2026-11-10); CAD $136.50 DigiKey CA snapshot 2025-08-08 | adafruit.com, DigiKey | Only credible µA-class measurement for sleep current; matches Nordic tooling |
| 3 | **Adafruit BME280** PID **2652** (STEMMA QT, 3–5 V reg + level shift) | 1 | **US$14.95** (in stock, 2026-11-10) | adafruit.com | Real sensor for driver + MCUboot/MQTT telemetry integration; needs 0x77→0x76 jumper mod (§5) |
| 4 | **Sensirion SHT45** reference, Adafruit PID **5665** | 1 | **US$12.50** (in stock, 2026-11-10) | adafruit.com | ±0.1 °C / ±1.0 %RH reference; 0x44 on same I2C bus as BME280 (no address clash) |
| 5 | Micro-USB **data** cables, 2× (DK + PPK2) | 2 | **UNVERIFIED** | local/Amazon/SparkFun | Charge-only cables are the classic bench failure (§9) |
| 6 | Jumper wires / breadboard / headers | 1 set | **UNVERIFIED** | local/Adafruit/SparkFun | DK has no Qwiic connector; header wiring is required (§7) |

**ESTIMATE** (not verified, mixed currencies, pre-tax/pre-shipping): ≈ **CAD 270–330** landed for
items 1–4 before cables; the DK price is the uncertain one (no verified USD price; CAD 79.38 is a
2025 snapshot). Everything is available from DigiKey CA except the two Adafruit sensor boards
(Adafruit ships from US; Pimoroni alternative ships from UK).

---

## 3. Recommended complete bench (adds OTA/fleet and debugging robustness)

| # | Item | Qty | Listed price | Why |
|---|---|---|---|---|
| 7 | 2nd BME280 (Pimoroni **PIM472**, factory **0x76**) | 1 | **£11.50** (GBP, 2026-11-10) | Second unit for repeatability / burn-in comparison *and* no soldering to get 0x76 |
| 8 | **nRF52840 Dongle** `NRF52840-DONGLE` (PCA10059) | 1 | CAD $15.34 / €15.69 (**snapshot / related listing**, 2026-11-10) | Cheap 2nd node for OTA/fleet; can be flashed via DK debug-out header or nRF Connect Programmer (no onboard debugger) |
| 9 | **SEGGER J-Link EDU Mini** (8.08.91) | 1 | **US$76.00** (in stock, 2026-11-10) | Independent probe; **EDU/non-commercial licence only** — do not use for commercial work |
| 10 | STEMMA QT / Qwiic cables | 1–2 | **US$0.95** (100 mm, PID 4210) / **$1.50** (400 mm, PID 5385) — **both out of stock** on 2026-11-10 | Only useful if a Qwiic host/breakout is added; DK has no Qwiic connector |
| 11 | Logic analyzer (I2C debug) | 1 | **UNVERIFIED** (Saleae Logic 8 page did not expose a price; page: saleae.com/products/logic-8) | Optional; a cheap 8-ch 24 MHz clone + sigrok/PulseView is enough to see 0x76 ACK/register traffic |
| 12 | Multimeter, ESD mat, coin cell (CR2032), standoffs | — | **UNVERIFIED** | CR2032 is the DK's lowest-noise supply for current measurement (§8) |

Also useful and **free**: `nRF Connect for Desktop` (includes Programmer and Power Profiler), Nordic
`nrfjprog`/`nrfutil`, Zephyr `west`, `pyserial-miniterm` (already in the HIL scripts).

---

## 4. Priced items — exact SKUs, stock evidence, links

| Item (exact model/SKU) | Qty | Unit price (currency, date) | Stock evidence | Link | Compatibility notes |
|---|---|---|---|---|---|
| nRF52840 DK, `NRF52840-DK` (PCA10056) | 1 | €43.45 EUR excl. VAT (2026-11-10); CAD $79.38 (snapshot 2025-10-13) | "In-Stock: 412", DigiKey LU | https://www.digikey.lu/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/1490-1072-ND/8593726 | Zephyr v4.1 board `nrf52840dk/nrf52840`; J-Link OB, VCOM, Arduino R3 headers; P22/P23 current-measure headers (§8) |
| — same, vendor page | | no price shown | — | https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK | Nordic page confirms "on-board SEGGER J-Link debugger", "Arduino Uno Revision 3 compatible", "interfaces directly with the Power Profiler Kit II" |
| Nordic PPK2, `NRF-PPK2` / Adafruit 5048 | 1 | US$99.95 USD (2026-11-10); €95.25 EUR listing (2026-11-10); CAD $136.50 snapshot 2025-08-08 | Adafruit "In stock"; DigiKey LU listing | https://www.adafruit.com/product/5048 | 200 nA–1 A, source (SMU) + ampere-meter (AMP) modes, 8 digital inputs; micro-USB cable **not included** |
| — PPK2 vendor page | | no price shown | — | https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2 | Specs + box contents + software (§8) |
| Adafruit BME280, PID 2652 | 1 | US$14.95 USD (2026-11-10) | "In stock" | https://www.adafruit.com/product/2652 | Factory **0x77**, ADDR jumper → **0x76** (§5); 3–5 V with regulator/level shift; STEMMA QT |
| SparkFun BME280 (Qwiic), `SEN-15440` | 0–1 | US$16.95 USD (2026-11-10) | page shows "In stock" | https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280-qwiic.html | **3.3 V only (1.8–3.6 V)**; factory **0x77**, 0x76 selectable via ADR jumper — verify jumper layout on the Qwiic board at checkout |
| SparkFun BME280 (older), `SEN-13676` / with headers `SEN-13905` | 0–1 | US$25.50 / US$22.50 USD (2026-11-10) | — | https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280.html | Same ADR jumper documented: "0x77 (default, '1' side) and 0x76 by slicing the trace and bridging the '0' side" |
| Pimoroni BME280, `PIM472` | 1 | £11.50 GBP (2026-11-10) | Shopify product JSON current (updated 2026-11-10) | https://shop.pimoroni.com/products/bme280-breakout | **Factory 0x76** — cut ADDR trace for 0x77; 3.3 V or 5 V; Qwiic/STEMMA QT; ships from UK |
| Adafruit SHT45, PID 5665 | 1 | US$12.50 USD (2026-11-10) | "In stock" | https://www.adafruit.com/product/5665 | ΔRH ±1.0 %RH, ΔT ±0.1 °C typical; I2C **0x44** (fixed); 3.3–5 V breakout |
| Adafruit SHT45 w/ PTFE filter, PID 6174 | 0–1 | US$13.50 USD (2026-11-10) | "Out of stock" | https://www.adafruit.com/product/6174 | Condensing/dusty locations; optional |
| Adafruit SHT40 / SHT41, PID 4885 / 5776 | alt | US$5.95 USD each (2026-11-10) | In stock | https://www.adafruit.com/product/4885 · https://www.adafruit.com/product/5776 | Cheaper temp/RH references (lower accuracy than SHT45 — read the product page before relying on them for ±0.5 °C) |
| SEGGER J-Link EDU Mini 8.08.91 | 0–1 | US$76.00 USD (2026-11-10) | "In Stock", ships 1 business day | https://shop-us.segger.com/product/j-link-edu-mini-8-08-91/ | "For hobbyists and educational use only" — licence restriction |
| Adafruit Feather nRF52840 Express, PID 4062 | alt | US$24.95 USD (2026-11-10) | **Out of stock** | https://www.adafruit.com/product/4062 | Feather form factor, SWD 2×5 0.05", no Arduino R3 headers, needs probe |
| STEMMA QT cable 4210 (100 mm) | 0–2 | US$0.95 USD (2026-11-10) | **Out of stock** | https://www.adafruit.com/product/4210 | DK has no Qwiic socket, so this is not sufficient alone |
| STEMMA QT cable 5385 (400 mm) | 0–1 | US$1.50 USD (2026-11-10) | **Out of stock** | https://www.adafruit.com/product/5385 | — |

**Not verified (do not quote as price):** nRF5340 DK (wrong target — dual core, different Zephyr
board; DigiKey LU related listing showed €43.45 but **not** confirmed on its own page), Seeed XIAO
nRF52840, Raytac/Fanstel modules, GY-BME280 listings, Saleae Logic 8, Joulescope JS220, Otii Arc
Pro, Current Ranger, INA219/INA228 modules, USB cables, breadboard/jumpers, multimeter/ESD mat.

---

## 5. CRITICAL: BME280 I2C address 0x76, per breakout (definitive)

The Bosch BME280 chip ships with **SDO = 0 → address 0x76** and **SDO = 1 → 0x77**. The project
overlay hardcodes `reg = <0x76>` (`firmware/boards/nrf52840dk_nrf52840.overlay`), so any breakout
whose factory state is 0x77 needs a one-time jumper change (or a one-line overlay change, see below).

| Breakout | Factory I2C address | How to get **0x76** | Evidence | Verdict for this project |
|---|---|---|---|---|
| **Adafruit PID 2652** | **0x77** | Solder the **ADDR jumper on the back closed**, *or* wire **SDO → GND**. Adafruit: "By default, the I2C address is 0x77. If you add a jumper from SDO to GND with a wire, or by soldering the ADDR jumper on the back closed, the address will change to 0x76." | Adafruit Learn pinouts page | **Works after one-time solder mod**; 3–5 V safe (regulator + level shift) |
| **SparkFun SEN-15440** (Qwiic) | **0x77** | Product page states "I2C Address: **0x77 (Default) or 0x76**". The board family uses an **ADR jumper** on the back: cut the trace from the '1' side and bridge the '0' side (SparkFun hookup guide, written for SEN-13676 — same BME280 breakout design). | SparkFun product page + hookup guide "Hardware Overview" | **Works after jumper/trace mod**; **3.3 V only — never feed 5 V** |
| **Pimoroni PIM472** | **0x76** | Nothing to do. "The trace on the back (marked ADDR) can be cut to change the I2C address from the **default of 0x76** to 0x77." | Pimoroni product JSON/body text | **Drop-in, zero mod** — best match for the current overlay |
| **Generic GY-BME280** (Amazon/AliExpress/LCSC) | Usually **0x76** (SDO tied low on the module) | Some modules have a 3-pad SDO jumper; most are fixed. | **UNVERIFIED** — no manufacturer documentation found | Risky: unknown pull-ups, counterfeit/calibration, 5 V vs 3.3 V variants. Only if budget-critical; scan the bus to confirm |
| Raw Bosch BME280 IC | 0x76 or 0x77 per SDO strap | Tie SDO low | Bosch datasheet (linked from repo README) | Not recommended for bench (needs own PCB/pull-ups) |

**No-solder alternative (in-tree, if the buyer dislikes soldering):** change
`firmware/boards/nrf52840dk_nrf52840.overlay` line 23 to `reg = <0x77>;` and rebuild. That is a
1-line change but it diverges from the committed 0x76 expectation used by the HIL/bench checklist;
keep 0x76 for fidelity with the repo unless the buyer explicitly accepts the 0x77 variant.

**Bus sharing:** BME280 at 0x76 and the SHT45 at 0x44 can share one I2C bus (no conflict), so both
sensors can be validated simultaneously on the DK's `&i2c0`. Only one SHT4x can be on the bus
(SHT4x address is fixed at 0x44).

**Pull-ups / levels:** all three breakouts include I2C pull-ups (SparkFun has an "I2C" jumper,
Adafruit/Pimoroni have onboard pull-ups) — no external resistors needed. Zephyr's
`i2c0_default` pinctrl for this DK does **not** enable internal pull-ups (`bias-pull-up` absent in
the v4.1.0 pinctrl dtsi), so the breakout's pull-ups are doing the work. Use the DK's **3V3** rail:
Adafruit and Pimoroni accept 3–5 V, **SparkFun must stay within 1.8–3.6 V**.

---

## 6. Board alternatives (why the DK is the recommendation)

| Board | Price evidence | Zephyr board target | Pros | Compatibility risks |
|---|---|---|---|---|
| **nRF52840 DK (PCA10056)** — *recommended* | €43.45 DigiKey LU live; CAD 79.38 snapshot | `nrf52840dk/nrf52840` (exact repo target) | J-Link OB + VCOM + Arduino headers + P22 current-meas header; `west flash` runner works out of the box; MCUboot sysbuild cross-built in CI for this board | Largest/expensive option |
| nRF52840 Dongle (PCA10059) | CAD 15.34 / €15.69 (snapshots/listings) | `nrf52840dongle/nrf52840` | Cheapest 2nd node for OTA/fleet; USB DFU bootloader | **No onboard debugger**; needs SWD probe or USB DFU + nrfutil; no Arduino headers; not suitable for the HIL serial flow without extra work |
| Adafruit Feather nRF52840 Express | US$24.95, **OOS** (2026-11-10) | `adafruit_feather_nrf52840` (upstream Zephyr) | Battery charger, native USB, Feather ecosystem | SWD 2×5 0.05" needs a probe; no Arduino R3 header; **out of stock** |
| Seeed XIAO nRF52840 | UNVERIFIED | `xiao_ble` (upstream Zephyr) | Tiny, cheap, battery pads | Needs probe; limited I/O; not the repo target |
| nRF5340 DK | UNVERIFIED (wrong target) | `nrf5340dk/nrf5340` (different SoC) | More capable | **Wrong SoC/board for this project** — do not substitute |
| Raytac MDBT50Q / Fanstel modules | UNVERIFIED | — | Production design-in | Bare modules: no debugger, no headers, no power/measurement headers; overkill for bench |

**Second board guidance:** buy **1 DK now**; add either a **2nd DK** (best for a frictionless
OTA/fleet receiver and for A/B swap tests) or a **Dongle** (cheapest second node; buy only if the
buyer is comfortable flashing it via the DK's debug-out header or USB DFU). The DK page confirms
the on-board debugger can program "external targets through the debug out header", so a DK +
dongle pair is workable without a second probe.

---

## 7. DK I2C pin mapping and sensor wiring (verified)

From the **Zephyr v4.1.0** board files (tag `v4.1.0`):

- `nrf52840dk_nrf52840.dts`: `arduino_i2c: &i2c0` — i.e. the Arduino I2C is **i2c0**.
- `nrf52840dk_nrf52840-pinctrl.dtsi`: `i2c0_default` = **TWIM_SDA on P0.26, TWIM_SCL on P0.27**.
- The same dts's `arduino_header` gpio-map: **A0=P0.03, A1=P0.04, A2=P0.28, A3=P0.29,
  A4=P0.30, A5=P0.31**, and **D14=P0.26, D15=P0.27**. So on this DK the I2C pins are the
  dedicated **SDA/SCL** pins (P0.26/P0.27); **A4/A5 are analog-only pins (P0.30/P0.31) and are NOT
  the I2C pins** (they are merely Arduino-analog A4/A5). Nordic's own user guide confirms the
  analog mapping: Table 5 "Mapping of analog pins": P0.03→A0, P0.04→A1, P0.28→A2, P0.29→A3,
  P0.30→A4, P0.31→A5.

**Wiring (BME280 → DK, both 3.3 V):**

| BME280 breakout pin | DK pin | Notes |
|---|---|---|
| VIN / 3V3 | **3V3** (power header) | Never use 5 V with SDI/SCL at 3.3 V logic; SparkFun board is 3.3 V-only |
| GND | GND | Common ground |
| SDA / SDI | **SDA = P0.26** | Zephyr `&i2c0` default |
| SCL / SCK | **SCL = P0.27** | Zephyr `&i2c0` default |
| (optional) SDO | GND | Only if the breakout's address jumper is not bridged (see §5) |

The DK's Arduino headers are sockets; the breakouts need a soldered male header (or use a
Qwiic/STEMMA QT host). Plan for **male-to-female Dupont jumpers** (male end into the DK socket,
female end onto the breakout's male header) or a breadboard + M-M jumpers. **Gender/header
pre-soldering is UNVERIFIED at purchase** — Adafruit BME280 photos show a loose header included;
SparkFun Qwiic SEN-15440 should be assumed **without** headers (solder or use Qwiic cables).

Sanity check after wiring: scan the bus (e.g. Zephyr `i2c scan` shell command if `CONFIG_I2C_SHELL`
is enabled, or a one-shot test app) and confirm the device ACKs at **0x76** (and SHT45 at 0x44).

---

## 8. PPK2 → DK current measurement header: setup, spec, caveats

**What Nordic says (verified quotes):**
- DK product page: "It also interfaces directly with the **Power Profiler Kit II**"; DK has
  "dedicated current measurement pins" for current consumption.
- DK user guide v1.3 (doc `4440_050`, §9 "Measuring current", mirrored PDF linked below):
  - Measurement connectors/solder bridges: **P22 = VDD current measurement** (series resistor
    R90, **SB40**), **P23 = VDDH** (R91, **SB41**).
  - "To put P22 in series with the load, cut the PCB track shorting solder bridge **SB40**." To
    restore: "solder SB40 or apply a jumper on P22".
  - "Connect an ampere-meter between the pins of connector **P22**".
  - Not recommended to power the board from USB during current measurements; suggested supplies:
    coin cell, external supply on **P21** (1.7–3.6 V), or Li-Po (J6/P27, 2.5–5 V).
  - "The current measurements will become unreliable when a **serial terminal** is connected to
    the virtual COM port."
  - Reprogramming while prepared for measurement: "remove measurement devices from P22, and then
    connect the USB cable."
- PPK2 product page: standalone unit, measures **200 nA–1 A** with resolution **100 nA–1 mA**;
  **ampere-meter (AMP)** mode needs an external 0.8–5 V supply for the DUT; **source mode (SMU)**
  supplies 0.8–5 V and up to 1 A itself; 8 digital inputs act as a low-end logic analyser for
  code-synchronised measurements; **box contains PPK2 PCB, jumper cables, 10-pin logic port
  cable — micro-USB cable NOT included**; order code `nRF-PPK2`. Software: **Power Profiler app
  in nRF Connect for Desktop** (the DK user guide's tool table lists nRF Connect for Desktop for
  **OS-X, Ubuntu Linux and Windows**).

**Practical hookup (use PPK2 in source/SMU mode — simplest for a DK):**
1. Flash the firmware and confirm the HIL suite first (with USB + serial), because **serial +
   accurate µA measurement are mutually exclusive per the DK guide**.
2. Disconnect the DK's USB cables. Cut **SB40** (VDD) on the DK to break the short across **P22**.
   (SB41 for the VDDH domain if ever needed.)
3. PPK2: set the board to **SMU/source** mode; connect **VOUT → P22 pin on the SoC side** and
   **GND → DK GND**, using the included jumper cables. Set source to **3.3 V** (nRF52840 VDD range
   1.7–3.6 V). Verify the exact VOUT/GND terminal labels and polarity against the PPK2 user-guide
   figure at first use — **UNVERIFIED terminal silkscreen labels in this session**.
   *Ampere-meter (AMP) mode alternative:* connect PPK2 in series between the two P22 pins and power
   the SoC from a coin cell / P21 supply — but for a DK the source-mode hookup is the low-friction
   path.
4. Connect PPK2 micro-USB (data cable, not charge-only) to the PC, launch **nRF Connect for
   Desktop → Power Profiler**, select the PPK2, and record sleep/active traces. Use the 8 digital
   inputs from a spare DK GPIO (e.g. an LED pin) if code-synchronised traces are wanted.
5. Do not expect serial output during measurement; use the PPK2's digital inputs or a second pass
   to correlate firmware phases with current.

This directly replaces the current software-only `power_sched` counters (repo note: the counters
"only measure scheduler time, not µA/mW"), giving the µA numbers the roadmap asks for.

---

## 9. Reference/calibration sensor and validation methodology

**Recommendation: one SHT45 (Adafruit 5665, US$12.50) + a pressure-reference procedure, optional
2nd BME280 for repeatability.** BME280 datasheet specs (as restated on the Adafruit page):
±1.0 °C, ±3 %RH, ±1 hPa absolute. The roadmap tolerances (±0.5 °C, ±3 %RH, ±1 hPa) are therefore
*comparison windows* against a reference, not guaranteed BME280 accuracy — the SHT45 is inside those
windows (±0.1 °C, ±1.0 %RH), so any excess delta is attributable to the BME280/unit/environment.

**Temperature & humidity method**
1. Put the BME280 and SHT45 in the same still-air enclosure/shield, at the same height, away from
   the DK's regulators/USB heat and out of direct sunlight/drafts (open bench air gives gradients
   >1 °C, which will fail a ±0.5 °C check even with perfect sensors).
2. Let both run ≥20 min (Pimoroni explicitly warns BME280 needs ≥20 min burn-in; readings take a
   few minutes to stabilise).
3. Log 1 Hz for 30–60 min; compare 1-min averages: `ΔT = BME280 − SHT45`, `ΔRH = BME280 − SHT45`.
4. Acceptance: median |ΔT| ≤ 0.5 °C and median |ΔRH| ≤ 3 %RH with **no systematic drift**; flag if
   the BME280 sits at a constant offset (unit calibration) vs random noise. ±3 %RH is realistic in
   the 20–80 %RH range; outside it BME280 humidity accuracy degrades.

**Pressure method (±1 hPa is the hard one)**
- Absolute-pressure validation needs a *same-height, traceable* reference. ECCC (Environment and
  Climate Change Canada, https://weather.gc.ca/) publishes station pressure and mean-sea-level
  pressure for Toronto stations; MSL → station conversion: `p_station ≈ p_MSL · (1 − 2.25577e−5·h)^5.25588`
  with h in metres. At Toronto's elevation a 1 hPa ≈ 8–9 m height difference — **sensor and
  reference heights must match within a few metres**.
- ECCC reports pressure rounded to 0.1 kPa (= 1 hPa), which is the same size as the tolerance, so
  the station-pressure route can only give a "not obviously wrong / within combined uncertainty"
  verdict, not a tight ±1 hPa proof. For a hard ±1 hPa claim you need a calibrated barometer or a
  barometric chamber — both out of scope; document this as a residual uncertainty in the bench
  report.
- Practical cheap fallback: 24-hour log, compare BME280 pressure trend/noise against station
  pressure trend, plus a 2nd BME280 (same model only gives repeatability, not accuracy). A
  different-family barometer (e.g. a BMP581 breakout) would give a better independent check if the
  budget allows — price **UNVERIFIED** in this session.

**Deliverable for HIL:** capture `bme280` vs `sht45` deltas + PPK2 trace + serial ztest verdicts in
the same run log (two passes if current measurement needs the USB detached).

---

## 10. Sourcing risks and unverified items (read before ordering)

1. **Nordic's own site is Cloudflare-protected** (HTTP 403 to this session). nRF52840 DK/PPK2
   **official list prices are not published on nordicsemi.com** — the page was fetched via a text
   proxy and contains no price field. Prices above are distributor prices.
2. **DigiKey and Mouser block automated fetches** (403 / access-denied / CAPTCHA). DigiKey LU was
   readable (EUR prices); DigiKey CA values are **Wayback snapshots** from 2025-08/10 taken from Digi-Key Canada,
   not live prices — **re-check at checkout**.
3. **No verified USD price for the DK.** Options at checkout: DigiKey US (`1490-1072-ND`), Mouser
   (`NRF52840-DK`), Newark/Farnell. Budget placeholder: €43.45 / CAD 79.38 as above.
4. **Stock is volatile:** Adafruit 4062 (Feather), 4210/5385 (QT cables) and 6174 (SHT45-PTFE) were
   out of stock on 2026-11-10; the BME280 2652, SHT45 5665 and PPK2 5048 were in stock. Pimoroni
   prices are GBP and ship from the UK (duty/handling for a Canadian buyer).
5. **SparkFun SEN-15440 address-jumper layout**: the product page confirms "0x77 (Default) or
   0x76", and the family hookup guide documents the ADR jumper; the Qwiic board's exact jumper
   silkscreen was not photographed in this session — inspect on arrival, or prefer the Pimoroni
   (factory 0x76) or Adafruit (documented ADDR jumper) boards.
6. **Generic GY-BME280**: no manufacturer datasheet; 0x76 is the common module default but
   **UNVERIFIED**; risk of 5 V/3.3 V variants, missing/incorrect pull-ups, clone sensors. Not
   recommended for a procurement doc that must be defensible.
7. **Cables:** DK and PPK2 both need **micro-USB** (not USB-C) **data** cables. Charge-only cables
   enumerate nothing — test each cable with a known-good device before the bench session. PPK2
   explicitly ships **without** a USB cable.
8. **DK bench mod is physical:** cutting SB40 is the documented way to insert the meter; it can be
   restored with solder or a P22 jumper. Order a spare DK if the board must stay pristine.
9. **Tariffs/duty/carrier fees** for US→Canada shipments (Adafruit/SparkFun/SEGGER) and UK→Canada
   (Pimoroni) are not estimated here — buyer must add them; DigiKey CA is domestic fulfilment.
10. **UNVERIFIED items** (no price quoted): Saleae Logic 8, Joulescope JS220, Qoitech Otii Arc Pro,
    Current Ranger, INA219/INA228 modules, nRF5340 DK, Seeed XIAO nRF52840, nRF52840 Dongle live
    price, USB cables, breadboard/jumpers, multimeter, ESD mat, BMP581 reference barometer,
    PPK2 terminal silkscreen/pinout figure.

---

## 11. Sources (all fetched 2026-11-10 unless noted)

| Source | Used for |
|---|---|
| https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK | DK features, J-Link, Arduino R3, PPK2 interface, power options |
| https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2 (+ /GetStarted) | PPK2 specs, modes, box contents, software, "measures on all Nordic DKs" |
| https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/product-briefs/power-profiler-kit-ii-pbv10.pdf | PPK2 product brief link from Nordic page |
| nRF52840 DK user guide v1.3 (doc 4440_050) — canonical: https://docs.nordicsemi.com/bundle/ug_nrf52840_dk/ ; PDF used: https://s3.amazonaws.com/snapeda/datasheet/NRF52840-DK_Nordic_Semiconductor.pdf | P22/P23, SB40/SB41, ampere-meter, measurement caveats, analog pin table, nRF Connect OS list |
| https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts | `arduino_i2c = &i2c0`, Arduino header gpio-map (A4=P0.30, A5=P0.31, D14=P0.26, D15=P0.27) |
| https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/boards/nordic/nrf52840dk/nrf52840dk_nrf52840-pinctrl.dtsi | i2c0 SDA=P0.26, SCL=P0.27 |
| https://learn.adafruit.com/adafruit-bme280-humidity-barometric-pressure-temperature-sensor-breakout/pinouts | Adafruit 2652 factory 0x77 → 0x76 ADDR jumper/SDO-GND |
| https://www.adafruit.com/product/2652 · /5665 · /5048 · /4062 · /4210 · /5385 · /4885 · /5776 · /6174 | US prices, stock, SHT45 accuracy 0x44 |
| https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280-qwiic.html · https://www.sparkfun.com/sparkfun-atmospheric-sensor-breakout-bme280.html · https://learn.sparkfun.com/tutorials/sparkfun-bme280-breakout-hookup-guide/hardware-overview | SEN-15440/13676/13905 prices, 0x77 default/0x76 select, 3.3 V-only, ADR jumper, pull-up jumpers |
| https://shop.pimoroni.com/products/bme280-breakout (and `.json`) | PIM472 £11.50, factory 0x76, cut trace for 0x77, dimensions 19×19×4.7 mm |
| https://shop-us.segger.com/product/j-link-edu-mini-8-08-91/ | J-Link EDU Mini $76.00, stock, EDU licence restriction |
| https://www.digikey.lu/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/1490-1072-ND/8593726 | DK €43.45, In-Stock 412 |
| https://www.digikey.lu/en/products/detail/nordic-semiconductor-asa/NRF-PPK2/13557476 | PPK2 €95.25 listing |
| https://www.digikey.lu/en/products/detail/nordic-semiconductor-asa/NRF52840-DONGLE/9491124 | Dongle €15.69 listing |
| DigiKey CA Wayback snapshots (Digi-Key Canada, 2025-08-08 / 2025-10-13) | CAD $136.50 PPK2, CAD $79.38 DK, CAD $15.34 dongle — **snapshot prices** |

*Note: this note is the only file added to the repo (`research/bench.md`); no firmware or source
files were modified.*
