#include "services/settings_registry.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

#include "services/prefs_store.h"

// Last on purpose: T() is a macro (see lang.h).
#include "lang.h"

static int32_t clampInt(const SettingDesc &s, int32_t v) {
  if (v < s.min) return s.min;
  if (v > s.max) return s.max;
  return v;
}

static bool isTextKind(const SettingDesc &s) {
  return s.kind == SET_TEXT || s.kind == SET_SECRET;
}

// ---------------------------------------------------------------------------
//  Reading
// ---------------------------------------------------------------------------

int32_t settingGet(const SettingDesc &s) {
  if (!s.store) return 0;
  switch (s.kind) {
    case SET_BOOL:    return *(bool *)s.store ? 1 : 0;
    case SET_ENUM:
    case SET_SUBMENU: return *(uint8_t *)s.store;
    case SET_INT:     return *(int32_t *)s.store;
    default:          return 0;
  }
}

const char* settingTextValue(const SettingDesc &s) {
  if (s.kind != SET_TEXT || !s.store) return "";
  return (const char *)s.store;
}

bool settingIsSet(const SettingDesc &s) {
  if (!isTextKind(s) || !s.store) return false;
  return ((const char *)s.store)[0] != '\0';
}

// ---------------------------------------------------------------------------
//  Writing
// ---------------------------------------------------------------------------

SettingResult settingSet(const SettingDesc &s, int32_t v) {
  if (!s.store) return SR_NO_STORE;

  switch (s.kind) {
    case SET_BOOL: {
      const bool b = (v != 0);
      if (*(bool *)s.store == b) return SR_UNCHANGED;
      *(bool *)s.store = b;
      prefsPutBool(s.id, b);
      break;
    }
    case SET_ENUM:
    case SET_SUBMENU: {
      if (v < 0 || (s.opt_count && v >= s.opt_count) || v > 255) return SR_RANGE;
      if (s.opt_ok && !s.opt_ok((uint8_t)v)) return SR_RANGE;
      if (*(uint8_t *)s.store == (uint8_t)v) return SR_UNCHANGED;
      *(uint8_t *)s.store = (uint8_t)v;
      prefsPutUChar(s.id, (uint8_t)v);
      break;
    }
    case SET_INT: {
      if (v < s.min || v > s.max) return SR_RANGE;
      if (*(int32_t *)s.store == v) return SR_UNCHANGED;
      *(int32_t *)s.store = v;
      prefsPutInt(s.id, v);
      break;
    }
    default:
      return SR_KIND;
  }

  if (s.on_change) s.on_change();
  return SR_OK;
}

SettingResult settingSetText(const SettingDesc &s, const char *v) {
  if (!isTextKind(s)) return SR_KIND;
  if (!s.store || s.max <= 0) return SR_NO_STORE;
  if (!v) v = "";
  if ((int32_t)strlen(v) >= s.max) return SR_RANGE;
  char *buf = (char *)s.store;
  if (strcmp(buf, v) == 0) return SR_UNCHANGED;
  memcpy(buf, v, strlen(v) + 1);
  prefsPutString(s.id, buf);
  if (s.on_change) s.on_change();
  return SR_OK;
}

SettingResult settingSetFromString(const SettingDesc &s, const char *v) {
  if (isTextKind(s)) return settingSetText(s, v);
  if (!v || !v[0]) return SR_RANGE;
  if (s.kind == SET_BOOL) {
    if (!strcmp(v, "true") || !strcmp(v, "on"))   return settingSet(s, 1);
    if (!strcmp(v, "false") || !strcmp(v, "off")) return settingSet(s, 0);
  }
  char *end = nullptr;
  const long n = strtol(v, &end, 10);
  if (!end || *end != '\0') return SR_RANGE;
  return settingSet(s, (int32_t)n);
}

void settingReset(const SettingDesc &s) {
  if (isTextKind(s)) settingSetText(s, s.def_text ? s.def_text : "");
  else if (s.store)  settingSet(s, s.def);
}

// ---------------------------------------------------------------------------
//  Lookup and visibility
// ---------------------------------------------------------------------------

const SettingDesc* settingById(const char *id) {
  if (!id || !id[0]) return nullptr;
  for (size_t i = 0; i < SETTINGS_COUNT; i++) {
    if (!strcmp(SETTINGS[i].id, id)) return &SETTINGS[i];
  }
  return nullptr;
}

const SettingSection* settingSectionOf(const SettingDesc &s) {
  return s.section < SETTING_SECTIONS_COUNT ? &SETTING_SECTIONS[s.section] : nullptr;
}

bool settingSectionVisible(uint8_t section) {
  if (section >= SETTING_SECTIONS_COUNT) return false;
  const SettingSection &sec = SETTING_SECTIONS[section];
  return !sec.applies || sec.applies();
}

bool settingVisible(const SettingDesc &s) {
  if (!settingSectionVisible(s.section)) return false;
  if (s.applies && !s.applies()) return false;
  return true;
}

void settingSubtitle(const SettingDesc &s, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  out[0] = '\0';

  if (s.sub_fn) {
    snprintf(out, out_size, "%s", s.sub_fn());
    return;
  }

  const char *opt = nullptr;
  if (s.kind == SET_ENUM && s.opt_str && s.opt_count) {
    const int32_t v = settingGet(s);
    if (v >= 0 && v < s.opt_count) opt = T((StringID)s.opt_str[v]);
  }
  const char *sub = s.str_sub ? T((StringID)s.str_sub) : nullptr;

  if (sub && opt)  snprintf(out, out_size, "%s - %s", sub, opt);
  else if (sub)    snprintf(out, out_size, "%s", sub);
  else if (opt)    snprintf(out, out_size, "%s", opt);
}

// ---------------------------------------------------------------------------
//  Loading
// ---------------------------------------------------------------------------

static void loadText(const SettingDesc &s) {
  char *buf = (char *)s.store;
  const String v = prefsGetString(s.id, s.def_text ? s.def_text : "");
  // Too long for the buffer: a newer build may have stored more. Cut, never
  // overflow.
  snprintf(buf, (size_t)s.max, "%s", v.c_str());
}

void settingsLoadAll() {
  for (size_t i = 0; i < SETTINGS_COUNT; i++) {
    const SettingDesc &s = SETTINGS[i];
    if (!s.store) continue;
    switch (s.kind) {
      case SET_BOOL:
        *(bool *)s.store = prefsGetBool(s.id, s.def != 0);
        break;
      case SET_ENUM:
      case SET_SUBMENU: {
        uint8_t v = prefsGetUChar(s.id, (uint8_t)s.def);
        // NVS survives a downgrade: a value past the list reads past opt_str.
        if (s.opt_count && v >= s.opt_count) v = (uint8_t)s.def;
        *(uint8_t *)s.store = v;
        break;
      }
      case SET_INT:
        *(int32_t *)s.store = clampInt(s, prefsGetInt(s.id, s.def));
        break;
      case SET_TEXT:
      case SET_SECRET:
        if (s.max > 0) loadText(s);
        break;
    }
  }
}
