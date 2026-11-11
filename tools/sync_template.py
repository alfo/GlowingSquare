#!/usr/bin/env python3
"""Bring the template's core into this project: the files under CORE are
copied from a commit of esp32-iot-template, everything else (src/project,
lib/, platformio.ini, the README) is left alone. Run it, build every env,
look at the diff, commit.

    tools/sync_template.py [path-to-template-checkout] [ref]

Defaults: ../esp32-iot-template, main. The commit it came from is written
to .template-version. Files the template deleted are listed, not deleted.
"""
import io
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# The core. A headless project leaves the UI layer out.
CORE = [
    "src/app", "src/services", "src/web", "src/lang", "src/lang.h", "src/lang.cpp",
    "src/main.cpp", "src/app_config.h",
    "src/hardware/flash_log.h", "src/hardware/flash_log.cpp",
    "src/hardware/sd_logger.h", "src/hardware/sd_logger.cpp",
    "scripts", "flasher", ".github/actions", ".github/workflows/release.yml",
    "partitions", "tools/gen_web_assets.py",
    "test/test_worker_rules.py", "test/test_local_date.py", "test/test_accent_fold.py",
]
# Inside the core, but the project's own.
KEEP = {"src/lang/strings_app.def"}
# Inside the core directories, but only used with a screen (APP_UI_LVGL):
# a headless project leaves them out.
UI_ONLY = {
    "src/app/screenshot.cpp", "src/app/screenshot.h",
    "src/app/setup_flow.cpp", "src/app/setup_flow.h",
    "src/web/web_screenshot.cpp", "src/web/web_screenshot.h",
    "scripts/lvgl_speed.py",
}


def main():
    tpl = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "esp32-iot-template"
    ref = sys.argv[2] if len(sys.argv) > 2 else "main"
    sha = subprocess.run(["git", "-C", str(tpl), "rev-parse", "--short", ref],
                         check=True, capture_output=True, text=True).stdout.strip()
    tar = subprocess.run(["git", "-C", str(tpl), "archive", ref, "--", *CORE],
                         check=True, capture_output=True).stdout
    incoming = set()
    with tarfile.open(fileobj=io.BytesIO(tar)) as t:
        for m in t.getmembers():
            if not m.isfile() or m.name in KEEP or m.name in UI_ONLY:
                continue
            incoming.add(m.name)
            dst = ROOT / m.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(t.extractfile(m).read())
    gone = []
    for c in CORE:
        p = ROOT / c
        files = [p] if p.is_file() else (list(p.rglob("*")) if p.is_dir() else [])
        for f in files:
            rel = str(f.relative_to(ROOT))
            if (f.is_file() and rel not in incoming and rel not in KEEP and rel not in UI_ONLY
                    and "__pycache__" not in rel):
                gone.append(rel)
    (ROOT / ".template-version").write_text(sha + "\n")
    print(f"synced {len(incoming)} files from {tpl.name}@{sha}")
    if gone:
        print("not in the template any more (delete if they are not yours):")
        for g in sorted(gone):
            print("   ", g)


if __name__ == "__main__":
    main()
