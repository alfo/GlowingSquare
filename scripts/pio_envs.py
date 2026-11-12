#!/usr/bin/env python3
"""What platformio.ini says about each env, for the scripts and CI.

    pio_envs.py              JSON list of every env with its metadata
    pio_envs.py --names      the env names, one per line (CI matrix)
    pio_envs.py --matrix     the env names as a JSON array

Per env: name, chip ("ESP32", "ESP32-S3", from the board), the partition
table, and the custom_flasher_name / custom_flasher_note options that the
web flasher shows. Options are looked up through `extends` and [env].
"""
import configparser
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load():
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.read(ROOT / "platformio.ini")
    return cp


def option(cp, section, key, seen=None):
    seen = set() if seen is None else seen
    if section in seen or not cp.has_section(section):
        return None
    seen.add(section)
    if cp.has_option(section, key):
        return cp.get(section, key).strip()
    if cp.has_option(section, "extends"):
        for parent in cp.get(section, "extends").split(","):
            v = option(cp, parent.strip(), key, seen)
            if v is not None:
                return v
    if section != "env":
        return option(cp, "env", key, seen)
    return None


def chip_of(board):
    b = (board or "").lower()
    if "s3" in b:
        return "ESP32-S3"
    if "c3" in b:
        return "ESP32-C3"
    if "s2" in b:
        return "ESP32-S2"
    return "ESP32"


def envs():
    cp = load()
    out = []
    for s in cp.sections():
        if not s.startswith("env:"):
            continue
        name = s[4:]
        board = option(cp, s, "board")
        out.append({
            "name": name,
            "board": board,
            "chip": chip_of(board),
            "partitions": option(cp, s, "board_build.partitions"),
            "flasher_name": option(cp, s, "custom_flasher_name") or name,
            "flasher_note": option(cp, s, "custom_flasher_note") or "",
        })
    return out


if __name__ == "__main__":
    e = envs()
    if "--names" in sys.argv:
        print("\n".join(x["name"] for x in e))
    elif "--matrix" in sys.argv:
        print(json.dumps([x["name"] for x in e]))
    else:
        print(json.dumps(e, indent=2))
