# Rules for coding agents

This file is for AI coding agents working on a project built from
**esp32-iot-template**, and on the template itself. Most rules exist because
an earlier change broke something on a real device: SpoolmanScale, which this
template is extracted from, learned them the hard way.

## The layout

| Layer | Where | Rule |
|---|---|---|
| Your project | `src/project/` | Everything that names or does what the product does. Start with `project_config.h`, `project_settings.cpp`, `project.cpp`. |
| Core | `src/app/`, `src/services/`, `src/web/`, `src/hardware/` | Never names the project. Calls it only through `APP_HOOKS` (`src/app/app_hooks.h`). |
| UI (boards with a screen) | `src/ui/` | LVGL 8.3. Built only with `-DAPP_UI_LVGL`. |
| Board drivers | `src/boards/` | One header and one `.cpp` per board behind `src/boards/board.h`. Only the env's own is compiled. |

A change that needs the core to know about the project is the wrong change:
add a hook or a registry entry instead.

## Where new code goes

| New code | Place |
|---|---|
| A setting (web Settings page and device settings screen) | one row in `src/project/project_settings.cpp` |
| UI text | `src/project/strings_app.def` (`S(STR_P_..., "English")`) |
| A web page of the project's own | a `WebPage` in `src/project/`, listed in `PROJECT_WEB_PAGES` |
| A card or a live value on the Status page | `APP_HOOKS.statusCard` / `statusJson` (`data-k` keys) |
| Work that takes more than a frame (HTTP, TLS) | a worker task, the pattern in `src/web/web_jobs.cpp`; results come back through a flag the loop reads |
| Work an event handler or a web route must not do | a `*_pending` flag, done in the loop |
| NVS | `src/services/prefs_store.h` only |
| Pure logic (parse, compute, decide) | a unit without Arduino, with a host test in `test/` |
| A new board | `src/boards/<board>.h/.cpp`, an env in `platformio.ini` with `custom_flasher_name` |

## House rules

`scripts/check.sh` checks most of these.

**Loop and tasks**

- The loop task runs the web server, the UI and the project. Never block it:
  no `delay()` longer than a frame, no HTTP. A blocked loop is a web page that
  does not answer and a WiFi that does not come back.
- LVGL, NVS and the log belong to the loop task. A worker task touches none of
  them; it sets state the loop reads.
- Workers check `workerHeapReady()` before they start. Without PSRAM (classic
  ESP32) a TLS handshake costs some 40 kB of internal heap: one at a time.

**Display (LVGL)**

- Colours, radii and fonts come from `src/ui/theme.h`. No hex literals elsewhere.
- An event handler never does HTTP, never blocks and never deletes its own
  object: it sets a pending flag, the loop does the work.
- Never `lv_scr_load()`: it reboots the device. Screens are overlays, built
  when opened and released when closed.
- Debounce with `millis()` in the loop, not with a short `lv_timer`.

**Texts**

- Every caption goes through `T(STR_XXX)`. Copy with `copyT()` or `snprintf()`,
  never `strncpy` from `T()`.
- `lang.h` comes after ArduinoJson in any file that includes both: `T` is a macro.
- A translation is a sparse `.def` override; see `src/lang.h`.

**Web interface**

- Every route starts with `webRequire(srv, gate, ...)`.
- A page script sits inside a C++ string literal: bind listeners after
  rendering, never through HTML attributes, and give every `fetch` a `.catch`
  (or use `post()` / `getJson()` from `/app.js`, which never reject).
- Secrets (`SET_SECRET`) are never sent back to the browser and never logged.

**Code**

- A function does one thing: at most 50 lines, 5 parameters, 3 levels of nesting.
- No magic numbers: a named constant with its unit.
- Every external call can fail (HTTP, NVS, SD, I2C): log it, and say which step failed.
- No em-dash (U+2014) anywhere: the compiler and the display both fail on it.
- ArduinoJson 7 idioms. Every dependency pinned in `platformio.ini`.

## Check before you hand over

```bash
scripts/check.sh                                   # house rules, JS, sizes
pio run                                            # every env
for t in test/test_*.py; do python3 "$t"; done     # host tests
```

A build proves little on a device: say what was tested on hardware and what
was not.

## Releases

Bump `FW_VERSION` in `src/project/project_config.h`, commit, tag the same
string (`v1.2.3`, or `v1.2.3-beta.4` for a test build) and push the tag.
`.github/workflows/release.yml` does the rest.

## Commits

Conventional Commits in English (`fix:`, `feat:`, `docs:`), saying what changes
for the user. No tool attribution in commits, comments or pull requests.

## Glowing Square specifics

- `src/project/` is Glowing Square. Everything else under `src/` is the
  template's core: change it in esp32-iot-template and bring it over with
  `tools/sync_template.py`, never here, or the next sync undoes it.
- The matrix is a `GFXcanvas16` (`src/project/matrix/matrix.h`). Draw into it
  and let the frame loop in `project.cpp` push it with `matrixShow()`. Never
  talk to PxMatrix or the DMA driver from anywhere else.
- A display mode (`src/project/modes/`) fetches on the fetch task and draws on
  the loop. `fetch()` touches only its own "next" data; `commit()`,
  `enter()` and `frame()` draw. Nothing waits: scrolling advances a pixel per
  `ScrollRow::tick()`.
- Each mode keeps v1's layout. A change to what the matrix shows comes with a
  matching update to `test/test_render.py`'s checks, and a look at its
  pictures (`--png`).
- MQTT topics are a public interface: v1's stay exactly as they were.
- `lib/PxMatrix` is a vendored library; mark every change in it with `// GS:`.
