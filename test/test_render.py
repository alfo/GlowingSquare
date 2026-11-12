"""Run each display mode's real fetch and drawing code on the host against
recorded API answers (test/fixtures) and check what lands on the 64x32
canvas. With --png DIR, also write LED-style pictures of every moment.

Needs Adafruit GFX and ArduinoJson from a PlatformIO build (.pio/libdeps);
skipped, not failed, when no env has been built yet."""
import glob
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]


def lib(name):
    hits = sorted(glob.glob(str(root / ".pio/libdeps/*" / name)))
    return Path(hits[0]) if hits else None


gfx, ajson = lib("Adafruit GFX Library"), lib("ArduinoJson")
if not gfx or not ajson:
    print("host render: skipped (build an env first, it fetches the libraries)")
    sys.exit(0)

png_dir = None
if "--png" in sys.argv:
    png_dir = Path(sys.argv[sys.argv.index("--png") + 1])
    png_dir.mkdir(parents=True, exist_ok=True)

sources = [
    root / "test/host_render/host_render.cpp",
    gfx / "Adafruit_GFX.cpp",
    root / "src/project/render.cpp",
    root / "src/project/matrix/matrix_common.cpp",
    root / "src/project/gs_state.cpp",
    root / "src/project/modes/mode_table.cpp",
    root / "src/project/modes/mode_tube.cpp",
    root / "src/project/modes/mode_flights.cpp",
    root / "src/project/airline_logos.cpp",
    root / "src/project/modes/mode_unifi.cpp",
]

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "render"
    subprocess.run(
        ["c++", "-std=gnu++17", "-O1", "-w", "-DARDUINO=10819",
         "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
         "-DARDUINOJSON_ENABLE_PROGMEM=0",
         "-I", str(root / "test/host_render/shim"), "-I", str(root / "src"),
         "-I", str(gfx), "-I", str(ajson / "src"),
         *map(str, sources), "-o", str(binary)],
        check=True)
    out = Path(tmp) / "out"
    out.mkdir()
    subprocess.run([str(binary), str(root / "test/fixtures"), str(out)], check=True)

    if png_dir:
        from PIL import Image, ImageDraw
        scale, dot = 10, 8
        for raw in sorted(out.glob("*.rgb565")):
            data = raw.read_bytes()
            img = Image.new("RGB", (64 * scale, 32 * scale), (8, 8, 10))
            d = ImageDraw.Draw(img)
            for i in range(64 * 32):
                v = data[2 * i] | (data[2 * i + 1] << 8)
                r, g, b = (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
                x, y = (i % 64) * scale, (i // 64) * scale
                c = (r, g, b) if v else (24, 22, 26)
                d.ellipse([x + 1, y + 1, x + dot, y + dot], fill=c)
            img.save(png_dir / (raw.stem + ".png"))
        print(f"pictures in {png_dir}")
