"""Compile the production rules for when a background worker may start and
read them back: the heap floor by the server's scheme, the higher of two,
the largest block against the stack, and the log gate
(services/worker_rules.cpp)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/worker_rules.h"
#include <cassert>

int main() {
  const uint32_t TLS = WORKER_MIN_HEAP_TLS, PLAIN = WORKER_MIN_HEAP_PLAIN;
  static_assert(WORKER_MIN_HEAP_TLS > WORKER_MIN_HEAP_PLAIN, "TLS is the dearer floor");

  // The scheme decides, in any case of letters.
  assert(workerMinHeap("https://spoolman.example:7443") == TLS);
  assert(workerMinHeap("HTTPS://host") == TLS);
  assert(workerMinHeap("http://192.168.4.100:7912") == PLAIN);
  // A bare address carries no scheme and is read as http.
  assert(workerMinHeap("192.168.4.100:8000") == PLAIN);
  // Nothing to read a scheme from: TLS, never the cheaper floor.
  assert(workerMinHeap("") == TLS);
  assert(workerMinHeap(nullptr) == TLS);
  // Shorter than the prefix: no read past the end, and plain.
  assert(workerMinHeap("http") == PLAIN);

  // Two servers: the higher floor, on whichever side the TLS stands.
  assert(workerMinHeapBoth("http://bb:8000", "http://sm:7913") == PLAIN);
  assert(workerMinHeapBoth("http://bb:8000", "https://sm:7443") == TLS);
  assert(workerMinHeapBoth("https://bb:8443", "http://sm:7913") == TLS);
  assert(workerMinHeapBoth("http://bb:8000", "") == TLS);

  // Enough in all, but no piece big enough for a 16 kB stack: refused.
  assert(!workerHeapAllows(60000, 12000, PLAIN, 16384));
  assert(workerHeapAllows(60000, 16384, PLAIN, 16384));
  // Exactly at the floor starts, one byte under does not.
  assert(workerHeapAllows(PLAIN, 30000, PLAIN, 16384));
  assert(!workerHeapAllows(PLAIN - 1, 30000, PLAIN, 16384));

  // The gate: first call due, then quiet for the interval, counting.
  LogGate g{};
  assert(logGateDue(&g, 1000, 60000));
  assert(!logGateDue(&g, 2000, 60000));
  assert(!logGateDue(&g, 60999, 60000));
  assert(logGateDue(&g, 61000, 60000));
  assert(logGateTakeSuppressed(&g) == 2);
  assert(logGateTakeSuppressed(&g) == 0);
  // millis() wraps: still due after the interval, not 49 days later.
  LogGate w{};
  assert(logGateDue(&w, 0xFFFFF000u, 60000));
  assert(!logGateDue(&w, 0x00000100u, 60000));
  assert(logGateDue(&w, 0x0000F000u, 60000));
  // Reset: the next call is due again.
  logGateReset(&g);
  assert(logGateDue(&g, 61001, 60000));
  // No gate: always due, nothing counted.
  assert(logGateDue(nullptr, 0, 60000));
  assert(logGateTakeSuppressed(nullptr) == 0);
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src/services/worker_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("worker rules: ok")
