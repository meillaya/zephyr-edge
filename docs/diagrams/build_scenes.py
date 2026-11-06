#!/usr/bin/env python3
"""Generate the committed Excalidraw scene sources for docs/diagrams.

The official Excalidraw CLI only renders standard scene JSON (it does not expand
the shorthand "label" / "cameraUpdate" pseudo-elements from the agent skill), so
this helper emits full elements. Visual rules follow the NixOS excalidraw skill:
hand-drawn roughness 2, Excalifont family 1, dark background #1e1e2e, the pastel
palette, 28/20/16/14 font sizes, >=120x60 shapes, 20-30px gaps, and
shape -> text -> arrow z-order.
"""

from __future__ import annotations

import json
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent
rng = random.Random(20261106)

# Palette (shape fills / strokes / text) from the drawing rules.
BG = "#1e1e2e"
TEXT = "#e5e5e5"
MUTED = "#a0a0a0"
BLUE, GREEN, ORANGE, PURPLE, RED, YELLOW, TEAL, PINK = (
    "#1e3a5f", "#1a4d2e", "#5c3d1a", "#2d1b69", "#5c1a1a", "#4d4a1a", "#1a4d4d", "#4d1a3f"
)
S_BLUE, S_GREEN, S_ORANGE, S_PURPLE, S_RED, S_YELLOW, S_TEAL, S_PINK = (
    "#4a9eed", "#22c55e", "#f59e0b", "#8b5cf6", "#ef4444", "#eab308", "#06b6d4", "#ec4899"
)


def _seed() -> int:
    return rng.randint(1, 2_000_000_000)


def _base(kind: str, x: float, y: float, w: float, h: float, **kw):
    e = {
        "id": f"{kind}_{_seed()}",
        "type": kind,
        "x": x,
        "y": y,
        "width": w,
        "height": h,
        "angle": 0,
        "strokeColor": "#1e1e1e",
        "backgroundColor": "transparent",
        "fillStyle": "solid",
        "strokeWidth": 2,
        "strokeStyle": "solid",
        "roughness": 2,
        "opacity": 100,
        "groupIds": [],
        "frameId": None,
        "roundness": None,
        "seed": _seed(),
        "version": 1,
        "versionNonce": _seed(),
        "isDeleted": False,
        "boundElements": [],
        "updated": 1,
        "link": None,
        "locked": False,
    }
    e.update(kw)
    return e


def text(s: str, x: float, y: float, size: int = 20, color: str = TEXT,
         width: float | None = None, align: str = "center"):
    lines = s.split("\n")
    w = width or max(60.0, max(len(line) for line in lines) * size * 0.62)
    h = len(lines) * size * 1.3
    return _base("text", x, y, w, h, strokeColor=color, backgroundColor="transparent",
                 fontFamily=1, fontSize=size, text=s, originalText=s,
                 textAlign=align, verticalAlign="top", containerId=None,
                 lineHeight=1.25, baseline=size * 0.8)


def center_text(s: str, cx: float, cy: float, size: int = 20, color: str = TEXT):
    lines = s.split("\n")
    w = max(60.0, max(len(line) for line in lines) * size * 0.62)
    h = len(lines) * size * 1.3
    return text(s, cx - w / 2, cy - h / 2, size, color, width=w)


def box(_id: str, label: str, x: float, y: float, w: float, h: float, fill: str,
        stroke: str, size: int = 20):
    # _id is kept so scenes read as a component list; Excalidraw ids are seeded.
    # Shrink the label font until its longest line fits inside the shape.
    max_len = max(len(line) for line in label.split("\n"))
    fit = int((w - 24) / (max_len * 0.62))
    size = max(14, min(size, fit))
    b = _base("rectangle", x, y, w, h, strokeColor=stroke, backgroundColor=fill,
              fillStyle="solid", roundness={"type": 3})
    return [b, center_text(label, x + w / 2, y + h / 2, size)]


def zone(label: str, x: float, y: float, w: float, h: float, fill: str, stroke: str):
    z = _base("rectangle", x, y, w, h, strokeColor="transparent", backgroundColor=fill,
              fillStyle="solid", opacity=30, roundness={"type": 3})
    return [z, text(label, x + 14, y + 8, 16, stroke, align="left")]


def arrow(x1: float, y1: float, x2: float, y2: float, color: str = S_BLUE,
          label: str | None = None, dashed: bool = False, size: int = 14):
    a = _base("arrow", x1, y1, abs(x2 - x1), abs(y2 - y1), strokeColor=color,
              strokeStyle="dashed" if dashed else "solid", roundness={"type": 2})
    a.update({
        "points": [[0, 0], [x2 - x1, y2 - y1]],
        "lastCommittedPoint": None,
        "startBinding": None,
        "endBinding": None,
        "startArrowhead": None,
        "endArrowhead": "arrow",
    })
    out = [a]
    if label:
        out.append(center_text(label, (x1 + x2) / 2, (y1 + y2) / 2 - 14, size, MUTED))
    return out


def scene(elements, name: str):
    # Dark canvas sized to the 1200x900 (4:3) content area; the official
    # renderer fits all elements, so do not use the skill's huge dark backdrop.
    bg = _base("rectangle", 0, 0, 1200, 900, strokeColor="transparent",
               backgroundColor=BG, fillStyle="solid", roundness=None)
    doc = {
        "type": "excalidraw",
        "version": 2,
        "source": "https://excalidraw.com",
        "elements": [bg] + elements,
        "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
        "files": {},
    }
    (HERE / f"{name}.excalidraw").write_text(json.dumps(doc, indent=1) + "\n")
    print(f"wrote {name}.excalidraw ({len(doc['elements'])} elements)")


def architecture():
    e = []
    e.append(text("Zephyr Edge Platform — System Architecture", 220, 24, 28))
    e += zone("EDGE DEVICE  (native_sim / nRF52840)", 28, 96, 520, 730, BLUE, S_BLUE)
    e += zone("MQTT", 578, 330, 150, 210, ORANGE, S_ORANGE)
    e += zone("CLOUD + OBSERVABILITY", 760, 96, 412, 730, GREEN, S_GREEN)

    e += box("sensor", "BME280 driver\nZephyr sensor API", 55, 160, 220, 72, BLUE, S_BLUE)
    e += box("ring", "sensor_log\nring buffer", 55, 270, 220, 72, TEAL, S_TEAL)
    e += box("power", "power_sched\nactive / idle / sleep", 310, 270, 210, 72, ORANGE, S_ORANGE)
    e += box("cbor", "telemetry_cbor\nCBOR map (keys 0–8)", 55, 380, 220, 72, PURPLE, S_PURPLE)
    e += box("mqtt", "MQTT client\nQoS1 + inbound PUBLISH", 310, 380, 210, 72, BLUE, S_BLUE)
    e += box("ota", "OTA receiver\nframes → slot1", 310, 490, 210, 72, RED, S_RED)
    e += box("fault", "fault_inject\nsensor / flash / drop", 55, 490, 220, 72, YELLOW, S_YELLOW)
    e += box("broker", "Mosquitto\n:8883 TLS", 592, 420, 120, 74, ORANGE, S_ORANGE)

    e += box("ingest", "cloud-ingest\nCBOR validate + metrics", 785, 150, 260, 72, PURPLE, S_PURPLE)
    e += box("prom", "Prometheus\n:9091", 785, 280, 260, 72, GREEN, S_GREEN)
    e += box("grafana", "Grafana\n:3000", 785, 410, 260, 72, PINK, S_PINK)
    e += box("fleet", "fleet-sim\ntest producer", 785, 560, 260, 72, BLUE, S_BLUE)
    e += box("otasim", "ota-sim\nHIL publisher", 785, 680, 260, 72, RED, S_RED)

    e += arrow(165, 232, 165, 270, S_BLUE)
    e += arrow(165, 342, 165, 380, S_TEAL)
    e += arrow(275, 416, 310, 416, S_PURPLE)
    e += arrow(520, 416, 592, 450, S_BLUE)
    e += arrow(712, 450, 785, 186, S_ORANGE, "subscribe")
    e += arrow(915, 222, 915, 280, S_PURPLE, "scrape")
    e += arrow(915, 352, 915, 410, S_GREEN, "query")
    e += arrow(915, 560, 712, 490, S_BLUE, "telemetry")
    e += arrow(785, 680, 712, 500, S_RED)
    e += arrow(592, 470, 520, 526, S_RED)
    # power scheduler triggers the sample callback
    e += arrow(310, 290, 275, 210, S_ORANGE)
    scene(e, "architecture")


def firmware_dataflow():
    e = []
    e.append(text("Firmware Runtime and Data Flow", 370, 24, 28))
    e += zone("WAKE CYCLE", 28, 96, 560, 730, BLUE, S_BLUE)
    e += zone("MQTT + OTA", 620, 96, 552, 730, PURPLE, S_PURPLE)

    e += box("power", "power_sched\nACTIVE → IDLE → SLEEP", 60, 150, 230, 72, ORANGE, S_ORANGE)
    e += box("sample", "sample_and_log\nsensor_sample_fetch", 60, 280, 230, 72, BLUE, S_BLUE)
    e += box("ring", "sensor_log\noverwrite-oldest ring", 60, 410, 230, 72, TEAL, S_TEAL)
    e += box("drain", "drain_and_publish\nconnect + retry slot", 60, 540, 230, 72, ORANGE, S_ORANGE)
    e += box("cbor", "telemetry_cbor\nencode map", 60, 670, 230, 72, PURPLE, S_PURPLE)

    e += box("mqtt", "MQTT client\ntelemetry_process()", 660, 470, 230, 72, BLUE, S_BLUE)
    e += box("broker", "Mosquitto\ntelemetry/<id>", 930, 470, 210, 72, ORANGE, S_ORANGE)
    e += box("ota", "OTA state machine\nstart / data / finish", 660, 300, 230, 72, RED, S_RED)
    e += box("flash", "flash_img\nslot1_partition", 930, 300, 210, 72, TEAL, S_TEAL)
    e += box("swap", "MCUboot\nmark swap", 930, 150, 210, 72, GREEN, S_GREEN)

    e += arrow(175, 222, 175, 280, S_ORANGE, "active callback")
    e += arrow(175, 352, 175, 410, S_BLUE, "reading")
    e += arrow(175, 482, 175, 540, S_TEAL, "drain")
    e += arrow(175, 612, 175, 670, S_ORANGE, "one reading")
    e += arrow(290, 706, 660, 506, S_PURPLE, "publish QoS1")
    e += arrow(890, 506, 930, 506, S_BLUE)
    e += arrow(930, 470, 890, 356, S_RED)
    e += arrow(890, 336, 930, 336, S_RED)
    e += arrow(1035, 300, 1035, 222, S_TEAL)

    e.append(text("sleep-window service: telemetry_process() every 100 ms", 660, 600, 16, MUTED, width=500, align="left"))
    e.append(text("fault_inject hooks: sensor / flash / publish (tests only)", 60, 770, 16, MUTED, width=500, align="left"))
    scene(e, "firmware-dataflow")


def demo_topology():
    e = []
    e.append(text("Local Demo / Validation Topology", 340, 24, 28))
    e += zone("HOST / CI", 28, 96, 470, 730, BLUE, S_BLUE)
    e += zone("DOCKER COMPOSE STACK", 545, 96, 625, 730, PURPLE, S_PURPLE)

    e += box("browser", "Browser\nGrafana UI", 60, 150, 210, 72, PINK, S_PINK)
    e += box("fleet", "fleet-sim\n5–50 devices", 60, 280, 210, 72, BLUE, S_BLUE)
    e += box("otasim", "ota-sim\nimage publisher", 60, 410, 210, 72, RED, S_RED)
    e += box("e2e", "e2e runner\nasserts /metrics", 60, 540, 210, 72, ORANGE, S_ORANGE)
    e += box("note", "native_sim firmware\nneeds TAP/bridge for live MQTT\n(golden CBOR test covers bytes)",
             60, 670, 360, 90, YELLOW, S_YELLOW, size=16)

    e += box("mosq", "Mosquitto\n:8883 TLS", 600, 150, 210, 72, ORANGE, S_ORANGE)
    e += box("cloud", "cloud-ingest\n:9090 /metrics /health", 600, 280, 230, 72, PURPLE, S_PURPLE)
    e += box("prom", "Prometheus\n:9091", 600, 410, 210, 72, GREEN, S_GREEN)
    e += box("graf", "Grafana\n:3000", 890, 410, 210, 72, PINK, S_PINK)
    e += box("vol", "named volumes\nbroker + metrics", 890, 280, 210, 72, TEAL, S_TEAL)

    e += arrow(270, 316, 600, 190, S_BLUE, "CBOR/QoS1")
    e += arrow(270, 446, 600, 220, S_RED, "OTA frames")
    e += arrow(705, 222, 705, 280, S_ORANGE)
    e += arrow(715, 352, 705, 410, S_PURPLE)
    e += arrow(810, 446, 890, 446, S_GREEN)
    scene(e, "demo-topology")


def ota_flow():
    e = []
    e.append(text("OTA Update Flow — cmd/<device_id>/ota", 315, 24, 28))
    e += zone("PUBLISHER", 28, 96, 340, 730, BLUE, S_BLUE)
    e += zone("BROKER", 378, 96, 240, 730, ORANGE, S_ORANGE)
    e += zone("DEVICE", 628, 96, 540, 730, PURPLE, S_PURPLE)

    e += box("p1", "start frame\n01 + size", 55, 160, 210, 72, BLUE, S_BLUE)
    e += box("p2", "data frames\n02 + chunk", 55, 290, 210, 72, BLUE, S_BLUE)
    e += box("p3", "finish frame\n03", 55, 420, 210, 72, BLUE, S_BLUE)
    e += box("p4", "abort on error\n04", 55, 550, 210, 72, RED, S_RED)
    e += box("b1", "Mosquitto\nQoS1", 405, 290, 180, 72, ORANGE, S_ORANGE)
    e += box("d1", "ota_handle_frame\nvalidate", 660, 160, 230, 72, PURPLE, S_PURPLE)
    e += box("d2", "flash_img\nslot1", 930, 160, 210, 72, TEAL, S_TEAL)
    e += box("d3", "finish:\nsize match", 660, 320, 230, 72, PURPLE, S_PURPLE)
    e += box("d4", "boot_request_upgrade\nMCUboot swap", 930, 320, 210, 72, GREEN, S_GREEN)
    e += box("d5", "reboot → validate\nswap succeeds", 930, 480, 210, 90, GREEN, S_GREEN)

    e += arrow(265, 196, 405, 326, S_BLUE, "publish")
    e += arrow(265, 326, 405, 326, S_BLUE)
    e += arrow(265, 456, 405, 326, S_BLUE)
    e += arrow(585, 326, 660, 196, S_ORANGE, "deliver")
    e += arrow(585, 326, 660, 356, S_ORANGE)
    e += arrow(890, 196, 930, 196, S_PURPLE)
    e += arrow(890, 356, 930, 356, S_PURPLE)
    e += arrow(1035, 392, 1035, 480, S_GREEN, "reboot")
    e += arrow(265, 586, 405, 360, S_RED, "abort", dashed=True)
    scene(e, "ota-flow")


if __name__ == "__main__":
    architecture()
    firmware_dataflow()
    demo_topology()
    ota_flow()
