#!/usr/bin/env bash
# House rules, checked by machine instead of by memory.
# Runs locally (scripts/check.sh) and in CI on every push and pull request.
# Exit 1 on any finding. Warnings are printed but do not fail the run.
set -u
cd "$(dirname "$0")/.."

fail=0
warn=0
ok()   { printf 'ok    %s\n' "$*"; }
bad()  { printf 'FAIL  %s\n' "$*"; fail=1; }
warnf(){ printf 'warn  %s\n' "$*"; warn=1; }

tracked() { git ls-files -z -- src platformio.ini partitions README.md .github scripts tools; }

# 1. No em-dash anywhere: it breaks Xtensa GCC in comments with misleading
#    errors and renders as a box in LVGL strings.
hits=$(tracked | xargs -0 env LC_ALL=C grep -n $'\xe2\x80\x94' 2>/dev/null || true)
if [ -n "$hits" ]; then bad "em-dash (U+2014) found:"; echo "$hits" | sed 's/^/        /'; else ok "no em-dash in tracked files"; fi

# 2. T(id) is never copied with a bare strncpy: that form does not terminate.
hits=$(grep -rnE 'strncpy\s*\([^,]+,\s*T\(' src | grep -vE '^[^:]+:[0-9]+:\s*//' || true)
if [ -n "$hits" ]; then bad "strncpy(buf, T(...)) - use copyT() or snprintf():"; echo "$hits" | sed 's/^/        /'; else ok "no strncpy on T()"; fi

# 3. lv_scr_load() causes reboots; the overlay architecture replaces it.
hits=$(grep -rn 'lv_scr_load(' src | grep -vE '^[^:]+:[0-9]+:\s*//' || true)
if [ -n "$hits" ]; then bad "lv_scr_load() is banned:"; echo "$hits" | sed 's/^/        /'; else ok "no lv_scr_load()"; fi

# 3b. Colours come from the theme (src/ui/theme_palette.h), never a number
#     written into a widget: a palette change would miss it.
hits=$(grep -rnE 'lv_color_hex\(0x[0-9a-fA-F]+' src --include='*.cpp' | grep -v '^src/ui/theme' || true)
if [ -n "$hits" ]; then bad "colour written as a number (name it in ui/theme_palette.h):"; echo "$hits" | sed 's/^/        /'; else ok "no raw colours in widgets"; fi

# 4. The version is vX.Y.Z or vX.Y.Z-beta.N. A bare "-beta" ranks at 0 in
#    version_compare and in the release guard.
v=$(sed -nE 's/^#define[[:space:]]+FW_VERSION[[:space:]]+"([^"]+)".*/\1/p' src/project/project_config.h | head -1)
if [ -z "$v" ]; then bad "FW_VERSION not found in src/project/project_config.h";
elif ! echo "$v" | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+(-beta\.[0-9]+)?$'; then bad "version '$v' is not vX.Y.Z or vX.Y.Z-beta.N";
else ok "version $v"; fi

# 5. Every string has English text and every id is declared once.
out=$(cat src/lang/strings_core.def src/project/strings_app.def | grep -E '^S\(' )
empty=$(echo "$out" | grep -E '^S\([A-Z0-9_]+, ""\)' || true)
dups=$(echo "$out" | sed -E 's/^S\(([A-Z0-9_]+),.*/\1/' | sort | uniq -d)
todo=$(echo "$out" | grep -E '"TODO"' || true)
if [ -n "$empty" ]; then bad "strings without English text:"; echo "$empty" | sed 's/^/        /'; fi
if [ -n "$dups" ];  then bad "string ids declared twice:"; echo "$dups" | sed 's/^/        /'; fi
if [ -n "$todo" ];  then bad "strings still saying TODO:"; echo "$todo" | sed 's/^/        /'; fi
[ -z "$empty$dups$todo" ] && ok "$(echo "$out" | wc -l | tr -d ' ') strings, each with English, none twice"

# 6. Every fetch in a page has a catch: a closed gate answers 403 as text,
#    r.json() throws, and an uncaught rejection leaves an empty card.
for p in src/web/pages/*.cpp src/project/*.cpp; do
  [ -f "$p" ] || continue
  f=$(grep -o 'fetch(' "$p" | wc -l | tr -d ' '); c=$(grep -o '\.catch(' "$p" | wc -l | tr -d ' ')
  if [ "$c" -lt "$f" ]; then bad "$p: $f fetch( but only $c .catch("; fi
done
ok "every fetch( has its .catch("

# 7. Every web route starts with webRequire().
routes=$(grep -rhoE '(srv|ota_server)\.on\("[^"]+"' src | wc -l | tr -d ' ')
noreq=0
while IFS= read -r f; do
  n_on=$(grep -cE '(srv|portal|ota_server)\.on\("/(api|update|status)' "$f")
  n_req=$(grep -c 'webRequire(' "$f")
  if [ "$n_req" -lt "$n_on" ]; then bad "$f: $n_on API routes but $n_req webRequire()"; noreq=1; fi
done < <(grep -rlE '\.on\("/(api|update|status)' src)
[ "$noreq" -eq 0 ] && ok "$routes routes, every API route gated"

# 8. The JavaScript the pages emit parses. Two levels of escaping, and the
#    compiler checks only one.
if command -v node >/dev/null 2>&1; then
  if python3 scripts/check_web_js.py; then ok "web page scripts parse"; else bad "web page scripts: see above"; fi
else
  bad "node not found - needed to check the web page scripts"
fi

# 9. Every built binary fits its env's app slot.
out=$(python3 scripts/size_gate.py); rc=$?
echo "$out" | sed 's/^/      /'
if [ "$rc" -ne 0 ]; then bad "binary size, see above"; else ok "binary sizes"; fi

echo
if [ "$fail" -ne 0 ]; then echo "check.sh: FAILED"; exit 1; fi
if [ "$warn" -ne 0 ]; then echo "check.sh: passed with warnings"; else echo "check.sh: all checks passed"; fi
exit 0
