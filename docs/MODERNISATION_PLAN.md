# Glowing Square 2: modernisation plan

Status (2026-11-10), branch `modernise`:

| Phase | State |
|---|---|
| 1 Template core | done, pushed to flatfieldco/esp32-iot-template (`27d2bdd`) |
| 2-3 Template UI + boards + fonts | in progress (separate agent, template repo only) |
| 4 Template CI/release/flasher | done with phase 1 (release.yml, multi-board flasher) |
| 5-7 GS2 firmware, modes, MQTT/HA | done here; all three envs build; host render test passes against recorded TfL / adsb.lol / adsbdb answers and a synthetic UniFi fixture |
| 8 GS2 CI/README | done here; not yet committed; nothing tested on hardware |

Changes from the plan while building:
- PxMatrix 1.8.2 does not compile on Arduino core 3: vendored into `lib/PxMatrix` with the two fixes marked `// GS:`.
- FastLED pinned to 3.10.3 (3.10.7 fails to compile on this core).
- Flights: when no aircraft is in range the display goes dark (v1 kept the last flight on screen indefinitely).
- Tube: rows below the last departure are cleared (v1 left a stale row when the list got shorter).
- The setup WiFi runs with a password, shown on the matrix itself.

Two deliverables:

1. **`esp32-iot-template`**: a new private repo, `flatfieldco/esp32-iot-template` (`~/Developer/esp32-iot-template`), GPL-3.0, extracted from SpoolmanScale with the filament, NFC, scale, backend, label and BLE code removed. It is a starting point for any ESP32 IoT device: WiFi provisioning, web UI with a settings pane, OTA from GitHub Releases, a web flasher, and an optional LVGL touchscreen layer.
2. **Glowing Square 2**: this repo, rebuilt from the template as **one firmware**. The web UI chooses what the matrix shows (Tube, Flights or Unifi), and each mode keeps its current fixed layout.

SpoolmanScale is GPL-3.0, so both repos are GPL-3.0, and both credit SpoolmanScale (Niko11111) in the README and file headers.

---

## 1. Template architecture

The template has three layers. Each layer may depend only on the layers above it in this list.

```
core/      headless; builds on every chip
  boot + loop task model, deferred-action flags, worker tasks (core 0)
  prefs_store (NVS), settings_registry (app-declared settings)
  wifi_manager, wifi_roam, setup_portal (AP + captive DNS), improv_serial
  mdns, time/NTP, device_name, breadcrumb, log ring (+ flash log)
  github_release, update_check, ota_state, partition_layout, version_compare
  web: server, shell, static css/js, access gates, jobs, pages registry
  web pages: Status, Settings, Network, Firmware, Logs
  lang: T()/STR_* string table, English only, mechanism kept
ui/        optional LVGL 8.3 layer, built only with -DAPP_UI_LVGL
  theme (dark/light + accent), ui_common, overlay registry (replaces the
  hard-coded hideAllOverlays), setup flow (welcome -> WiFi -> done),
  WiFi scan/pass/connect/portal screens, settings screen driven by the
  registry, system/info/OTA/display/timezone screens, num_pad, popups
boards/    one header + one .cpp per board behind board.h
  board.h: begin(), width/height, flush(area, px), setBrightness(),
           readTouch(), prepareSleep(), hasPsram, needsEvenFlush, ...
```

### Changes to SpoolmanScale code during extraction
- **The settings registry becomes app-declared.** The app supplies its `SettingDesc` table, and the template code has no idea what the settings mean. Three new kinds join BOOL/ENUM/SUBMENU: **TEXT**, **SECRET** (write-only; the web UI shows it as "set" or "not set") and **INT** (with min, max and unit). GS2 needs these for stop IDs, API keys and coordinates. `/api/settings` and the on-device settings screen both render from that one table. Settings are grouped into sections, and a section can be hidden by a predicate (for example, Tube settings show only in Tube mode).
- **Branding comes from build flags or `app_config.h`.** `APP_NAME`, `APP_SLUG`, `GH_REPO`, the `GH_IMAGE_ASSET` pattern, the portal SSID prefix, the mDNS model and the flasher URL all move there. Nothing in `core/` names the product.
- **Headless provisioning.** With no screen, the AP portal starts on its own when no SSID is saved, or after several failed boots, or when BOOT is held for 3 s. Improv serial works as it does now. The portal password is printed on serial and can be turned off. GS2 can also show it on the matrix.
- **No-PSRAM profile.** The log ring moves to internal RAM at a smaller size, screenshots are compiled out, the worker-heap floors are tuned per chip, and the update check stack shrinks where it can. `lvgl_mem` budgets are per board.
- **The hubs get split.** `app_state` and `deferred_actions` keep only generic state. The app adds its own flags through an `appLoopHooks` struct: `setup()`, `loop()`, `onWifiUp()`, `buildMainScreen()` and `registerWebPages()`.
- **Kept from SpoolmanScale:** the AGENTS.md rules (no `lv_scr_load`, handlers only set flags, theme tokens, `T()`, `webRequire` on every route), the `check.sh` rules that aren't scale-specific, the host-test harness, and the whitelist `.gitignore`.

### Boards and PlatformIO envs

| Env | Chip / flash / PSRAM | Display | Status |
|---|---|---|---|
| `esp32-headless` | ESP32 / 4MB / none | none | core only, example app |
| `esp32s3-headless` | S3 / 8-16MB | none | core only |
| `wt32-sc01-plus` | S3 / 16MB / 2MB QSPI | ST7796 8-bit parallel + FT6336U (LovyanGFX) | ported as-is, known to work |
| `waveshare-amoled-175` | S3R8 / 16MB / 8MB | CO5300 466x466 QSPI + CST9217, AXP2101 | new; col offset 6 |
| `waveshare-amoled-18` | S3R8 / 16MB / 8MB | SH8601 or CO5300 368x448 + FT3168 or CST816, TCA9554 | new; panel revision found by probing the touch IC address |
| `waveshare-amoled-206`, `-216`, `-164`, `-143`, `-132`, `-241`, `-191` | S3R8 | CO5300 / SH8601 / RM67162 / RM690B0 | pin tables written from Waveshare's configs; **untested** until someone has the board |

AMOLED driver: GFX Library for Arduino (`Arduino_CO5300` / `Arduino_SH8601` over `Arduino_ESP32QSPI`), which Waveshare's own examples use. Touch uses SensorLib and the PMU uses XPowersLib. LVGL stays on 8.3.11, the version SpoolmanScale is proven on. The even-pixel flush `rounder_cb` and the per-board column offset are fields in `board.h`. These panels have no backlight pin, so brightness is set through register 0x51.

Partitions: a 16MB S3 table (as SpoolmanScale), an 8MB table, and a 4MB classic table (nvs, otadata, 2 x ~1.9MB app slots, 64KB coredump, small log). CI enforces each table's app-slot size limit.

### Release pipeline
- `ci.yml` runs `check.sh`, the host tests (looped over `test/*.py`) and a build of every env with its size check.
- `release.yml` runs on a `v*` tag. It checks that the tag equals `FW_VERSION` and ranks above the published version, builds every env, attaches `<slug>-<env>.bin` assets, and writes `version.json` with `{env: {stable, sha256, size, prerelease…}}`. It also writes one ESP Web Tools manifest per env (classic ESP32: bootloader at 0x1000; S3: at 0x0) and commits only those generated files to `main` for GitHub Pages.
- `index.html` is the flasher page with a board picker, and Improv provisions WiFi straight after the flash.
- Each device checks only its own env's entry, so an S3 firmware can never be offered to a classic board.

### Example app
`examples/hello` (the default `src/app/`) has one TEXT, one ENUM and one BOOL setting, a status card on the web page, and a main LVGL screen on boards with a screen. It shows every extension point in under 300 lines.

---

## 2. Glowing Square 2 architecture

```
src/
  app/            app hooks, mode manager, render loop
  matrix/         Matrix interface (Adafruit_GFX canvas + brightness + show)
    matrix_pxmatrix.cpp   Brian Lough Mini 32 shield (R1=13 CLK=14 LAT=22 OE=2 A/B/C/D=19/23/18/5)
    matrix_dma.cpp        ESP32-HUB75-MatrixPanel-DMA v3.1 (configurable pins)
  render/         text rows, static+scrolling row, right-aligned text, icons, offline dot, fades
  modes/          DisplayMode interface: settings(), fetchTask(), render(frame)
    tube.cpp      TfL arrivals
    flights.cpp   adsb.lol + adsbdb route lookup
    unifi.cpp     UniFi Network API
    party.cpp     the Aurora patterns (Flock, Spiral, Wave, Snake, SimplexNoise) as chill/party
  mqtt/           PubSubClient, legacy topics + Home Assistant discovery
web pages: Status (live preview of the 64x32 frame), Settings, Network, Firmware, Logs
```

### Main changes from v1
- **Non-blocking render loop.** v1 blocks in `delay()` while text scrolls, so MQTT and OTA stall. In GS2 every mode keeps a frame state and `render()` draws one frame at a fixed rate (about 50 Hz scroll steps). Fetches run in a worker task on core 0, and results reach the loop through a mutex-guarded snapshot. Fades become a frame-driven tween in every mode.
- **One firmware, mode picked at runtime.** The *Display mode* ENUM setting (Tube / Flights / Unifi) is set in the web UI or over MQTT. Changing mode does not need a reboot. Settings for the other modes stay hidden.
- **The three layouts are kept pixel-for-pixel:**
  - Tube: 4 rows of `Nm` in orange plus scrolling grey `towards + platform`.
  - Flights: `FROM>TO`, then flight number, then altitude (green above 10,000 ft, red below), then `kts` and aircraft type.
  - Unifi: down and up month totals with icons, clients and guests with icons, a "New: <host>" scroller, and the 8px rx graph.
  - The red pixel in the bottom-right corner still means offline after 3 failed fetches.
- **Fixes:**
  - The dangling `const char*` in the Tube departures goes: strings are copied into fixed buffers.
  - The hard-coded ArduinoOTA password goes. ArduinoOTA becomes optional, with its password set in the web UI. The default path is web upload plus GitHub OTA.
  - All the duplicated files are merged.

### Data sources
- **Tube:** `GET https://api.tfl.gov.uk/Line/{route}/Arrivals/{stopId}?direction=…` (a blank route uses `/StopPoint/{id}/Arrivals`). `app_key` is optional: it works without a key, but anonymous requests are heavily rate-limited. The JSON is filtered as in v1, and the 60 s poll stays.
- **Flights:** `GET https://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}`, then pick the nearest airborne aircraft. The route comes from `GET https://api.adsbdb.com/v0/callsign/{cs}` and is cached by callsign in RAM (LRU, 16 entries). Settings change from a bounding box to **centre lat/lon plus radius**. A tool on the settings page converts an old `lat1,lat2,lon1,lon2` box. When adsbdb has no route, the first line shows the callsign instead of `FROM>TO`. airplanes.live refused our test request, so it's dropped.
- **Unifi:** direct from the device, with no PHP proxy. Base `https://<console>`, header `X-API-KEY`, TLS verification optional (self-signed consoles).
  - Client count: `GET /proxy/network/integration/v1/sites/{site}/clients?limit=1`, which returns `totalCount`.
  - Guests and live WAN rate: legacy `GET /proxy/network/api/s/{site}/stat/health` gives `num_guest` and `wan.rx_bytes-r`.
  - **The graph keeps the controller's 5-minute history, as v1 did.** It uses legacy `POST /proxy/network/api/s/{site}/stat/report/5minutes.site` with `{"attrs":["wan-rx_bytes","time"],"start":…,"end":…}`. That is the same report the PHP proxy read through `stat_5minutes_site()`. The device takes the last 100 buckets to set the scale and draws the last 64, so the graph is full from the first fetch. If that endpoint refuses the key, the device falls back to sampling `stat/health` itself.
  - **Month totals** come from legacy `stat/report/daily.site` (POST, `attrs: wan-rx_bytes, wan-tx_bytes`).
  - **Newest client:** the client list sorted by `connectedAt`.
  - ⚠️ Whether the legacy `stat/*` endpoints accept `X-API-KEY` without a cookie login isn't documented. Maintained tools (unpoller) do it. If it fails on your console, there's a fallback setting for username and password login.
  - The memory risk is the client list (v1 needed PHP because the JSON was too large). The device pages it with `limit=25` and filters the JSON as it parses.

### MQTT
- Legacy topics, unchanged, so existing automations keep working:
  - Publish `{room}/{name}/mqtt` `connected` / `disconnected` (LWT).
  - Subscribe `{room}/{name}/in` with `{"brightness": n}`.
  - Subscribe `{room}/state` with `{"party": "off|chill|on"}`.
  - `{name}` defaults to `glowingsquare/<mode>`, as v1's `MQTT_NAME`, and can be overridden.
  - Note: the README said `party_mode`, but the code always used `party`. GS2 keeps `party` and the README gets corrected.
- Home Assistant discovery (on/off setting, `homeassistant/` prefix):
  - a `light` (on/off plus brightness, with fade)
  - a `select` for display mode
  - a `select` for party mode (off / chill / party)
  - diagnostic `sensor`s for RSSI, uptime and firmware version
  - an `update` entity for firmware

### GS2 PlatformIO envs
| Env | Board | Matrix driver |
|---|---|---|
| `gs-mini32-pxmatrix` | D1 Mini 32 + Lough Mini 32 shield (your three displays) | PxMatrix (scan 1/16, `setMuxDelay(0,2,0,0,0)` as now) |
| `gs-esp32-dma` | classic ESP32 wired for DMA / Lough I2S shield / Trinity | HUB75 DMA |
| `gs-s3-dma` | any ESP32-S3 | HUB75 DMA |

The flasher at `alfo.github.io/glowingsquare` lists all three.

### Repo layout after the change
- `Enclosure/` stays.
- The three v1 sketches move out of the tree. They stay in history under the tag `v1-legacy`, which the README links.
- The README is rewritten: hardware, flashing, first setup, settings reference, MQTT/HA reference.

---

## 3. Phases

Each phase ends with a build of every env, the host tests passing, and a commit. Hardware checks are flagged for you.

| # | Phase | Ends with |
|---|---|---|
| 1 | Template core: extract, de-brand, app-declared registry with the new kinds, headless provisioning, no-PSRAM profile | `esp32-headless` + `esp32s3-headless` build; example app works in a browser *(you: flash one board)* |
| 2 | Template UI + WT32-SC01 Plus via `board.h` | `wt32-sc01-plus` builds; setup flow + settings screen *(you: check on device)* |
| 3 | Waveshare AMOLED boards: 1.75 and 1.8 first, then the rest from pin tables | all envs build; untested boards flagged in README *(you: 1.75 on device)* |
| 4 | Template CI, release workflow, flasher page, docs, AGENTS.md | a dry-run tag build in CI on the template repo |
| 5 | GS2 scaffold from template, Matrix interface with both drivers, render helpers, mode manager, party patterns | `gs-mini32-pxmatrix` shows party mode *(you: flash one display)* |
| 6 | Tube, Flights, Unifi modes + settings + live preview | modes render on the web preview *(you: check each on device; test UniFi API key auth)* |
| 7 | MQTT legacy + HA discovery | entities appear in HA *(you)* |
| 8 | GS2 CI/release/flasher, README, move v1 out, PR | PR on `modernise` |

What I can verify myself: PlatformIO builds of every env (pioarduino is installed), host unit tests, and the web UI and JSON via host-side checks. I can't verify anything that needs the hardware or your network: panels, touch, the matrix, and the UniFi console.

## 4. Defaults I've assumed (say if any are wrong)
- Template repo: `flatfieldco/esp32-iot-template`, private.
- Flights setting changes from a bounding box to centre plus radius.
- v1 sketches are removed from the tree and kept under the `v1-legacy` tag.
- ArduinoOTA is off by default (web and GitHub OTA cover it).
- The live 64x32 preview on the Status page is a new extra.
