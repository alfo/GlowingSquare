#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  Settings registry
//
//  Every setting a project has is declared once, in
//  project/project_settings.cpp, and rendered by both interfaces: the
//  web Settings page (GET/POST /api/settings) and, on boards with a
//  screen, the on-device settings screen. Adding a setting is one table
//  row and, if it needs a label, one string. Nothing else.
//
//  StringIDs are plain numbers so this header stays clear of lang.h,
//  whose T() macro collides with ArduinoJson's template parameter.
// ============================================================

enum SettingKind : uint8_t {
  SET_BOOL,      // on or off                         store: bool*
  SET_ENUM,      // one of opt_count values            store: uint8_t*
  SET_INT,       // integer in [min, max]              store: int32_t*
  SET_TEXT,      // string, at most max-1 bytes        store: char[max]
  SET_SECRET,    // like SET_TEXT, but never read back; the web shows set/unset
  SET_SUBMENU,   // no value of its own; the device UI opens screen `opens`
};

enum SettingFlags : uint8_t {
  SF_NONE          = 0,
  SF_REBOOT        = 1 << 0,  // takes effect after a restart; the UI says so
  SF_ACTIVE_IF_SET = 1 << 1,  // a SUBMENU row that reads as active while store != 0
  SF_WEB_ONLY      = 1 << 2,  // long text is no fun on a touch keyboard
  SF_DEVICE_ONLY   = 1 << 3,
  SF_MULTILINE     = 1 << 4,  // SET_TEXT: a textarea on the web
};

struct SettingSection {
  const char* id;        // stable, used by the web as an anchor
  uint16_t    str_name;
  uint16_t    str_info;  // 0 = no description line
  bool (*applies)();     // null = always shown
  // Web only: markup appended to the section's card after its rows, for what
  // a table row cannot be - a "Test connection" button and its <script>.
  // Returned as a C string to keep this header clear of Arduino's String.
  const char* (*web_extra)();
};

struct SettingDesc {
  // The NVS key and the web id, deliberately the same string: renaming one
  // would silently reset that setting on every device in the field.
  // NVS keys are at most 15 characters.
  const char*  id;
  SettingKind  kind;
  uint8_t      section;   // index into SETTING_SECTIONS
  void*        store;

  uint16_t     str_name;
  uint16_t     str_sub;   // 0 = none
  uint16_t     str_info;  // 0 = no help text
  const char*  icon;      // LVGL symbol; the web ignores it

  int32_t      def;       // BOOL / ENUM / INT
  const char*  def_text;  // TEXT / SECRET
  int32_t      min;       // INT
  int32_t      max;       // INT: upper bound. TEXT/SECRET: buffer size in bytes
  const char*  unit;      // INT: shown after the value, not translated ("s", "nm")

  uint8_t         opt_count;  // ENUM
  const uint16_t* opt_str;    // ENUM: opt_count StringIDs, in value order

  bool (*applies)();          // null = always shown
  const char* (*sub_fn)();    // a subtitle built at run time
  void (*on_change)();        // after the value changed and was written
  uint8_t      opens;         // SUBMENU: project-defined screen id
  bool (*opt_ok)(uint8_t v);  // ENUM: whether one value can be picked now
  uint8_t      flags;         // SettingFlags

  // ---- Chainable modifiers, so a table row reads like a sentence ----------
  constexpr SettingDesc sub(uint16_t s) const        { SettingDesc d = *this; d.str_sub = s; return d; }
  constexpr SettingDesc info(uint16_t s) const       { SettingDesc d = *this; d.str_info = s; return d; }
  constexpr SettingDesc withIcon(const char* i) const { SettingDesc d = *this; d.icon = i; return d; }
  constexpr SettingDesc when(bool (*f)()) const      { SettingDesc d = *this; d.applies = f; return d; }
  constexpr SettingDesc subFn(const char* (*f)()) const { SettingDesc d = *this; d.sub_fn = f; return d; }
  constexpr SettingDesc onChange(void (*f)()) const  { SettingDesc d = *this; d.on_change = f; return d; }
  constexpr SettingDesc optOk(bool (*f)(uint8_t)) const { SettingDesc d = *this; d.opt_ok = f; return d; }
  constexpr SettingDesc unitLabel(const char* u) const { SettingDesc d = *this; d.unit = u; return d; }
  constexpr SettingDesc flag(uint8_t f) const        { SettingDesc d = *this; d.flags |= f; return d; }
};

// ---- Row builders ----------------------------------------------------------

constexpr SettingDesc settingBool(const char* id, uint8_t section, bool* store,
                                  uint16_t name, bool def) {
  return { id, SET_BOOL, section, store, name, 0, 0, nullptr,
           def ? 1 : 0, nullptr, 0, 1, nullptr, 0, nullptr,
           nullptr, nullptr, nullptr, 0, nullptr, SF_NONE };
}

constexpr SettingDesc settingEnum(const char* id, uint8_t section, uint8_t* store,
                                  uint16_t name, uint8_t def,
                                  const uint16_t* opts, uint8_t opt_count) {
  return { id, SET_ENUM, section, store, name, 0, 0, nullptr,
           def, nullptr, 0, (int32_t)opt_count - 1, nullptr, opt_count, opts,
           nullptr, nullptr, nullptr, 0, nullptr, SF_NONE };
}

constexpr SettingDesc settingInt(const char* id, uint8_t section, int32_t* store,
                                 uint16_t name, int32_t def, int32_t lo, int32_t hi) {
  return { id, SET_INT, section, store, name, 0, 0, nullptr,
           def, nullptr, lo, hi, nullptr, 0, nullptr,
           nullptr, nullptr, nullptr, 0, nullptr, SF_NONE };
}

// `cap` is sizeof the char buffer behind `store`.
constexpr SettingDesc settingText(const char* id, uint8_t section, char* store,
                                  size_t cap, uint16_t name, const char* def) {
  return { id, SET_TEXT, section, store, name, 0, 0, nullptr,
           0, def, 0, (int32_t)cap, nullptr, 0, nullptr,
           nullptr, nullptr, nullptr, 0, nullptr, SF_NONE };
}

constexpr SettingDesc settingSecret(const char* id, uint8_t section, char* store,
                                    size_t cap, uint16_t name) {
  return { id, SET_SECRET, section, store, name, 0, 0, nullptr,
           0, "", 0, (int32_t)cap, nullptr, 0, nullptr,
           nullptr, nullptr, nullptr, 0, nullptr, SF_NONE };
}

constexpr SettingDesc settingSubmenu(const char* id, uint8_t section, uint16_t name,
                                     uint8_t opens) {
  return { id, SET_SUBMENU, section, nullptr, name, 0, 0, nullptr,
           0, nullptr, 0, 0, nullptr, 0, nullptr,
           nullptr, nullptr, nullptr, opens, nullptr, SF_NONE };
}

// Convenience for the table: SETTING_TEXT(id, sec, buf, name, def)
#define SETTING_TEXT(id, sec, buf, name, def)  settingText(id, sec, buf, sizeof(buf), name, def)
#define SETTING_SECRET(id, sec, buf, name)     settingSecret(id, sec, buf, sizeof(buf), name)
#define SETTING_ENUM(id, sec, store, name, def, opts) \
  settingEnum(id, sec, store, name, def, opts, (uint8_t)(sizeof(opts) / sizeof(opts[0])))

// ---- Declared by the project (project/project_settings.cpp) ----------------

extern const SettingSection SETTING_SECTIONS[];
extern const size_t         SETTING_SECTIONS_COUNT;
extern const SettingDesc    SETTINGS[];
extern const size_t         SETTINGS_COUNT;

// ---- Access ----------------------------------------------------------------

enum SettingResult : uint8_t {
  SR_OK = 0,
  SR_UNCHANGED,   // same value; nothing written, on_change not run
  SR_RANGE,       // out of range, too long, or not an option right now
  SR_KIND,        // wrong setter for this kind
  SR_NO_STORE,
};

// BOOL 0/1, ENUM raw value, INT the value. Other kinds answer 0.
int32_t settingGet(const SettingDesc &s);
// TEXT the value, SECRET "" (never read back), others "".
const char* settingTextValue(const SettingDesc &s);
// SECRET: whether a value is stored.
bool settingIsSet(const SettingDesc &s);

// The only writers. Variable, NVS and on_change in one, so a web route cannot
// forget what a device callback does. Values arrive off the network, so the
// range is checked here rather than trusted.
SettingResult settingSet(const SettingDesc &s, int32_t v);
SettingResult settingSetText(const SettingDesc &s, const char *v);
// Parses `v` by kind: "1"/"0"/"true"/"false" for BOOL, a number for ENUM/INT.
SettingResult settingSetFromString(const SettingDesc &s, const char *v);

// Back to the declared default.
void settingReset(const SettingDesc &s);

const SettingDesc*    settingById(const char *id);
const SettingSection* settingSectionOf(const SettingDesc &s);

// Whether the row belongs on screen right now: its section applies and it
// applies itself.
bool settingVisible(const SettingDesc &s);
bool settingSectionVisible(uint8_t section);

// The subtitle, already resolved: sub_fn, else str_sub joined with the
// current option name, else whichever of the two exists.
void settingSubtitle(const SettingDesc &s, char *out, size_t out_size);

// Reads every setting from NVS into its variable, clamped to its range.
void settingsLoadAll();
