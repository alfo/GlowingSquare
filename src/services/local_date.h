#pragma once

#include <stddef.h>
#include <stdio.h>

// ============================================================
//  LOCAL DATE -> ISO
//
//  The scan keeps a spool's last drying the way isoToLocal()
//  wrote it: "DD.MM.YYYY", "YYYY-MM-DD" or "MM/DD/YYYY",
//  whichever the date setting was at the time. The separators
//  tell the three apart, so all of them are read back without
//  asking the setting, which may have changed since.
// ============================================================

#define LOCAL_DATE_ISO_LEN 11   // "YYYY-MM-DD" and the NUL

// True and "YYYY-MM-DD" in iso when the text is a date in one of the three
// display formats; false, iso empty, for "-", "" or anything else.
static inline bool localDateToIso(const char* local, char* iso, size_t n) {
  if (n) iso[0] = '\0';
  if (!local || !iso || n < LOCAL_DATE_ISO_LEN) return false;
  int day = 0, month = 0, year = 0;
  const bool read = sscanf(local, "%2d.%2d.%4d", &day, &month, &year) == 3 ||
                    sscanf(local, "%4d-%2d-%2d", &year, &month, &day) == 3 ||
                    sscanf(local, "%2d/%2d/%4d", &month, &day, &year) == 3;
  if (!read || year < 1000 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  snprintf(iso, n, "%04d-%02d-%02d", year, month, day);
  return true;
}
