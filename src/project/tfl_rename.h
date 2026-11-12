#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// ============================================================
//  The Tube mode's Rename setting: "from=to; from=to"
//
//  TfL's predictions are sometimes plainly wrong for a platform. At Notting
//  Hill Gate in November 2026 every Circle train via Victoria came as the
//  Hammersmith & City to "Hammersmith", where it ends after the whole loop,
//  and every District train to Wimbledon as "Check Front of Train". A rule
//  replaces a destination that contains `from` (any case) with `to`, as the
//  platform's own sign says it. The first rule that matches wins.
//
//  No Arduino here: the host test runs it.
// ============================================================

// The part of [b, e) without spaces at either end.
static inline void tflTrim(const char *&b, const char *&e) {
  while (b < e && isspace((unsigned char)*b)) b++;
  while (e > b && isspace((unsigned char)e[-1])) e--;
}

// True when [b, e) occurs in text, in any case.
static inline bool tflContains(const char *text, const char *b, const char *e) {
  const size_t n = (size_t)(e - b);
  if (n == 0) return false;
  for (const char *t = text; *t; t++) {
    size_t i = 0;
    while (i < n && t[i] && tolower((unsigned char)t[i]) == tolower((unsigned char)b[i])) i++;
    if (i == n) return true;
  }
  return false;
}

// `text` renamed by `rules` into out. True when a rule matched.
static inline bool tflRename(const char *text, const char *rules, char *out, size_t n) {
  for (const char *r = rules; r && *r;) {
    const char *end = strchr(r, ';');
    if (!end) end = r + strlen(r);
    const char *eq = (const char *)memchr(r, '=', (size_t)(end - r));
    if (eq) {
      const char *fb = r, *fe = eq, *tb = eq + 1, *te = end;
      tflTrim(fb, fe);
      tflTrim(tb, te);
      if (tflContains(text, fb, fe)) {
        snprintf(out, n, "%.*s", (int)(te - tb), tb);
        return true;
      }
    }
    r = *end ? end + 1 : end;
  }
  snprintf(out, n, "%s", text);
  return false;
}
