#!/usr/bin/env python3
"""Generate the breadboard-style wiring diagram (docs/images/wiring.svg).

Usage: python3 tools/gen_wiring.py docs/images/wiring.svg

Standard library only. Coordinates are hand-placed so that wires cross as
little as possible; every crossing is drawn with a white halo, and only
junction dots mean "connected". Re-run after any change to the pinout and
check the result in a browser before committing.
"""
import sys

W, H = 1500, 1185
out = []
FONT = "Helvetica, Arial, sans-serif"

C = {
    "12v": "#d62728", "5v": "#c2185b", "3v3": "#ff8c00", "gnd": "#222222",
    "cs": "#8c564b", "sck": "#d4a900", "miso": "#2ca02c", "mosi": "#1f77b4",
    "rst": "#bcbd22", "int": "#9a9a9a",
    "zero": "#17becf", "buzz": "#e377c2", "btn": "#6a3d9a",
    "step": "#2ca02c", "dir": "#1f77b4", "en": "#7f7f7f", "uart": "#d4a900",
    "m_a1": "#222222", "m_a2": "#2ca02c", "m_b1": "#d62728", "m_b2": "#1f77b4",
}


def e(s):
    out.append(s)


def text(x, y, s, size=11, fill="#222", anchor="start", weight="normal", rot=None, italic=False, halo=False):
    t = f' transform="rotate({rot} {x} {y})"' if rot is not None else ""
    st = ' font-style="italic"' if italic else ""
    if halo:
        st += ' stroke="#fff" stroke-width="3" paint-order="stroke"'
    e(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" '
      f'font-weight="{weight}"{st}{t}>{s}</text>')


def wire(pts, color, w=2.5, dash=None):
    d = "M" + " L".join(f"{x},{y}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    e(f'<path d="{d}" stroke="#fff" stroke-width="{w + 4}" fill="none" stroke-linejoin="round"/>')
    e(f'<path d="{d}" stroke="{color}" stroke-width="{w}" fill="none" stroke-linejoin="round"{da}/>')


def rail(pts, color):
    d = "M" + " L".join(f"{x},{y}" for x, y in pts)
    e(f'<path d="{d}" stroke="{color}" stroke-width="4" fill="none" stroke-linejoin="round"/>')


def dot(x, y, color):
    e(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}" stroke="#fff" stroke-width="1.2"/>')


def box(x, y, w, h, fill, rx=6):
    e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="#00000055"/>')


def pin(x, y):
    e(f'<rect x="{x - 4}" y="{y - 4}" width="8" height="8" fill="#e8c34a" stroke="#7a5c00" stroke-width="1"/>')


def pins_edge(pins, y, edge, color="#fff"):
    """Header pins along a module edge, labels rotated into the board."""
    for x, name in pins:
        pin(x, y)
        if edge == "top":
            text(x + 3.5, y + 9, name, 9, color, "end", rot=-90)
        else:
            text(x + 3.5, y - 9, name, 9, color, "start", rot=-90)


e(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">')
e(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
text(24, 36, "single-cell-braille-controller — wiring", 20, weight="bold")
text(24, 58, "Breakout-board level. Pin order on your modules may differ from the drawing: match by the printed label.", 12, "#555")

# ---------------------------------------------------------------- rails
V5_TOP, GND_TOP, V33_TOP = 75, 90, 105
V12_MID, V33_MID, GND_MID = 670, 685, 700
X5, XG, X33, X12 = 210, 225, 240, 255
V5_END = 1100
RAIL_END_TOP, RAIL_END_MID = 1420, 1225
rail([(RAIL_END_TOP, GND_TOP), (XG, GND_TOP), (XG, GND_MID), (RAIL_END_MID, GND_MID)], C["gnd"])
rail([(RAIL_END_TOP, V33_TOP), (X33, V33_TOP), (X33, V33_MID), (RAIL_END_MID, V33_MID)], C["3v3"])
rail([(X12, 480), (X12, V12_MID), (RAIL_END_MID, V12_MID)], C["12v"])
rail([(X5, 500), (X5, V5_TOP), (V5_END, V5_TOP)], C["5v"])
text(V5_END + 8, V5_TOP + 4, "+5V", 11, C["5v"], weight="bold")
text(RAIL_END_TOP + 8, GND_TOP + 4, "GND", 11, C["gnd"], weight="bold")
text(RAIL_END_TOP + 8, V33_TOP + 4, "+3V3", 11, C["3v3"], weight="bold")
text(RAIL_END_MID + 8, V12_MID + 4, "+12V", 11, C["12v"], weight="bold")
text(RAIL_END_MID + 8, V33_MID + 4, "+3V3", 11, C["3v3"], weight="bold")
text(RAIL_END_MID + 8, GND_MID + 4, "GND", 11, C["gnd"], weight="bold")

# ---------------------------------------------------------------- wires
# Blue Pill pin x positions (USB end on the left).
TOP = ["B12", "B13", "B14", "B15", "A8", "A9", "A10", "A11", "A12", "A15",
       "B3", "B4", "B5", "B6", "B7", "B8", "B9", "5V", "GND", "3V3"]
BOT = ["VB", "C13", "C14", "C15", "A0", "A1", "A2", "A3", "A4", "A5",
       "A6", "A7", "B0", "B1", "B10", "B11", "R", "3V3", "GND", "GND"]
BP_X0, PITCH = 460, 26
BP_TOP_Y, BP_BOT_Y = 478, 582


def tx(name):
    return BP_X0 + PITCH * TOP.index(name)


def bx(name, nth=0):
    idx = [i for i, n in enumerate(BOT) if n == name][nth]
    return BP_X0 + PITCH * idx


# AMS1117 -> rails
wire([(182, 540), (XG, 540)], C["gnd"]); dot(XG, 540, C["gnd"])
wire([(182, 520), (X33, 520)], C["3v3"]); dot(X33, 520, C["3v3"])
wire([(182, 500), (X5, 500)], C["5v"]); dot(X5, 500, C["5v"])
wire([(182, 480), (X12, 480)], C["12v"]); dot(X12, 480, C["12v"])

# W5500 (bottom pins) -> Blue Pill top row
W5 = {"CS": 305, "SCK": 333, "MISO": 361, "MOSI": 389, "RST": 417, "INT": 445}
W5_BOT = 330
for name, bp, lvl, col in [("CS", "B12", 440, "cs"), ("SCK", "B13", 430, "sck"),
                           ("MISO", "B14", 420, "miso"), ("MOSI", "B15", 410, "mosi"),
                           ("RST", "A10", 400, "rst"), ("INT", "B5", 390, "int")]:
    wire([(W5[name], W5_BOT), (W5[name], lvl), (tx(bp), lvl), (tx(bp), BP_TOP_Y)], C[col],
         dash="6 4" if name == "INT" else None)
# W5500 power
wire([(330, 150), (330, V33_TOP)], C["3v3"]); dot(330, V33_TOP, C["3v3"])
wire([(360, 150), (360, GND_TOP)], C["gnd"]); dot(360, GND_TOP, C["gnd"])

# Slot sensors
S2 = dict(x=480, y=140, w=150, h=60, vcc=500, gnd=525, d0=tx("A8"), a0=590)
S1 = dict(x=580, y=232, w=165, h=60, vcc=680, gnd=705, d0=tx("A9"), a0=616)
for s in (S2, S1):
    wire([(s["d0"], s["y"] + s["h"]), (s["d0"], BP_TOP_Y)], C["zero"])
    wire([(s["vcc"], s["y"]), (s["vcc"], V33_TOP)], C["3v3"]); dot(s["vcc"], V33_TOP, C["3v3"])
    wire([(s["gnd"], s["y"]), (s["gnd"], GND_TOP)], C["gnd"]); dot(s["gnd"], GND_TOP, C["gnd"])

# Buzzer + buttons (top right)
# Buzzer driver: PB6 -> 1 kOhm -> BC368 base; emitter -> GND; collector ->
# buzzer (-); buzzer (+) -> +5V.
BZ_X = 1066                       # buzzer / collector / emitter column
BASE_Y, BASE_X = 258, 1050        # base lead end
C_Y, E_Y = 236, 280               # collector / emitter terminals
R_X0, R_X1 = 998, 1026            # base resistor body
E_GND_X = 1112                    # emitter return column to the GND rail
wire([(tx("B6"), BP_TOP_Y), (tx("B6"), 320), (985, 320), (985, BASE_Y), (R_X0, BASE_Y)], C["buzz"])
wire([(R_X1, BASE_Y), (BASE_X, BASE_Y)], C["buzz"])
wire([(BZ_X, 210), (BZ_X, C_Y)], C["buzz"])  # buzzer (-) to collector, not ground
wire([(BZ_X, E_Y), (BZ_X, 296), (E_GND_X, 296), (E_GND_X, GND_TOP)], C["gnd"]); dot(E_GND_X, GND_TOP, C["gnd"])
wire([(BZ_X, 162), (BZ_X, V5_TOP)], C["5v"]); dot(BZ_X, V5_TOP, C["5v"])

UI = [("B7", 1170, 355, "btn"), ("B8", 1280, 370, "btn"), ("B9", 1390, 385, "btn")]
for bp, x, lvl, col in UI:
    wire([(tx(bp), BP_TOP_Y), (tx(bp), lvl), (x, lvl), (x, 272)], C[col])
    wire([(x, 208), (x, GND_TOP)], C["gnd"]); dot(x, GND_TOP, C["gnd"])

# Blue Pill power from the mid rails (bottom-row 3V3/GND)
wire([(bx("3V3"), BP_BOT_Y), (bx("3V3"), V33_MID)], C["3v3"]); dot(bx("3V3"), V33_MID, C["3v3"])
wire([(bx("GND"), BP_BOT_Y), (bx("GND"), GND_MID)], C["gnd"]); dot(bx("GND"), GND_MID, C["gnd"])

# TMC2209 logic wiring. TX reaches PDN_UART through 1 kOhm, RX directly.
TMC_TOP = 770
J_Y = 650


def resistor(x):
    e(f'<rect x="{x - 5}" y="604" width="10" height="30" rx="4" fill="#e9d3a6" stroke="#8a6d3b"/>')
    for yy, c in ((610, "#8b4513"), (616, "#111"), (622, "#d62728")):
        e(f'<rect x="{x - 5}" y="{yy}" width="10" height="3" fill="{c}"/>')


T1 = dict(VM=305, GNDm=331, VIO=357, GND=383, MS1=409, MS2=435, STEP=461, PDN=487, DIR=513, EN=539)
T2 = dict(DIR=955, EN=981, STEP=1007, PDN=1033, MS1=1059, MS2=1085, GND=1111, VIO=1137, GNDm=1163, VM=1189)

# motor 01: STEP A1, TX A2, RX A3, DIR A4, EN A5
wire([(bx("A1"), BP_BOT_Y), (bx("A1"), 712), (T1["STEP"], 712), (T1["STEP"], TMC_TOP)], C["step"])
wire([(bx("A2"), BP_BOT_Y), (bx("A2"), J_Y), (bx("A3"), J_Y)], C["uart"])
wire([(bx("A3"), BP_BOT_Y), (bx("A3"), 722), (T1["PDN"], 722), (T1["PDN"], TMC_TOP)], C["uart"])
wire([(bx("A4"), BP_BOT_Y), (bx("A4"), 732), (T1["DIR"], 732), (T1["DIR"], TMC_TOP)], C["dir"])
wire([(bx("A5"), BP_BOT_Y), (bx("A5"), 742), (T1["EN"], 742), (T1["EN"], TMC_TOP)], C["en"])
dot(bx("A3"), J_Y, C["uart"]); resistor(bx("A2"))
# motor 02: DIR A7, EN B0, STEP B1, TX B10, RX B11
wire([(bx("A7"), BP_BOT_Y), (bx("A7"), 752), (T2["DIR"], 752), (T2["DIR"], TMC_TOP)], C["dir"])
wire([(bx("B0"), BP_BOT_Y), (bx("B0"), 742), (T2["EN"], 742), (T2["EN"], TMC_TOP)], C["en"])
wire([(bx("B1"), BP_BOT_Y), (bx("B1"), 732), (T2["STEP"], 732), (T2["STEP"], TMC_TOP)], C["step"])
wire([(bx("B10"), BP_BOT_Y), (bx("B10"), J_Y), (bx("B11"), J_Y)], C["uart"])
wire([(bx("B11"), BP_BOT_Y), (bx("B11"), 722), (T2["PDN"], 722), (T2["PDN"], TMC_TOP)], C["uart"])
dot(bx("B11"), J_Y, C["uart"]); resistor(bx("B10"))
text(bx("A2") - 8, 624, "1 kΩ", 9, "#555", "end", halo=True)
text(bx("B10") - 8, 624, "1 kΩ", 9, "#555", "end", halo=True)

# TMC power + MS1/MS2 to the mid rails
for T in (T1, T2):
    for key, y, col in (("VM", V12_MID, "12v"), ("GNDm", GND_MID, "gnd"), ("VIO", V33_MID, "3v3"),
                        ("GND", GND_MID, "gnd"), ("MS1", GND_MID, "gnd"), ("MS2", GND_MID, "gnd")):
        wire([(T[key], TMC_TOP), (T[key], y)], C[col]); dot(T[key], y, C[col])

# TMC -> motors
MOT_Y = 975
for T, xs in ((T1, (385, 410, 435, 460)), (T2, (1035, 1060, 1085, 1110))):
    for x, col in zip(xs, ("m_a1", "m_a2", "m_b1", "m_b2")):
        wire([(x, 906), (x, MOT_Y)], C[col])

# ---------------------------------------------------------------- modules
# AMS1117 breakout with barrel jack
box(40, 415, 150, 150, "#b03a2e")
e('<rect x="12" y="495" width="34" height="40" rx="4" fill="#111"/>')
e('<circle cx="24" cy="515" r="7" fill="#444"/>')
text(54, 519, "← 12 V", 10, "#fff", weight="bold")
text(54, 532, "DC in", 10, "#fff", weight="bold")
text(115, 436, "Power breakout", 12, "#fff", "middle", "bold")
text(115, 452, "AMS1117 · 12 V / 5 V / 3.3 V", 9, "#fde", "middle")
for y, n in ((480, "12V OUT"), (500, "5V"), (520, "3V3"), (540, "GND")):
    pin(182, y); text(174, y + 4, n, 10, "#fff", "end")
text(115, 586, "LDO burns ≈(12−3.3)·I", 10, "#a33", "middle", italic=True)
text(115, 600, "— it gets warm", 10, "#a33", "middle", italic=True)

# W5500
box(290, 150, 175, 180, "#2b2b2b")
e('<rect x="268" y="200" width="30" height="60" rx="3" fill="#c9c9c9" stroke="#777"/>')
text(380, 222, "W5500", 14, "#fff", "middle", "bold")
text(380, 238, "Ethernet module", 10, "#ccc", "middle")
text(380, 252, "SPI2 · RJ45 to the PLC", 10, "#ccc", "middle")
pins_edge([(330, "3V3"), (360, "GND")], 150 + 4, "top")
pins_edge([(x, n) for n, x in W5.items()], W5_BOT - 4, "bottom")

# Sensors
for s, title, sub in ((S2, "Slot sensor · ZERO 02", "flag on disc 2"), (S1, "Slot sensor · ZERO 01", "flag on disc 1")):
    box(s["x"], s["y"], s["w"], s["h"], "#1d4e9e")
    if s is S2:
        text(s["x"], s["y"] - 20, title, 11, "#222", weight="bold", halo=True)
        text(s["x"], s["y"] - 7, "LM393 module · " + sub, 9, "#555", halo=True)
    else:
        text(s["x"] + s["w"] + 10, s["y"] + 26, title, 11, "#222", weight="bold", halo=True)
        text(s["x"] + s["w"] + 10, s["y"] + 40, "LM393 module · " + sub, 9, "#555", halo=True)
    pins_edge([(s["vcc"], "VCC"), (s["gnd"], "GND")], s["y"] + 4, "top")
    pins_edge([(s["d0"], "D0"), (s["a0"], "A0")], s["y"] + s["h"] - 4, "bottom")
    text(s["a0"], s["y"] + s["h"] + 14, "nc", 9, "#999", "middle", italic=True)

# Buzzer
e(f'<circle cx="{BZ_X}" cy="186" r="24" fill="#151515"/>')
e(f'<circle cx="{BZ_X}" cy="186" r="4" fill="#444"/>')
text(BZ_X, 176, "+", 12, "#fff", "middle", "bold")
text(BZ_X, 205, "−", 12, "#fff", "middle", "bold")
text(BZ_X - 32, 182, "Passive piezo", 11, "#222", "end", "bold", halo=True)
text(BZ_X - 32, 196, "+ to +5V, − to collector", 9, "#555", "end", halo=True)
# base resistor
e(f'<rect x="{R_X0}" y="{BASE_Y - 5}" width="{R_X1 - R_X0}" height="10" rx="4" fill="#e9d3a6" stroke="#8a6d3b"/>')
for dx, c in ((6, "#8b4513"), (12, "#111"), (18, "#d62728")):
    e(f'<rect x="{R_X0 + dx}" y="{BASE_Y - 5}" width="3" height="10" fill="{c}"/>')
text((R_X0 + R_X1) / 2, BASE_Y - 10, "1 kΩ", 9, "#555", "middle", halo=True)
# BC368 NPN symbol
e(f'<circle cx="{BZ_X - 4}" cy="{BASE_Y}" r="17" fill="#fff" stroke="#222" stroke-width="1.5"/>')
e(f'<line x1="{BASE_X}" y1="{BASE_Y - 10}" x2="{BASE_X}" y2="{BASE_Y + 10}" stroke="#222" stroke-width="2.5"/>')
e(f'<line x1="{BASE_X}" y1="{BASE_Y - 4}" x2="{BZ_X}" y2="{C_Y}" stroke="#222" stroke-width="1.8"/>')
e(f'<line x1="{BASE_X}" y1="{BASE_Y + 4}" x2="{BZ_X}" y2="{E_Y}" stroke="#222" stroke-width="1.8"/>')
e(f'<polygon points="{BZ_X},{E_Y} {BZ_X - 9},{E_Y - 3} {BZ_X - 4},{E_Y - 10}" fill="#222"/>')
text(BZ_X + 20, BASE_Y - 2, "BC368", 11, "#222", weight="bold", halo=True)
text(BZ_X + 20, BASE_Y + 11, "NPN", 9, "#555", halo=True)
text(BZ_X + 6, C_Y + 2, "C", 8, "#555", halo=True)
text(BZ_X + 6, E_Y + 2, "E", 8, "#555", halo=True)
text(BASE_X - 4, BASE_Y + 22, "B", 8, "#555", halo=True)
text(990, 334, "PB6 · TIM4_CH1 PWM", 9, "#555", "end", halo=True)

# Buttons
for x, n, p in ((1170, "REPEAT", "PB7"), (1280, "NEXT", "PB8"), (1390, "PREV", "PB9")):
    e(f'<rect x="{x - 22}" y="{218}" width="44" height="44" rx="4" fill="#3a3a3a"/>')
    e(f'<circle cx="{x}" cy="240" r="13" fill="#111" stroke="#666"/>')
    pin(x, 212); pin(x, 268)
    text(x + 28, 236, n, 11, "#222", weight="bold")
    text(x + 28, 250, p, 10, "#555")

# Blue Pill
box(430, 465, 560, 130, "#1f4fa8", 8)
e('<rect x="408" y="510" width="26" height="40" rx="3" fill="#c9c9c9" stroke="#777"/>')
text(400, 534, "USB", 9, "#555", "end")
text(710, 525, "Blue Pill · STM32F103C8", 15, "#fff", "middle", "bold")
text(710, 543, "power from +3V3 pin — keep USB unplugged while the 12 V supply is on", 10, "#cfe0ff", "middle")
for i, n in enumerate(TOP):
    pin(BP_X0 + PITCH * i, BP_TOP_Y)
    text(BP_X0 + PITCH * i, BP_TOP_Y + 18, n, 9, "#fff", "middle")
for i, n in enumerate(BOT):
    pin(BP_X0 + PITCH * i, BP_BOT_Y)
    text(BP_X0 + PITCH * i, BP_BOT_Y - 10, n, 9, "#fff", "middle")
e(f'<circle cx="{bx("C13")}" cy="552" r="4" fill="#7CFC00"/>')
text(bx("C13") + 8, 556, "LED (onboard)", 8, "#cfe0ff")
text(985, 510, "SWD →", 9, "#cfe0ff", "end")
text(985, 522, "ST-Link", 9, "#cfe0ff", "end")

# TMC2209 boards
for T, x0, title, sub in ((T1, 290, "TMC2209 · motor 01", "USART2 · STEP TIM2_CH2"),
                          (T2, 940, "TMC2209 · motor 02", "USART3 · STEP TIM3_CH4")):
    box(x0, 770, 270, 140, "#6b2fa0")
    names = {"GNDm": "GND", "PDN": "PDN", "VIO": "VCC_IO"}
    pins_edge([(x, names.get(k, k)) for k, x in T.items()], 774, "top")
    xs = (385, 410, 435, 460) if T is T1 else (1035, 1060, 1085, 1110)
    pins_edge(list(zip(xs, ("A1", "A2", "B1", "B2"))), 906, "bottom")
    text(x0 + 135, 854, title, 13, "#fff", "middle", "bold")
    text(x0 + 135, 870, sub, 10, "#e5d4f5", "middle")
text(280, 846, "MS1 = MS2 = GND", 10, "#555", "end", halo=True)
text(280, 860, "→ UART address 0", 10, "#555", "end", halo=True)
text(280, 874, "(both drivers)", 10, "#555", "end", halo=True)
text(280, 896, "PDN = J1 pin 4, the UART", 10, "#555", "end", halo=True)
text(280, 910, "pin by factory default", 10, "#555", "end", halo=True)

# Motors
for x0, title, sub in ((352, "NEMA17 · motor 01", "disc 1 · dots 1, 4"), (1002, "NEMA17 · motor 02", "disc 2 · dots 2, 3, 5, 6")):
    e(f'<rect x="{x0}" y="{MOT_Y}" width="140" height="140" rx="10" fill="#8d939b" stroke="#555"/>')
    e(f'<circle cx="{x0 + 70}" cy="{MOT_Y + 75}" r="34" fill="#6c7279"/>')
    e(f'<circle cx="{x0 + 70}" cy="{MOT_Y + 75}" r="7" fill="#ddd"/>')
    for dx, dy in ((14, 14), (126, 14), (14, 126), (126, 126)):
        e(f'<circle cx="{x0 + dx}" cy="{MOT_Y + dy}" r="5" fill="#555"/>')
    text(x0 - 12, MOT_Y + 60, title, 12, "#222", "end", "bold")
    text(x0 - 12, MOT_Y + 76, sub, 10, "#555", "end")
    text(x0 - 12, MOT_Y + 92, "coil A → A1/A2 · coil B → B1/B2", 10, "#555", "end")

# ---------------------------------------------------------------- legend
LX, LY = 1270, 770
e(f'<rect x="{LX - 12}" y="{LY - 24}" width="222" height="420" rx="6" fill="#f7f7f7" stroke="#ddd"/>')
text(LX, LY - 4, "Legend", 12, "#222", weight="bold")
items = [("12v", "+12 V (motor supply)"), ("5v", "+5 V (buzzer only)"), ("3v3", "+3.3 V (logic)"), ("gnd", "GND (common)"),
         ("cs", "SPI CS (PB12)"), ("sck", "SPI SCK / UART"), ("miso", "SPI MISO / STEP"),
         ("mosi", "SPI MOSI / DIR"), ("rst", "W5500 RST (PA10)"), ("int", "W5500 INT (PB5, unused)"),
         ("en", "Driver EN"), ("zero", "ZERO sensor D0"), ("btn", "Buttons"), ("buzz", "Buzzer drive (PB6)")]
for i, (k, n) in enumerate(items):
    y = LY + 14 + i * 19
    da = ' stroke-dasharray="6 4"' if k == "int" else ""
    e(f'<line x1="{LX}" y1="{y}" x2="{LX + 26}" y2="{y}" stroke="{C[k]}" stroke-width="{4 if k in ("12v", "5v", "3v3", "gnd") else 2.5}"{da}/>')
    text(LX + 34, y + 4, n, 10)
y = LY + 14 + len(items) * 19
dot(LX + 13, y, "#555"); text(LX + 34, y + 4, "junction (connected)", 10)
for i, n in enumerate(("Crossing wires are not connected.", "Buttons close to GND and use the", "MCU's internal pull-ups.", "The BC368 switches the piezo's", "low side (emitter to GND).", "Sensor A0 pins stay unconnected.")):
    text(LX, y + 24 + i * 13, n, 9, "#777", italic=True)

e("</svg>")
open(sys.argv[1], "w").write("\n".join(out))
