#!/usr/bin/env python3
"""Every built env's firmware.bin against the app slot of its own partition
table. Reads platformio.ini for each env's board_build.partitions (following
`extends`) and the table for app0's size.

    size_gate.py            check every env that has been built
    size_gate.py <env>...   check these, and fail if one is not built

Prints one line per env; exit 1 when an image does not fit or is missing.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pio_envs import ROOT, envs  # noqa: E402


def app_slot(table):
    for line in (ROOT / table).read_text().splitlines():
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5 and cols[0] == "app0":
            return int(cols[4], 0)
    raise ValueError(f"{table}: no app0 row")


def main(argv):
    table_of = {e["name"]: e["partitions"] for e in envs()}
    wanted = argv or [e for e in table_of if (ROOT / ".pio/build" / e / "firmware.bin").exists()]
    fail = 0
    for env in wanted:
        fw = ROOT / ".pio/build" / env / "firmware.bin"
        if not fw.exists():
            print(f"FAIL  {env}: not built")
            fail = 1
            continue
        table = table_of.get(env)
        if not table:
            print(f"FAIL  {env}: no such env, or no board_build.partitions")
            fail = 1
            continue
        slot = app_slot(table)
        size = fw.stat().st_size
        pct = size * 100 // slot
        if size > slot:
            print(f"FAIL  {env}: {size} bytes, more than the {slot} byte slot of {table}")
            fail = 1
        else:
            print(f"ok    {env}: {size} of {slot} bytes ({pct} %)")
    if not wanted:
        print("warn  nothing built yet")
    return fail


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
