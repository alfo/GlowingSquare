#!/usr/bin/env python3
"""Assemble a release from the built envs: the GitHub release assets, and
the files GitHub Pages serves (web flasher, manifests, version.json).

    release_assemble.py --version v1.2.3 --dist dist --pages pages --assets assets

dist/<env>/ holds firmware.bin, bootloader.bin, partitions.bin and
boot_app0.bin of each env (CI downloads the build artifacts there; locally,
boot_app0.bin is found in ~/.platformio when it is missing). pages/ is a checkout of the
gh-pages branch, updated in place. assets/ receives <slug>-<env>.bin per env.

A tag with a beta counter (v1.2.3-beta.4) is a test build: it lands in the
"prerelease" fields of version.json only, and the web flasher keeps offering
the last stable release. Any other tag updates both.

Run it locally the same way to see what a release would publish.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pio_envs import ROOT, envs  # noqa: E402

# Where each image goes, per chip. The classic ESP32's ROM loads the
# bootloader from 0x1000; the S3's from 0x0. The rest is the same on both.
BOOTLOADER_AT = {"ESP32": 0x1000, "ESP32-S2": 0x1000}
PARTITIONS_AT = 0x8000
BOOT_APP0_AT = 0xE000
APP_AT = 0x10000


def config_value(name):
    text = (ROOT / "src/project/project_config.h").read_text()
    m = re.search(r'#define\s+' + name + r'\s+"([^"]*)"', text)
    return m.group(1) if m else ""


def is_test_build(version):
    return re.search(r"-beta\.\d+$", version) is not None


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_boot_app0():
    for p in Path.home().glob(".platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin"):
        return p
    raise SystemExit("boot_app0.bin not found under ~/.platformio/packages")


def manifest(app_name, version, env, chip):
    return {
        "name": f"{app_name} ({env['flasher_name']})",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": chip,
            "improv": True,
            "parts": [
                {"path": "bootloader.bin", "offset": BOOTLOADER_AT.get(chip, 0x0)},
                {"path": "partitions.bin", "offset": PARTITIONS_AT},
                {"path": "boot_app0.bin", "offset": BOOT_APP0_AT},
                {"path": "firmware.bin", "offset": APP_AT},
            ],
        }],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--dist", type=Path, required=True)
    ap.add_argument("--pages", type=Path, required=True)
    ap.add_argument("--assets", type=Path, required=True)
    a = ap.parse_args()

    version, test = a.version, is_test_build(a.version)
    slug, app_name, repo = config_value("APP_SLUG"), config_value("APP_NAME"), config_value("APP_GH_REPO")
    if config_value("FW_VERSION") != version:
        raise SystemExit(f"FW_VERSION is {config_value('FW_VERSION')}, the tag says {version}")

    a.assets.mkdir(parents=True, exist_ok=True)
    a.pages.mkdir(parents=True, exist_ok=True)
    vpath = a.pages / "version.json"
    vdata = json.loads(vpath.read_text()) if vpath.exists() else {}
    vdata.setdefault("envs", {})
    vdata["url"] = f"https://github.com/{repo}/releases/latest" if repo else ""

    boards = []
    boot_app0 = find_boot_app0
    for env in envs():
        d = a.dist / env["name"]
        fw = d / "firmware.bin"
        if not fw.exists():
            print(f"skip {env['name']}: not in {a.dist}")
            continue
        asset = a.assets / f"{slug}-{env['name']}.bin"
        shutil.copy2(fw, asset)
        sha, size = sha256(fw), fw.stat().st_size

        e = vdata["envs"].setdefault(env["name"], {})
        e.update({"prerelease": version, "prerelease_sha256": sha, "prerelease_size": size})
        if not test:
            e.update({"stable": version, "stable_sha256": sha, "stable_size": size})
            out = a.pages / "fw" / env["name"]
            out.mkdir(parents=True, exist_ok=True)
            for part in ("bootloader.bin", "partitions.bin"):
                shutil.copy2(d / part, out / part)
            shutil.copy2(d / "boot_app0.bin" if (d / "boot_app0.bin").exists() else boot_app0(),
                         out / "boot_app0.bin")
            shutil.copy2(fw, out / "firmware.bin")
            (out / "manifest.json").write_text(
                json.dumps(manifest(app_name, version, env, env["chip"]), indent=2) + "\n")
        boards.append({
            "env": env["name"], "name": env["flasher_name"], "note": env["flasher_note"],
            "chip": env["chip"], "manifest": f"fw/{env['name']}/manifest.json",
            "version": e.get("stable", ""),
        })
        print(f"{env['name']}: {asset.name} {size} bytes sha256 {sha[:12]}")

    # A project with a single env may be read without "envs" by older
    # firmware; the first env's fields stand at the top level too.
    first = next(iter(vdata["envs"].values()), {})
    for k in ("stable", "stable_sha256", "stable_size", "prerelease", "prerelease_sha256", "prerelease_size"):
        if k in first:
            vdata[k] = first[k]
    vpath.write_text(json.dumps(vdata, indent=2) + "\n")

    if not test:
        (a.pages / "boards.json").write_text(json.dumps(
            {"app": app_name, "version": version, "boards": boards}, indent=2) + "\n")
        shutil.copy2(ROOT / "flasher/index.html", a.pages / "index.html")
        for extra in (ROOT / "flasher").glob("*"):
            if extra.name != "index.html" and extra.is_file():
                shutil.copy2(extra, a.pages / extra.name)
        shutil.copy2(ROOT / "src/project/assets/logo.svg", a.pages / "logo.svg")
        (a.pages / ".nojekyll").write_text("")
    print("version.json:", json.dumps(vdata["envs"], indent=None)[:400])


if __name__ == "__main__":
    main()
