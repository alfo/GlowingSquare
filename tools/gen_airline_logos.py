#!/usr/bin/env python3
"""Turn a curated list of airlines into src/project/airline_logos.cpp: one
16x16 RGB565 mark per airline, on black, for the Flights mode.

The marks come from pics.avs.io's square logos (64 px), and each is made to
sit on black: the logo's ground, white or the airline's tile colour, is
taken out (colour to alpha, so the edges stay smooth), and a white mark that
stood on a tile takes the tile's colour instead: easyJet's white "e" on
orange becomes an orange "e". A row's own `tint` or `keep` overrides that.

The list is the airlines over west London (a two-minute sample within 50 nm
of Notting Hill Gate on 12.11.2026, BA, Ryanair and easyJet far ahead) and
Heathrow's regular carriers. An airline not on it gets a plain plane icon.
Each row's callsign prefixes (ICAO) all share its mark: BA's Shuttle,
CityFlyer and Euroflyer are BA's.

Needs Pillow. The logos are downloaded once into tools/.logo-cache (not
committed); the generated file is committed, so a build needs nothing but
PlatformIO. --sheet PATH also writes an LED-style picture of every mark.

    python3 tools/gen_airline_logos.py [--sheet logos.png]
"""
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
cache = root / "tools/.logo-cache"
out_cpp = root / "src/project/airline_logos.cpp"

SIZE = 16          # the mark's side on the panel, in pixels
SOURCE = 64        # the size asked of pics.avs.io
MIN_ALPHA = 0.2    # less of the mark than this is the ground showing through
GAMMA = 2.2        # the logos' colours are for a screen; the panel is linear
URL = "https://pics.avs.io/al_square/{n}/{n}/{iata}.png"

# Drawn here, for marks that do not survive 16 px: BA's speedmarque is a
# ribbon two pixels tall, United's globe is noise, Vueling's and Wizz's are
# wordmarks.
BA_ART = dict(palette={"R": "#eb2226", "r": "#8c1418", "B": "#2a6ad8", "b": "#163a78"}, rows=[
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..rrRRRRRRRRRRRR",
    "rRRRRRRRRRRRRRRR",
    "..rrRRRRRRRRRRRR",
    "..........bBBBBB",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
])
UA_ART = dict(palette={"B": "#4c8dff", "b": "#2a56b0"}, rows=[
    "......BBBB......",
    "....BBBbbBBB....",
    "..BB.bb..bb.BB..",
    "..B..b....b..B..",
    ".BbbbbbbbbbbbbB.",
    ".B...b....b...B.",
    "BB..b......b..BB",
    "BbbbbbbbbbbbbbbB",
    "B...b......b...B",
    "BB..b......b..BB",
    ".B...b....b...B.",
    ".BbbbbbbbbbbbbB.",
    "..B..b....b..B..",
    "..BB.bb..bb.BB..",
    "....BBBbbBBB....",
    "......BBBB......",
])
VY_ART = dict(palette={"Y": "#ffcc00", "y": "#806600"}, rows=[
    "................",
    "................",
    "............YY..",
    "............YY..",
    "................",
    "YY.......YY.....",
    "YY.......YY.....",
    "yYY.....YYy.....",
    ".YY.....YY......",
    ".yYY...YYy......",
    "..YY...YY.......",
    "..yYY.YYy.......",
    "...YYYYY........",
    "....YYY.........",
    "................",
    "................",
])
W6_ART = dict(palette={"M": "#ff1f9a", "P": "#c41fd0", "p": "#7a1488"}, rows=[
    "................",
    "................",
    "................",
    "MM.....MM.....MM",
    "MM.....MM.....MM",
    "MM....MMMM....MM",
    "MM....MMMM....MM",
    ".PP..PP..PP..PP.",
    ".PP..PP..PP..PP.",
    ".PP.PPp..pPP.PP.",
    "..PPPP....PPPP..",
    "..pPPp....pPPp..",
    "...PP......PP...",
    "................",
    "................",
    "................",
])

# IATA code (the logo's name at pics.avs.io), callsign prefixes, options:
#   tint="#rrggbb"  draw the mark in this colour
#   keep=True       keep the mark's own colours even after a tile
#   crop=(x0, y0, x1, y1)  only this part of the 64 px logo
#   art=...         drawn here instead (above)
AIRLINES = [
    ("BA", "BAW SHT CFE EFW", {"art": BA_ART}),
    ("FR", "RYR RUK MAY LDM", {}),
    ("U2", "EZY EJU EZS", {}),
    ("VS", "VIR", {}),
    ("VY", "VLG", {"art": VY_ART}),
    ("W6", "WZZ WUK WMT", {"art": W6_ART}),
    ("LS", "EXS", {}),
    ("BY", "TOM", {}),
    ("EI", "EIN EUK", {}),
    ("AF", "AFR", {}),
    ("KL", "KLM KLC", {}),
    ("LH", "DLH", {"tint": "#ffad00"}),
    ("EW", "EWG", {}),
    ("LX", "SWR", {}),
    ("OS", "AUA", {}),
    ("SN", "BEL", {}),
    ("SK", "SAS", {}),
    ("AY", "FIN", {}),
    ("IB", "IBE IBS", {}),
    ("TP", "TAP", {}),
    ("AZ", "ITY", {}),
    ("LO", "LOT", {}),
    ("A3", "AEE", {}),
    ("TK", "THY", {}),
    ("LM", "LOG", {}),
    ("DY", "NOZ NAX NSZ", {"tint": "#e8303c"}),
    ("FI", "ICE", {}),
    ("EK", "UAE", {"crop": (14, 6, 50, 36)}),
    ("QR", "QTR", {"tint": "#c8406e"}),
    ("EY", "ETD", {}),
    ("SQ", "SIA", {}),
    ("CX", "CPA", {}),
    ("AI", "AIC", {}),
    ("TG", "THA", {}),
    ("QF", "QFA", {}),
    ("JL", "JAL", {}),
    ("NH", "ANA", {}),
    ("KE", "KAL", {}),
    ("AA", "AAL", {}),
    ("UA", "UAL", {"art": UA_ART}),
    ("DL", "DAL", {}),
    ("AC", "ACA", {}),
    ("B6", "JBU", {}),
    ("AS", "ASA", {}),
    ("AM", "AMX", {}),
    ("RJ", "RJA", {}),
    ("LY", "ELY", {}),
    ("GF", "GFA", {}),
    ("WY", "OMA", {}),
    ("SV", "SVA", {}),
    ("MS", "MSR", {}),
    ("ET", "ETH", {}),
    ("PK", "PIA", {}),
    ("CA", "CCA", {}),
    ("MU", "CES", {}),
    ("BR", "EVA", {}),
]


def fetch(iata):
    cache.mkdir(exist_ok=True)
    path = cache / f"{iata}.png"
    if not path.exists():
        req = urllib.request.Request(URL.format(n=SOURCE, iata=iata),
                                     headers={"User-Agent": "gen_airline_logos"})
        with urllib.request.urlopen(req, timeout=20) as r:
            path.write_bytes(r.read())
    return Image.open(path).convert("RGB")


def ground(im):
    """The logo's ground: the colour most of its corners have. Most, not the
    average: Qatar's oryx reaches one corner, and an average with it left
    the maroon tile standing."""
    w, h = im.size
    pts = [im.getpixel((x, y)) for x in (0, 1, w - 2, w - 1) for y in (0, 1, h - 2, h - 1)]
    return max(set(pts), key=pts.count)


def to_alpha(p, bg):
    """GIMP's colour to alpha: how much of p is not bg, and what it is."""
    a = 0.0
    for c, b in zip(p, bg):
        if c > b:
            a = max(a, (c - b) / (255 - b) if b < 255 else 0)
        elif c < b:
            a = max(a, (b - c) / b if b > 0 else 0)
    if a <= 0:
        return 0.0, (0, 0, 0)
    return a, tuple(min(255, max(0, (c - b) / a + b)) for c, b in zip(p, bg))


def saturation(c):
    hi, lo = max(c), min(c)
    return 0 if hi == 0 else (hi - lo) / hi


def drawn(art):
    out = Image.new("RGB", (SIZE, SIZE))
    pal = {k: tuple(int(v[i:i + 2], 16) for i in (1, 3, 5)) for k, v in art["palette"].items()}
    for y, row in enumerate(art["rows"]):
        assert len(row) == SIZE, row
        for x, ch in enumerate(row):
            if ch != ".":
                out.putpixel((x, y), pal[ch])
    return out


def led(im):
    """Screen colours to the panel's. The panel lights an LED for as long as
    its value says, so a value is light, not a screen's gamma-encoded level:
    BA's red (235, 34, 38) lit its green and blue a sixth of the time and
    read as pink. Through the gamma it is (214, 3, 4), red."""
    return im.point(lambda v: int(round(255 * (v / 255) ** GAMMA)))


def mark(iata, opts):
    """The logo as a 16x16 RGB image on black, in the panel's colours."""
    if "art" in opts:
        return led(drawn(opts["art"]))
    src = fetch(iata)
    if "crop" in opts:
        bg_src = src
        src = src.crop(opts["crop"])
        bg = ground(bg_src)
    else:
        bg = ground(src)
    tile = saturation(bg) > 0.25 or max(bg) < 200    # a coloured ground, not white
    w, h = src.size
    rgba = Image.new("RGBA", (w, h))
    grey = []
    for y in range(h):
        for x in range(w):
            a, c = to_alpha(src.getpixel((x, y)), bg)
            if a < MIN_ALPHA:
                a, c = 0.0, (0, 0, 0)
            rgba.putpixel((x, y), (int(c[0]), int(c[1]), int(c[2]), int(a * 255)))
            if a > 0.5:
                grey.append(saturation(c) < 0.2 and min(c) > 150)
    tint = opts.get("tint")
    if not tint and tile and not opts.get("keep") and grey and sum(grey) > 0.6 * len(grey):
        tint = "#%02x%02x%02x" % tuple(int(v) for v in bg)
    if tint:
        t = tuple(int(tint[i:i + 2], 16) for i in (1, 3, 5))
        px = rgba.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                k = max(r, g, b) / 255
                px[x, y] = (int(t[0] * k), int(t[1] * k), int(t[2] * k), a)
    box = rgba.getchannel("A").point(lambda v: 255 if v > 40 else 0).getbbox() or (0, 0, w, h)
    cw, ch = box[2] - box[0], box[3] - box[1]
    side = max(cw, ch)
    sq = Image.new("RGBA", (side, side))
    sq.paste(rgba.crop(box), ((side - cw) // 2, (side - ch) // 2))
    small = sq.resize((SIZE, SIZE), Image.LANCZOS)
    out = Image.new("RGB", (SIZE, SIZE))
    out.paste(small, (0, 0), small)
    # Every mark at full strength: the panel's brightness is set elsewhere,
    # and a dark red or navy mark all but vanished beside the others.
    peak = max(max(p) for p in out.getdata()) or 1
    k = 255 / peak
    return led(out.point(lambda v: min(255, int(v * k))))


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def sheet(marks, path):
    scale, dot, cols, gap = 10, 8, 8, 4
    cell = SIZE * scale
    rows = (len(marks) + cols - 1) // cols
    im = Image.new("RGB", (cols * (cell + gap * scale), rows * (cell + 3 * scale)), (12, 12, 12))
    d = ImageDraw.Draw(im)
    for n, (iata, m) in enumerate(marks):
        ox = (n % cols) * (cell + gap * scale)
        oy = (n // cols) * (cell + 3 * scale)
        for y in range(SIZE):
            for x in range(SIZE):
                c = m.getpixel((x, y))
                c = c if c != (0, 0, 0) else (30, 30, 30)
                d.ellipse([ox + x * scale + 1, oy + y * scale + 1,
                           ox + x * scale + 1 + dot, oy + y * scale + 1 + dot], fill=c)
        d.text((ox, oy + cell + 6), iata, fill=(200, 200, 200))
    im.save(path)


def main():
    marks = [(iata, mark(iata, opts)) for iata, _, opts in AIRLINES]
    if "--sheet" in sys.argv:
        sheet(marks, sys.argv[sys.argv.index("--sheet") + 1])
    lines = [
        "// Generated by tools/gen_airline_logos.py. Do not edit: change the list",
        "// there and run it again.",
        '#include "project/airline_logos.h"',
        "",
    ]
    for iata, m in marks:
        words = [rgb565(p) for p in m.getdata()]
        lines.append(f"static const uint16_t LOGO_{iata}[{SIZE * SIZE}] PROGMEM = {{")
        for i in range(0, len(words), SIZE):
            lines.append("  " + ", ".join("0x%04X" % v for v in words[i:i + SIZE]) + ",")
        lines.append("};")
    lines.append("")
    lines.append("const AirlineLogo AIRLINE_LOGOS[] = {")
    for iata, prefixes, _ in AIRLINES:
        for p in prefixes.split():
            lines.append(f'  {{ "{p}", LOGO_{iata} }},')
    lines.append("};")
    lines.append("const size_t AIRLINE_LOGOS_N = sizeof(AIRLINE_LOGOS) / sizeof(AIRLINE_LOGOS[0]);")
    out_cpp.write_text("\n".join(lines) + "\n")
    print(f"{len(marks)} airlines, {sum(len(p.split()) for _, p, _ in AIRLINES)} prefixes -> {out_cpp.relative_to(root)}")


if __name__ == "__main__":
    main()
