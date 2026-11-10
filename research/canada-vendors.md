# Canadian procurement channels, landed cost, local/loan options

**Project:** `zephyr-edge` (MIT, personal/research — see `LICENSE`, `README.md`, `docs/ROADMAP.md`)
**Buyer:** Toronto, Ontario, Canada (HST 13%)
**Price-check / FX check date:** 2026-11-10. **All prices fluctuate; re-check at checkout.**
**FX anchor:** USD/CAD **1.4123** — Bank of Canada daily average, 2026-11-09
(https://www.bankofcanada.ca/rates/exchange/daily-exchange-rates/, Valet series `FXUSDCAD`).
**Status labels:** `VERIFIED` = fetched from the cited page today; `SNAPSHOT` = archived page/price, re-check before ordering; `UNVERIFIED` = could not confirm today, treat as estimate only.

Why this repo's needs matter for channel choice (`docs/ROADMAP.md`, `tests/hil/README.md`):
the remaining work is *physical* — flush the already cross-built `nrf52840dk/nrf52840` image,
run `tests/hil/run_tests.sh`, wire a real BME280, and measure sleep current with a PPK2. So the
must-haves are an nRF52840 DK, a PPK2, a BME280, and a network transport. A Raspberry Pi 5 is the
local MQTT/OTA host/bench controller; microSD/cables/jumpers are consumables.

---

## 1. TL;DR — best channel per item

| Item (canonical) | Best channel | Price anchor | Why |
|---|---|---|---|
| nRF52840 DK PCA10056 | **Digi-Key Canada** (`digikey.ca`) | **CAD $79.38** SNAPSHOT (2025-10-13) | Only major CA distributor with CAD pricing + DDP; free ship ≥ $100 CAD |
| Nordic Power Profiler Kit II | **Digi-Key Canada** | **CAD $136.50** SNAPSHOT (2025-08-08) | Same order as DK → clears $100 free-ship threshold |
| BME280 breakout | **PiShop.ca** (CAD, no border) / Adafruit US | PiShop **CAD $20.95** (generic breakout) / **$23.95** (Adafruit STEMMA); Adafruit **USD $14.95** VERIFIED | PiShop = Canadian, no import math; Adafruit if you want exact 2652 |
| SHT45/SHT40 reference sensor | **Adafruit (US)** | **USD $12.50** SHT45 VERIFIED (checked 2026-11-10) | Not stocked at PiShop (search empty); small US order or add to Adafruit basket |
| W5500 SPI Ethernet | **AliExpress** (cheap module) or **PiShop** Arduino Ethernet Shield 2 | PiShop **CAD $41.95** (W5500-based shield); AliExpress module est. CAD $4–10 UNVERIFIED | Shield = guaranteed W5500 + level shifting; module = cheap but slow/risky |
| Raspberry Pi 5 8GB | **PiShop.ca** | **CAD $249.50** VERIFIED 2026-11-10 | Canadian, CAD, no duty/brokerage; stock is the bottleneck, not price |
| Pi 5 official PSU (27W USB-C) | PiShop.ca | **CAD $16.95** (white/black US plug) | Combine with Pi order (weight shipping) |
| Pi 5 NVMe HAT | PiShop.ca | **CAD $21.25** (M.2 HAT+ Compact) | Official HAT; NVMe SSD itself UNVERIFIED (Amazon.ca/AliExpress) |
| Pi 5 case | PiShop.ca | **CAD $14.45** (official case) | — |
| microSD | PiShop.ca | **CAD $27.95** SanDisk Ultra 32GB; $28.95 official 32GB | Beware fake cards on marketplaces |
| USB data cables | PiShop.ca / Sayal (local) | **CAD $2.75–10.15** | Must be *data* cables (charge-only is the classic trap) |
| Jumper wires/headers | PiShop.ca / Sayal / Creatron (local) | **CAD $2.75–4.45** (40-pin bundles) | Local pickup beats shipping for $5 parts |
| Optional logic analyzer | Amazon.ca / AliExpress (UNVERIFIED) | est. **CAD $40–70** | Not stocked by the Canadian Pi/electronics shops checked |
| Optional nRF52840 Dongle PCA10059 | Digi-Key Canada | **CAD $15.34** SNAPSHOT (2025-08-08) | Add to DK+PPK2 order |

**Checked, not recommended for these items:** Future Electronics (Montreal HQ, CAD, free shipping
select Americas countries — see matrix), RS Components Canada (site blocked during research → terms
UNVERIFIED), Amazon.ca for the Pi itself (price/stocks volatile; counterfeit risk on SD/PSU), AliExpress
for anything timing-critical (2–6 weeks, no DDP).

---

## 2. Channel matrix (Canadian distributors + maker retailers + US direct)

### 2.1 Canadian distributors (CAD pricing, DDP, no surprise brokerage)

| Channel | Pricing | Free-shipping threshold | Shipping to Toronto | Duty/tax handling | Notes / source |
|---|---|---|---|---|---|
| **Digi-Key Canada** — https://www.digikey.ca | CAD (or USD toggle) | **Free delivery ≥ CAD $100**; **$15 CAD** charge below $100 (USD pricing: free ≥ USD $100, $20 USD below) | Usually delivers to Canada **within ~24 h** (per Digi-Key) | **Incoterms DDP** — "Duty and customs paid by DigiKey"; HST added at checkout | Marketplace items excluded from free-shipping math. Verified 2026-11-10: https://www.digikey.ca/en/help-support/delivery-information/delivery-time-and-cost |
| **Mouser Canada** — https://www.mouser.ca | CAD | **Free shipping on most orders over CAD $100** | Not verified today (site CAPTCHA); typical 1–3 business days | **Incoterms DDP (CAD pricing)**; "All prices include duty and customs fees on select shipping methods" | Wayback snapshot 2025-10-17 of https://www.mouser.ca/help/ — re-check live |
| **Newark / element14 Canada** — https://canada.newark.com | CAD | **Free delivery ≥ CAD $200** (excl. tax/freight); **$11.99 CAD** below $200 | Standard 2–3 business days from US stock; order by 6:00 PM EST (Purolator 5:30 PM) | "**All Air shipments include brokerage, duties, and taxes**"; ground may differ | VERIFIED 2026-11-10: https://canada.newark.com/help-delivery-information |
| **Future Electronics** — https://www.futureelectronics.com | CAD (region selectable) | Free shipping to "select countries in the Americas"; US row shows USD $50 minimum cart, **Canada row not captured → UNVERIFIED** | Ships from Canada/US; local FAE/account managers (Montreal HQ) | CAD site; commercial DDP terms not verified today | VERIFIED page exists: https://www.futureelectronics.com/policies/shipping-delivery; live prices blocked in research |
| **RS Components Canada** — https://ca.rs-online.com | CAD (site exists) | **UNVERIFIED** (all fetches blocked, 773-byte shell) | UNVERIFIED | UNVERIFIED | Low priority: overlaps Digi-Key/Mouser; check manually if you already have an RS account |
| **Arrow / Avnet** | USD/CAD business accounts | Business-oriented; no consumer free-ship fact verified | — | — | Not practical for a one-off personal order; skip |

### 2.2 Canadian maker retailers (no import math, HST at checkout)

| Channel | Pricing | Shipping / pickup | Key stock for this project | Source |
|---|---|---|---|---|
| **PiShop.ca** — https://www.pishop.ca (Ottawa, ON; Raspberry Pi Approved Reseller) | CAD | **Canada only** (freight-forwarder addresses without approval are cancelled); weight-based **Canada Post Expedited Parcel or UPS Standard**; same/next business day, 2 PM EST cutoff; **pickup Mon–Fri 10–4 in Ottawa** (not Toronto) | Pi 5 8GB $249.50, 27W PSU $16.95, case $14.45, M.2 HAT+ $21.25, BME280 breakouts, microSD, jumper wires, USB cables, Arduino Ethernet Shield 2 (W5500) $41.95 | Shipping policy (updated 2025-12-29): https://support.pishop.ca/article/5-where-do-you-ship-orders-to-and-how ; prices fetched 2026-11-10 |
| **Buyapi.ca** — https://www.buyapi.ca | CAD | **Redirects to PiShop.ca** — same operation (HiPi Industries) | Use PiShop; no separate account needed | https://www.pishop.ca/blog/legendary-buyapica-is-now-pishopca/ |
| **Canada Robotix (CAROBOTIX)** — https://www.canadarobotix.com | CAD | Ships from **Canadian + US warehouses**; **NO order pickup** — "Our office is not open to the public… we do not offer order pick-up" (55 Albert St, Suite 100, Markham, ON L3P 2T4) | Adafruit/SparkFun/Arduino/Raspberry Pi brands; good fallback for BME280/SHT4x/breakouts | VERIFIED 2026-11-10: https://www.canadarobotix.com/pages/contact-us ; free-shipping threshold UNVERIFIED |
| **Creatron Inc** — 349 College St, Toronto, ON M5T 1S5 · 647-349-9258 | CAD, in-store | **Local pickup/walk-in**; Mon–Fri 11–6, Sat 11–5, Sun closed; **free shipping across Ontario over $150** (2024 snapshot) | Components, headers, jumpers, USB/HDMI cables, Adafruit/SparkFun stock | SNAPSHOT 2024-07-05 via Wayback; **live creatroninc.com is compromised (serving gambling spam on 2026-11-10) — call first, do not enter payment details on that domain** |
| **Sayal Electronics** — https://secure.sayal.com/STORE4/ | CAD, in-store/online | **Free shipping on orders ≥ CAD $50** within Canada (some exceptions); 5 GTA stores: **Scarborough 3791 Victoria Park Ave** (M–F 8–6, Sat 9–4), **Vaughan 7500 Keele St Unit B, Concord L4K 1Z9**, Mississauga 1350 Matheson Blvd, Pickering 1050 Brock Rd, Markham 7701 Woodbine Ave | Breadboards, jumper/Dupont wires, headers, micro-USB cables, PSUs, tools, multimeters — best for same-day consumables | VERIFIED 2026-11-10: https://secure.sayal.com/STORE4/storelocations.php |

### 2.3 US-direct and marketplaces

| Channel | Pricing | Shipping to Canada | Duty/tax | Notes |
|---|---|---|---|---|
| **Adafruit** — https://www.adafruit.com | USD | International via **DHL Express Worldwide (1–3 business days to Canada) or USPS** (cheap; handoff to Canada Post; customs can sit 4–6 weeks, ~1% loss risk per Adafruit) | **"Some DHL Express shipments can be DDP"** — taxes charged at checkout; USPS = pay at door | VERIFIED shipping page 2026-11-10: https://www.adafruit.com/shipping. Prices: BME280 USD $14.95, SHT45 USD $12.50 (VERIFIED 2026-11-10) |
| **SparkFun** — https://www.sparkfun.com | USD | International shipping details blocked during research (support page truncated) → **UNVERIFIED** | Pay at door unless DDP method chosen; brokerage risk | Alternative source for BME280/SparkFun boards; prefer Adafruit or PiShop if SparkFun shipping unknown |
| **Seeed Studio (US/CN)** | USD | Direct China/US; DHL/FedEx | Pay at door; DDP varies | Seeed W5500 Ethernet Shield **USD $35.00** VERIFIED (checked 2026-11-10) — expensive vs generic module |
| **Amazon.ca** — https://www.amazon.ca | CAD | Prime next-day; HST | Domestic, no customs | Pi 5 8GB listings observed in page HTML ~CAD $249–286 but not reliable → **UNVERIFIED**; major counterfeit/underspec risk for microSD, no-name USB-C PSUs, BME280 clones |
| **AliExpress** — https://www.aliexpress.com | USD | 2–6 weeks (sometimes longer); ePacket/Canada Post | No DDP; CBSA may assess | Best for W5500 modules (est. CAD $4–10), jumper/header assortments, logic-analyzer clones; no returns in practice |
| **eBay / Kijiji / Facebook Marketplace** | CAD/USD | Local pickup (Kijiji/FB) or shipped | Private sale = no HST on used goods; imports still taxed | Realistic used-Pi/mini-PC/used-DK route; test before paying; no warranty |

---

## 3. Import & tax math for a Toronto buyer

### 3.1 The rules (verified)

- **HST Ontario = 13%** — CRA table: "Ontario | 13%" (VERIFIED 2026-11-10:
  https://www.canada.ca/en/revenue-agency/services/tax/businesses/topics/gst-hst-businesses/charge-collect-which-rate.html).
  As an individual you do **not** need a GST/HST number; tax is charged/collected at checkout (DDP) or
  by the carrier (DAP).
- **Courier de minimis (CUSMA/USMCA goods):** *Courier Imports Remission Order*, s. 4.1 (current to
  2026-10-19): for goods **imported from Mexico or the United States transported by courier** —
  **(a) customs duties are remitted if value for duty ≤ CAD $150; (b) excise taxes (GST/HST) are
  remitted if value for duty ≤ CAD $40.** Source: https://laws-lois.justice.gc.ca/eng/regulations/SI-85-182/section-4.1.html
  → Practical: a US-origin order above **CAD $40** pays **13% HST**; between $40–$150 duty is normally
  relieved; above $150 duty applies **unless** the goods qualify under CUSMA (electronics are commonly
  MFN-free anyway — treat per-HS-code as UNVERIFIED). CBSA memo: D8-2-16 (Courier Imports Remission),
  https://cbsa-asfc.gc.ca/publications/dm-md/d8/d8-2-16-eng.pdf (not re-fetched today).
- **Mail:** Canada Post applies a low-value exemption (commonly **CAD $20**) with a **$9.95 handling fee**
  when duty/tax is owed. The $20 mail figure is widely documented but the official page 404'd today →
  **UNVERIFIED-CURRENT**; CBSA/Canada Post pages to confirm: https://www.canadapost-postescanada.ca/cpc/en/support/topic/customs-requirements
- **Non-US/Mexico courier goods (e.g. AliExpress):** the $150 CUSMA relief does not apply; relief is
  limited (historically ~CAD $40) — confirm current CBSA guidance; assume HST applies above ~$40.
- **DDP vs DAP is the real cost driver.** DDP (Digi-Key, Mouser CAD, Newark air, Adafruit DHL option)
  = tax at checkout, **no brokerage at the door**. DAP/USPS/FedEx Ground on a US order = courier
  invoices brokerage + disbursement on top of HST.

### 3.2 Typical courier brokerage fees (published fee lists; "typical/approx")

| Carrier / service | Fee when duty/tax owed | Notes |
|---|---|---|
| **Canada Post** (mail) | **$9.95** handling | Charged only when duty/tax is owed |
| **UPS Standard** | **Entry preparation fee by value for duty**: $0–20 → $0; 20.01–40 → $7; 40.01–60 → $19.75; 60.01–100 → $23.55; 100.01–150 → $31.45; 150.01–200 → $35.90; 200.01–350 → **$70.25**; 350.01–500 → $78.15; + **disbursement 3.7% (min $7.75)**; +$6 per line after 3 | Worst option for small orders — can double the tax bill |
| **UPS Express / Express Saver / Expedited** | Disbursement **3.7% (min $12)**; routine clearance included up to 3 product lines | Usually the sane UPS choice |
| **FedEx Express / Ground** | Disbursement **3.1% (min $12)**; +$5.60 per line over 3; +$17.10 if another agency must clear | No entry fee by value per FedEx fee list |
| **DHL Express** | **2.75% (min $18)** without DHL account; **min $12** with account; +$5.50 per line over 3 | DHL DDP at checkout avoids this |

Source (carrier fee lists, CAD): https://othermile.com/courier-fees — UPS guide dated 2026-09-06,
FedEx rates effective 2026-07-20, DHL guide valid 2026-01-01, Canada Post page 2023-08-01. Estimates,
not customs advice.

### 3.3 Forwarders (US address → Toronto)

| Forwarder | Fee | Includes | Pickup/delivery |
|---|---|---|---|
| **Shippsy** — https://shippsy.com/pricing/ | **$10.99 + tax**/package (4–7 business days) or **$9.99 + tax** (2–3 business days); >10 lb adds **$0.50/lb** (to 100 lb) | Fee covers brokerage + transportation | Doorstep delivery option |
| **CrossBorder Pickups** — https://crossborderpickups.ca/pricing/ | Light **$8.50**; Regular 1–10 lb **$11.00**; Large 10–30 lb **$17.00**; Oversize 30–150 lb **$20 + $0.50/lb beyond 30** | Brokerage included, **duties & taxes still applicable**, first 2 customs lines, storage 15 days | **Pickup at Canadian locations** (GTA) |

Both bill HST on the goods (same 13%) but replace the courier's $12–$70 brokerage with a flat
~$9–12 fee. They win when a US retailer's only option is USPS/FedEx Ground (no DDP) or when direct
DHL shipping is expensive.

### 3.4 Worked example — USD $160 order, direct vs forwarder

Assumptions: item value **USD $160** → **CAD $225.97** at 1.4123 (2026-11-09). US-origin electronics,
CUSMA → duty ≈ $0; HST 13% = **CAD $29.38** (above the CAD $40 tax de minimis). Retail shipping assumed
USD $15 (CAD $21.18) unless noted. Courier fee brackets applied to value for duty ≈ CAD $226.

| Route | Item (CAD) | Retail ship | HST | Brokerage/handling | **Landed (CAD)** |
|---|---|---|---|---|---|
| **DHL Express DDP** (Adafruit option) | 225.97 | 21.18 | 29.38 (checkout) | **$0** (DDP; no door fee) | **≈ $276.53** |
| **USPS → Canada Post** (DAP) | 225.97 | 35.31 (USD 25) | 29.38 | $9.95 handling | **≈ $300.61** |
| **UPS Express** (DAP) | 225.97 | 21.18 | 29.38 | $12.00 disbursement min | **≈ $288.53** |
| **UPS Standard** (DAP) | 225.97 | 21.18 | 29.38 | **$70.25 entry + $7.75 disbursement** | **≈ $354.53** |
| **Shippsy** (US retailer free US shipping) | 225.97 | $0 (US domestic) | 29.38 | $12.42 ($10.99+tax) | **≈ $267.77** |
| **CrossBorder Pickups** (GTA pickup) | 225.97 | $0 (US domestic) | 29.38 | $11.00 | **≈ $266.35** |
| **Digi-Key/Mouser CAD direct** (DDP, free ship ≥ $100) | CAD price already | $0 | 13% at checkout | $0 | **just item × 1.13** |

**Takeaways:** (1) avoid DAP + UPS Standard — brokerage can exceed the tax; (2) a US$160 US order is
cheapest via forwarder (~$266–268) or DHL-DDP (~$277), and most expensive via UPS Standard (~$355);
(3) for the actual project list, **buying the Nordic/PPK2 from Digi-Key Canada in CAD and the Pi kit from
PiShop.ca means zero import math** — use US-direct only for items unavailable in Canada (SHT45, niche
modules, logic analyzer).

---

## 4. Per-item sourcing table (CAD evidence)

| Item | Best channel + link | Price | Alt channel / price | Notes |
|---|---|---|---|---|
| **nRF52840 DK PCA10056** | Digi-Key Canada — https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/8593726 | **CAD $79.38** SNAPSHOT 2025-10-13, InStock | Mouser CA (UNVERIFIED price, DDP, free ≥ $100); Future Electronics FAE | The DK is the HIL prerequisite (`tests/hil/README.md`) |
| **Nordic PPK2** | Digi-Key Canada — https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF-PPK2/13557476 | **CAD $136.50** SNAPSHOT 2025-08-08, InStock | — | Combine with DK in one DK order → free shipping |
| **nRF52840 Dongle PCA10059** (optional) | Digi-Key Canada (part `NRF52840-DONGLE`) | **CAD $15.34** SNAPSHOT 2025-08-08 | — | Add to DK order; useful for Thread/BLE transport later |
| **BME280 breakout** | PiShop.ca — https://www.pishop.ca/search.php?search_query=BME280 | **CAD $20.95** (BME280 breakout), **CAD $23.95** (Adafruit STEMMA BME280) | Adafruit — https://www.adafruit.com/product/2652 **USD $14.95** VERIFIED; Canada Robotix (UNVERIFIED price) | PiShop = no import; Adafruit = exact 2652 board |
| **SHT45 breakout** | Adafruit — https://www.adafruit.com | **USD $12.50** VERIFIED (checked 2026-11-10) | SHT40 breakout likely cheaper at Adafruit/Canada Robotix (UNVERIFIED) | Not found at PiShop (search empty 2026-11-10) |
| **W5500 SPI Ethernet** | PiShop.ca — Arduino Ethernet Shield 2 (W5500) **CAD $41.95** | **CAD $41.95** | Seeed W5500 Ethernet Shield **USD $35.00** VERIFIED; AliExpress W5500 module est. **CAD $4–10** UNVERIFIED | W5500 module needs 3.3V level care; shield is plug-and-play on DK headers |
| **Raspberry Pi 5 8GB** | PiShop.ca — https://www.pishop.ca/product/raspberry-pi-5-8gb/ | **CAD $249.50** VERIFIED 2026-11-10 | Adafruit **USD $200** VERIFIED (≈ CAD $282.46 + HST/ship); Amazon.ca UNVERIFIED | RAM-price volatility in 2025–2026; buy when in stock, prices move fast |
| **Pi 5 27W USB-C PSU** | PiShop.ca | **CAD $16.95** (US plug, white/black) | 45W USB-C PSU **CAD $21.45** | Use official PSU; no-name PSUs risk undervoltage/fire |
| **Pi 5 NVMe HAT** | PiShop.ca | **CAD $21.25** (M.2 HAT+ Compact) | — | NVMe SSD itself: Amazon.ca/AliExpress **UNVERIFIED** (est. CAD $60–110 for 256–512 GB) |
| **Pi 5 case** | PiShop.ca | **CAD $14.45** (official case); bumper **CAD $6.30** | — | — |
| **microSD** | PiShop.ca | **CAD $27.95** SanDisk Ultra 32GB; **CAD $28.95** official 32GB; official 64GB **$45.95** | Sayal/Creatron local (UNVERIFIED) | Avoid marketplace fakes; 32 GB is plenty for MQTT/OTA bench |
| **USB data cables** | PiShop.ca / Sayal (local) | **CAD $2.95–10.15** (micro-USB A→microB, 1 m $10.15) | Sayal same-day | Must be data-rated; label them once bought |
| **Pi 5 USB-C / USB-TTL debug cable** | PiShop.ca | USB-TTL serial for Pi 5 debug **CAD $9.15** | — | Optional but handy |
| **Jumper wires / headers / breadboard** | PiShop.ca / Sayal (local) | 40× M-F **CAD $2.95**; F-F **$2.95**; 6-pin 12" **$4.45**; breadboard wiring kit **$8.45** | Creatron/Sayal same-day | Buy locally if you need them today |
| **Optional logic analyzer** | Amazon.ca / AliExpress | **UNVERIFIED** est. **CAD $40–70** (8-ch, 24 MHz clone) | Saleae/Kingst LA1010 (cad $) UNVERIFIED | Not stocked at PiShop/Creatron-as-online in research |

**Price evidence notes:** Digi-Key product pages are Cloudflare-blocked to scripted fetches; the DK/PPK2/
Dongle CAD prices above come from **Wayback snapshots** from Digi-Key Canada (2025-10-13 and 2025-08-08).
They are directionally reliable but **must be re-checked at checkout**. Everything tagged `VERIFIED`
was fetched from the cited live page on 2026-11-10.

---

## 5. Landed-cost budget tiers (CAD, all-in with 13% HST)

Assumptions: Digi-Key order ships free (≥$100); PiShop shipping estimated **CAD $18** for the full
accessory basket (weight-based, exact amount shown at cart — UNVERIFIED); Adafruit SHT45 shipping
estimated **USD $18** (DHL/USPS; UNVERIFIED) and converted at 1.4123.

| Tier | Contents | Goods (CAD) | Shipping | HST 13% | **Landed total (CAD)** |
|---|---|---|---|---|---|
| **0 — Minimum** (validate sensor path + flash DK) | DK $79.38 + BME280 $20.95 | 100.33 | $15 (DK under-$100) + ~$12 (PiShop) = $27 | 16.55 | **≈ $144** (split order). If Digi-Key stocks a BME280 breakout, one $100+ order ≈ **$117** |
| **1 — Recommended** (HIL + PPK2 + Pi 5 host) | DK $79.38 + PPK2 $136.50 (DK, free ship) = $215.88; PiShop: Pi5 $249.50 + PSU $16.95 + case $14.45 + M.2 HAT $21.25 + microSD $27.95 + BME280 $20.95 + jumpers $4.45 + micro-USB $10.15 + USB-TTL $9.15 + Ethernet shield $41.95 = $416.75; Adafruit SHT45 USD $12.50 | 632.63 + 17.66 (SHT45) = **650.29** | $0 (DK) + $18 (PiShop) + $25.42 (Adafruit ≈ USD 18) = **43.42** | **90.18** | **≈ CAD $784** (add ~5% contingency → ~$824) |
| **2 — Complete** (add dongle, NVMe, logic analyzer) | Tier 1 + Dongle $15.34 (add to DK order) + NVMe SSD est. $60–110 + logic analyzer est. $40–70 | +115.34…195.34 | $0 | +15…25.4 | **≈ CAD $915–1,005** |

Notes: Adafruit SHT45 order can be avoided if you accept an SHT40/others from PiShop/Canada Robotix
(price UNVERIFIED) — then the whole Tier 1 is two domestic orders (Digi-Key + PiShop) with zero
customs. Mini-PC alternative to the Pi 5 (used eBay/Kijiji, CAD $150–300 UNVERIFIED) removes the
$249.50 Pi line but adds its own PSU/SSD; the repo only needs a Linux host for MQTT/OTA, so a used
SFF PC is a legitimate substitution.

---

## 6. Toronto local, makerspaces, student/edu, loaner routes

### 6.1 Local pickup (no shipping, no waiting)

- **Sayal Electronics — 3791 Victoria Park Ave, Scarborough** (M–F 8–6, Sat 9–4; 416-494-8999);
  also **Vaughan 7500 Keele St Unit B, Concord** (M–F 8–6, Sat 10–4; 905-738-7193),
  Mississauga (1350 Matheson Blvd), Pickering (1050 Brock Rd), Markham (7701 Woodbine Ave).
  Best for jumper wires/headers/breadboards/cables/multimeter/tools same-day. Free shipping ≥ $50 if
  ordering online instead. (VERIFIED: https://secure.sayal.com/STORE4/storelocations.php)
- **Creatron Inc — 349 College St** (M–F 11–6, Sat 11–5; 647-349-9258). Components + Adafruit/SparkFun
  stock, walk-in. **Warning: creatroninc.com was serving gambling spam on 2026-11-10 — call before
  visiting and don't enter payment data there** (2024 snapshot: free shipping Ontario > $150).
- **Canada Robotix — NO pickup** ("office is not open to the public… we do not offer order pick-up",
  Markham); mail-order only.
- **PiShop.ca — no Toronto pickup**; pickup only at their Ottawa warehouse (9-190 Colonnade Rd,
  Mon–Fri 10–4). Not useful for a Toronto buyer unless shipping anyway.
- **Digi-Key/Mouser/Newark** have no Canadian pickup counter — courier only.

### 6.2 Makerspaces / tools (soldering, oscilloscope, rework)

- **Site 3 coLaboratory** (site3.ca): **CAD $60/month + HST**, all three membership levels; first+last
  month ($120) due at signup; storage locker +$10–15/month. Electronics/soldering tools, oscilloscope
  likely available — confirm tool list. (VERIFIED: https://www.site3.ca/membership/)
- **Hacklab.TO** (hacklab.to): dues **CAD $75/month**, 3-day onboarding before dues start; open-house
  culture. (VERIFIED: https://hacklab.to/membership/)
- **Toronto Tool Library** (torontotoollibrary.com): tool lending rather than an electronics lab; the
  membership page 404'd → **membership fee UNVERIFIED**; useful for enclosures/power tools, not PPK2-class
  gear.
- **University makerspaces** (U of T Myhal/fabrication centres, TMU, York): typically restricted to
  students/staff or require an affiliation/guest agreement — **UNVERIFIED** for outside use; a student
  collaborator is the realistic path.
- **Practical fallback:** a soldering iron + multimeter from Sayal (~CAD $25–60 UNVERIFIED) covers
  header soldering for BME280/breakouts; the DK's onboard J-Link means no external programmer is needed.

### 6.3 Student / education discounts (realistic)

- **No public individual edu discount verified** at Digi-Key/Mouser/Newark/PiShop for this project —
  academic pricing usually runs through an institutional account/purchase order.
- Ask at **Sayal/Creatron counters** (UNVERIFIED) and use **PiShop education bundles** if buying multiple
  Pi units; Adafruit/SparkFun teacher discounts are US classroom programs — not applicable to an
  individual research project.
- If you have a university affiliation, buy through the lab's Digi-Key/Mouser account: same CAD prices,
  consolidated billing, and the institution handles HST/paperwork.

### 6.4 Loaner / EVAL-kit routes (be honest)

- **Nordic Semiconductor does not run a public loaner/EVAL-kit program for individuals.** DK/PPK2
  samples are routed through distributors/sales for qualified commercial opportunities; an MIT hobby/
  research project is unlikely to qualify. (UNVERIFIED as a policy statement — inferred from
  distributor/sales-routed sample model; no public individual request path found.)
- **Realistic routes that do work:**
  1. **University lab/supervisor** requests through an institutional account — strongest option; a
     supervisor can often borrow an extra DK from a teaching lab for a term.
  2. **Future Electronics FAE / account manager** (Montreal HQ, Canadian branches): they hold Nordic
     stock and demo units. Ask for a **short-term demo/loaner** with a one-paragraph project brief,
     timeline, and expected volume; as an individual expect a polite no — as a startup/company account
     it's normal.
  3. **Local Zephyr/embedded meetups and makerspace members** — borrow/co-verify; the Zephyr community
     often has DKs sitting on desks.
  4. **Used DK on Kijiji/eBay** (est. CAD $40–70 UNVERIFIED) — the API-version DK (PCA10056) is
     well-supported; verify it enumerates as a J-Link and flash a test image.
  5. **On PPK2 specifically:** very rarely loaned. Cheaper substitute for mW-level checks: a Nordic
     PPK1 (older), or a bench supply + shunt resistor/DMM; the repo's HIL scripts only need functional
     verification (`tests/hil/README.md` §Notes) — PPK2 is for the µA/mW measurement task in
     `docs/ROADMAP.md`.

---

## 7. How to place the orders — checklist

1. **Accounts & currency:** create `digikey.ca` (set **CAD**), `mouser.ca` (CAD — DDP), `pishop.ca`.
   No GST/HST number needed as an individual; a university lab account can use its own.
2. **Order 1 — Digi-Key Canada (free shipping ≥ $100):** nRF52840 DK **CAD $79.38** + PPK2
   **CAD $136.50** (+ Dongle $15.34 = $231.22). Verify current CAD prices at checkout; **DDP** means
   no brokerage surprise. Expected delivery to Toronto ~24 h per Digi-Key. Note: Digi-Key may split
   Marketplace items — they don't count toward free shipping.
3. **Order 2 — PiShop.ca (all Raspberry Pi + sensors + consumables):** Pi 5 8GB, 27W PSU, case,
   M.2 HAT+ Compact, microSD, BME280, jumper wires, USB cables, Ethernet shield. Shipping is
   **weight-based** (Canada Post Expedited/UPS Standard) — add an item to see the rate, combine
   everything to pay one shipping charge; signature required over $300.
4. **Order 3 (only if SHT45 needed) — Adafruit:** SHT45 **USD $12.50**; choose **DHL DDP** for tax at
   checkout (fast, no door fee) or USPS for cheapest (slow, $9.95 Canada Post handling). Keep the order
   small; >CAD $40 value pays 13% HST either way.
5. **Local same-day (Sayal/Creatron):** jumper wires, headers, breadboard, soldering supplies,
   micro-USB data cable — avoids $10–15 shipping on $5 parts. Call Creatron first due to the website issue.
6. **US retailer with no DDP option (SparkFun etc.):** consider **Shippsy** ($10.99 + tax) or
   **CrossBorder Pickups** ($11.00 regular) instead of UPS Standard — see §3.4.
7. **Payment methods:** credit card accepted by Digi-Key/Mouser/Newark/PiShop/Adafruit; PayPal at
   Digi-Key/Mouser (UNVERIFIED current); PiShop takes major cards. Foreign-currency charges may add a
   ~2.5% card FX fee on USD orders (check your card).
8. **Shipping address:** use a real street address (no PO box for couriers); if using a forwarder, use
   the exact US address + suite format they give you, and remember **PiShop cancels unapproved
   freight-forwarder addresses**.
9. **Backorders:** Digi-Key/Mouser may hold or split; a split can drop you below a free-shipping
   threshold — add a cheap in-stock filler (jumpers/headers) or accept $15. For Pi 5 shortages, check
   PiShop stock daily and avoid paying a premium on Amazon/AliExpress.
10. **Keep evidence:** save invoices/order confirmations (warranty/returns, e.g. DK returns policy via
    order history). If a courier bills brokerage unexpectedly, ask for the commercial invoice and the
    DDP terms from the seller before paying.

---

## 8. Risk notes / watch-outs

- **Charged-at-door surprises:** DAP + UPS Standard is the worst case (§3.2); choose DDP channels or a
  forwarder for US orders worth > CAD $40.
- **Counterfeits/underspec:** "BME280" modules that are actually BMP280; fake microSD capacity;
  no-name USB-C PSUs (undervoltage reboots, fire risk); USB "charging" cables with no data lines — all
  common on Amazon.ca/AliExpress. Buy the PSU/SD from PiShop and cables from Sayal; label data cables.
- **Raspberry Pi 5 price/stock volatility:** PiShop **CAD $249.50** (8GB) on 2026-11-10 reflects the
  2025–2026 memory-price surge; Adafruit USD $200. Set a budget alert and re-check; a used mini PC is
  a valid substitute for the MQTT/OTA host.
- **Creatron website compromise:** `creatroninc.com` served Indonesian gambling spam on 2026-11-10
  (domain/SEO hijack). Do not trust it; phone 647-349-9258 or walk in.
- **Digi-Key product-page prices** in this note are SNAPSHOTs (Cloudflare-blocked today) — re-check.
- **SHT45/w5500/module availability** moves; PiShop had no SHT45 and no bare W5500 module on
  2026-11-10; Canada Robotix/Adafruit/AliExpress are the fallbacks.
- **Returns across the border** are painful (shipping + possible re-import fees); prefer DDP channels
  and Canadian stock for anything likely to be returned.
- **PPK2 measurement wiring:** PPK2 powers the DK externally; do not back-power via USB while measuring
  (standard Nordic guidance) — affects the µA/mW numbers you report.
- **Prices in this document are research anchors** at 2026-11-10; all fluctuate. Nothing here is
  customs or legal advice.

---

## 9. Source log (accessed 2026-11-10 unless noted)

**Verified live today**
- Bank of Canada daily USD/CAD (1.4123, 2026-11-09): https://www.bankofcanada.ca/rates/exchange/daily-exchange-rates/ · API: https://www.bankofcanada.ca/valet/observations/FXUSDCAD/json?recent=1
- CRA GST/HST rates — Ontario 13%: https://www.canada.ca/en/revenue-agency/services/tax/businesses/topics/gst-hst-businesses/charge-collect-which-rate.html
- Courier Imports Remission Order s. 4.1 ($150 duty / $40 tax, US+Mexico, courier), current to 2026-10-19: https://laws-lois.justice.gc.ca/eng/regulations/SI-85-182/section-4.1.html
- CBSA Memorandum D8-2-16 (Courier Imports Remission) — PDF not re-fetched: https://cbsa-asfc.gc.ca/publications/dm-md/d8/d8-2-16-eng.pdf
- Courier brokerage fee lists (UPS 2026-09-06, FedEx 2026-07-20, DHL 2026-01-01, Canada Post 2023-08): https://othermile.com/courier-fees
- Digi-Key Canada delivery time & cost (free ≥$100 CAD; $15 CAD under; DDP; ~24 h delivery): https://www.digikey.ca/en/help-support/delivery-information/delivery-time-and-cost
- Digi-Key Canada international shipping rates: https://www.digikey.ca/en/help-support/delivery-information/international-shipping-rates
- Newark Canada delivery information (free ≥$200 CAD; $11.99 under; air includes brokerage/duties/taxes; 2–3 business days): https://canada.newark.com/help-delivery-information
- Future Electronics shipping & delivery (free shipping select Americas countries; Canada threshold not captured): https://www.futureelectronics.com/policies/shipping-delivery
- PiShop.ca shipping policy (Canada-only; weight-based; Ottawa pickup; signature >$300): https://support.pishop.ca/article/5-where-do-you-ship-orders-to-and-how
- PiShop.ca product/search prices: https://www.pishop.ca/product/raspberry-pi-5-8gb/ and https://www.pishop.ca/search.php?search_query=BME280 (etc.)
- Buyapi.ca → PiShop.ca merger: https://www.pishop.ca/blog/legendary-buyapica-is-now-pishopca/
- Canada Robotix contact (Markham; no pickup): https://www.canadarobotix.com/pages/contact-us
- Sayal store locations/hours + free shipping ≥$50: https://secure.sayal.com/STORE4/storelocations.php
- Adafruit shipping & returns (DHL 1–3 days to Canada; USPS slow; DHL may be DDP): https://www.adafruit.com/shipping
- Shippsy pricing ($9.99/$10.99 + tax per package): https://shippsy.com/pricing/
- CrossBorder Pickups pricing (Light $8.50 / Regular $11 / Large $17 / Oversize $20+): https://crossborderpickups.ca/pricing/
- Site 3 coLaboratory membership ($60/mo + HST): https://www.site3.ca/membership/
- Hacklab.TO membership ($75/mo): https://hacklab.to/membership/

**Snapshots (re-check before ordering)**
- Mouser Canada help page (CAD, DDP, "all prices include duty and customs fees on select shipping methods", free shipping most orders over $100 CAD) — Wayback 2025-10-17: https://web.archive.org/web/20251017091450/https://www.mouser.ca/help/
- Creatron store location/hours + free shipping Ontario > $150 — Wayback 2024-07-05: https://web.archive.org/web/20240705052932/https://www.creatroninc.com/
- Digi-Key DK CAD $79.38 (2025-10-13), PPK2 CAD $136.50 (2025-08-08), Dongle CAD $15.34 (2025-08-08) — Wayback snapshots from Digi-Key Canada; product URLs: https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF52840-DK/8593726 · https://www.digikey.ca/en/products/detail/nordic-semiconductor-asa/NRF-PPK2/13557476

**Blocked / UNVERIFIED (could not confirm today)**
- Live Digi-Key product pages (Cloudflare) — price anchors are snapshots.
- Live Mouser.ca (CAPTCHA) — terms from Wayback.
- RS Components Canada (ca.rs-online.com returned a 773-byte shell) — all terms UNVERIFIED.
- SparkFun international shipping rates (support page truncated) — cost/carrier UNVERIFIED.
- Future Electronics Canada free-shipping threshold and live prices — page exists but Canada row/live pricing not captured.
- Canada Post mail $20 exemption page (404 today) — the $20 mail figure should be confirmed before relying on it.
- Amazon.ca and AliExpress prices (bot-blocked) — marked UNVERIFIED; no price claims made.
- Logic-analyzer and NVMe SSD prices — no verified Canadian listing found; estimates only.
- Nordic loaner/EVAL policy — no public individual program found; guidance is realistic expectation, not a quoted policy.
