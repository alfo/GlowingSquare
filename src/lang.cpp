// ============================================================
//  Localisation - the tables. See lang.h.
// ============================================================
#include "lang.h"

// Some captions start with an LVGL symbol. A build without LVGL has no
// symbol font, so there they are empty strings.
#ifdef APP_UI_LVGL
#include <lvgl.h>
#else
#define LV_SYMBOL_IMAGE     ""
#define LV_SYMBOL_EYE_OPEN  ""
#define LV_SYMBOL_EYE_CLOSE ""
#define LV_SYMBOL_POWER     ""
#define LV_SYMBOL_WARNING   ""
#define LV_SYMBOL_WIFI      ""
#define LV_SYMBOL_REFRESH   ""
#define LV_SYMBOL_OK        ""
#define LV_SYMBOL_DOWNLOAD  ""
#define LV_SYMBOL_UPLOAD    ""
#define LV_SYMBOL_CLOSE     ""
#define LV_SYMBOL_RIGHT     ""
#define LV_SYMBOL_LEFT      ""
#define LV_SYMBOL_SETTINGS  ""
#define LV_SYMBOL_LIST      ""
#endif

Lang    g_lang     = LANG_EN;
uint8_t g_date_fmt = DATE_FMT_DMY;

const char* const LANG_NAMES[LANG_COUNT] = { "English" };
const char* const LANG_CODES[LANG_COUNT] = { "en" };

// English: every id, in enum order, straight from the .def files.
static const char* const STRINGS_EN[STR_COUNT] = {
  "",
#define S(id, en) en,
#include "lang/strings_core.def"
#include "project/strings_app.def"
#undef S
};

// A translation fills only the ids it has, by index:
//
//   static const char* STRINGS_DE[STR_COUNT];
//   static struct FillDe { FillDe() {
//   #define S(id, txt) STRINGS_DE[id] = txt;
//   #include "lang/strings_core.de.def"
//   #include "project/strings_app.de.def"
//   #undef S
//   } } s_fill_de;
//
// and adds STRINGS_DE to LANG_TABLES below.

const char* const* const LANG_TABLES[LANG_COUNT] = { STRINGS_EN };

static_assert(sizeof(STRINGS_EN) / sizeof(STRINGS_EN[0]) == STR_COUNT,
              "STRINGS_EN and the StringID enum are out of step");
