// ============================================================
//  Localisation (i18n)
//
//  Every UI string has an id, STR_*, and an English text, declared once
//  in a .def file:
//
//    lang/strings_core.def       the core's strings
//    project/strings_app.def     the project's strings
//
//  as S(STR_SOMETHING, "English text"). The enum and the English column
//  are generated from those files, so a string cannot be added to one and
//  forgotten in the other.
//
//  A translation is a sparse override, lang/strings_core.<xx>.def and
//  project/strings_app.<xx>.def, with the same S(ID, "text") rows for the
//  ids it covers. Anything it leaves out falls back to English. Adding a
//  language: a value in enum Lang, its name in LANG_NAMES, and its two .def
//  files included in lang.cpp.
//
//  T(STR_X) returns the text in the current language. lang.h has to come
//  after ArduinoJson in any file that uses both: T is a macro, and
//  ArduinoJson uses T as a template parameter.
// ============================================================
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

// LANG_COUNT is the sentinel loadPrefs() clamps against: the NVS byte is not
// trustworthy, and an out-of-range value would index a column that is not there.
enum Lang : uint8_t { LANG_EN = 0, LANG_COUNT };
extern Lang g_lang;
extern const char* const LANG_NAMES[LANG_COUNT];   // in their own language
extern const char* const LANG_CODES[LANG_COUNT];   // BCP 47, for <html lang>

// Date format, the NVS byte "date_fmt". Unknown values read as DD.MM.YYYY.
enum DateFormat : uint8_t { DATE_FMT_DMY = 0, DATE_FMT_ISO = 1, DATE_FMT_US = 2 };
extern uint8_t g_date_fmt;

enum StringID : uint16_t {
  STR_NONE = 0,   // 0 means "no string" in the settings registry
#define S(id, en) id,
#include "lang/strings_core.def"
#include "project/strings_app.def"
#undef S
  STR_COUNT
};

// [language][id]; a null entry falls back to English.
extern const char* const* const LANG_TABLES[LANG_COUNT];

static inline const char* langText(int id) {
  if (id <= STR_NONE || id >= STR_COUNT) return "";
  const char* s = LANG_TABLES[g_lang][id];
  return s ? s : LANG_TABLES[LANG_EN][id];
}

// T(STR_XXX) -> the string in the current language. LVGL can take it
// directly; lv_label_set_text() copies.
#define T(id) langText(id)

// A table string into a buffer, terminated - for the places that go on to
// format or append. Never strncpy(buf, T(id), ...): it leaves the last byte
// to chance whenever a translation fills the buffer.
static inline void copyT(char* dst, size_t n, int id) {
  snprintf(dst, n, "%s", langText(id));
}
