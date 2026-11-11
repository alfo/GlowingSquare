#include "services/brownout_guard.h"

#include <Arduino.h>

#include "app_config.h"
#include "app/app_state.h"

#ifdef APP_BROWNOUT_GUARD_MS
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

static uint32_t s_saved = 0;
static uint32_t s_since_ms = 0;
static bool     s_off = false;
static bool     s_held = false;

void brownoutGuardBegin() {
  if (s_off) return;
  s_saved = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  s_since_ms = millis();
  s_off = true;
}

void brownoutGuardHold(bool on) {
  if (on == s_held) return;
  s_held = on;
  if (on) {
    if (!s_off) s_saved = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    s_off = true;
    s_since_ms = millis();
  }
  // Off again: brownoutGuardTick() restores the detector one guard period
  // later, so a failed transfer does not trip it on its way out.
}

void brownoutGuardTick() {
  if (!s_off || s_held) return;
  if (!wifi_ok && millis() - s_since_ms < (uint32_t)APP_BROWNOUT_GUARD_MS) return;
  // A little longer after connecting: the first transmissions spike too.
  if (wifi_ok && millis() - s_since_ms < 1000) return;
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, s_saved);
  s_off = false;
  Serial.println("Brownout detector back on");
}
#else
void brownoutGuardBegin() {}
void brownoutGuardTick() {}
void brownoutGuardHold(bool) {}
#endif
