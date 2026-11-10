"""Compile the production local-date parser and read back all three display formats."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/local_date.h"
#include <cassert>
#include <cstring>

int main() {
  char iso[LOCAL_DATE_ISO_LEN];
  // The three formats isoToLocal() writes, all read back to the same day.
  assert(localDateToIso("10.03.2026", iso, sizeof(iso)) && strcmp(iso, "2026-03-10") == 0);
  assert(localDateToIso("2026-03-10", iso, sizeof(iso)) && strcmp(iso, "2026-03-10") == 0);
  assert(localDateToIso("03/10/2026", iso, sizeof(iso)) && strcmp(iso, "2026-03-10") == 0);
  // Single digits, as a hand-typed date may come.
  assert(localDateToIso("1.3.2026", iso, sizeof(iso)) && strcmp(iso, "2026-03-01") == 0);
  // What the scan holds when there is no date.
  assert(!localDateToIso("-", iso, sizeof(iso)) && iso[0] == 0);
  assert(!localDateToIso("", iso, sizeof(iso)) && iso[0] == 0);
  assert(!localDateToIso(nullptr, iso, sizeof(iso)));
  // Not a date: a month of 13, a day of 0, a mixed separator, a short buffer.
  assert(!localDateToIso("10.13.2026", iso, sizeof(iso)));
  assert(!localDateToIso("2026-03-00", iso, sizeof(iso)));
  assert(!localDateToIso("10.03/2026", iso, sizeof(iso)));
  char tiny[4];
  assert(!localDateToIso("10.03.2026", tiny, sizeof(tiny)) && tiny[0] == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("local date parser: ok")
