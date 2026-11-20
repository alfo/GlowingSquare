#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ============================================================
//  A city's name, as the panel can show it
//
//  adsbdb names an airport's municipality as it is written there, and
//  sometimes district first: Istanbul's airport is in "Arnavutköy,
//  Istanbul". The panel's 5x7 font is 7-bit ASCII: an "ö" came out as
//  two glyphs of garbage. placeName() keeps the city and folds each
//  letter to its base letter (services/accent_fold.h keeps ä ö ü ß,
//  which the label printers' fonts have and this font has not).
//
//  No Arduino here: the host test runs it.
// ============================================================

// U+00C0 .. U+00FF and U+0100 .. U+017F, one base letter each. "x" and "/"
// stand for the multiplication and division signs among them.
static const char PLACE_FOLD_LATIN1[] =
    "AAAAAAACEEEEIIII"   // C0
    "DNOOOOOxOUUUUYTs"   // D0
    "aaaaaaaceeeeiiii"   // E0
    "dnooooo/ouuuuyty";  // F0
static const char PLACE_FOLD_EXT_A[] =
    "AaAaAaCcCcCcCcDd"   // 0100
    "DdEeEeEeEeEeGgGg"   // 0110
    "GgGgHhHhIiIiIiIi"   // 0120
    "IiIiJjKkkLlLlLlL"   // 0130
    "lLlNnNnNnnNnOoOo"   // 0140
    "OoOoRrRrRrSsSsSs"   // 0150
    "SsTtTtTtUuUuUuUu"   // 0160
    "UuUuWwYyYZzZzZzs";  // 0170

// `in` as 7-bit ASCII into out: a letter with an accent becomes its base
// letter, a no-break space a space, and anything else beyond ASCII is left
// out rather than drawn as garbage.
static inline void placeFold(const char *in, char *out, size_t n) {
  if (!n) return;
  size_t o = 0;
  const uint8_t *p = (const uint8_t *)in;
  while (*p && o + 1 < n) {
    if (*p < 0x80) { out[o++] = (char)*p++; continue; }
    const int len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    for (int i = 1; i < len; i++)
      if ((p[i] & 0xC0) != 0x80) { p += i; goto next; }   // broken: skip what there was
    if (len == 2) {
      const unsigned cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
      if (cp >= 0xC0 && cp <= 0xFF)        out[o++] = PLACE_FOLD_LATIN1[cp - 0xC0];
      else if (cp >= 0x100 && cp <= 0x17F) out[o++] = PLACE_FOLD_EXT_A[cp - 0x100];
      else if (cp == 0xA0)                 out[o++] = ' ';
    }
    p += len;
  next:;
  }
  out[o] = '\0';
}

// The city in a municipality name: after its last comma ("Arnavutköy,
// Istanbul"), unless what follows is a state's two or three letters
// ("Washington, DC"), then before it. Folded by placeFold().
static inline void placeName(const char *municipality, char *out, size_t n) {
  const char *start = municipality, *end = municipality + strlen(municipality);
  const char *comma = strrchr(municipality, ',');
  if (comma) {
    const char *tail = comma + 1;
    while (*tail == ' ') tail++;
    if (end - tail >= 4) start = tail;
    else                 end = comma;
  }
  char part[48];
  const size_t len = (size_t)(end - start) < sizeof(part) - 1 ? (size_t)(end - start) : sizeof(part) - 1;
  memcpy(part, start, len);
  part[len] = '\0';
  placeFold(part, out, n);
}
