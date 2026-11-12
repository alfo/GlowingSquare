#pragma once

#include <Arduino.h>

// ============================================================
//  Airline marks for the Flights mode
//
//  A curated list, not every airline: those over west London and Heathrow's
//  regular carriers, each mark prepared to sit on black at 16x16 by
//  tools/gen_airline_logos.py, which writes airline_logos.cpp. To add an
//  airline, add a row there and run it again.
//
//  Looked up by the callsign's ICAO prefix (BAW136 -> BAW), so a flight
//  needs no lookup anywhere to get its mark. The host test runs the lookup
//  (its Arduino.h is a shim).
// ============================================================

constexpr int16_t AIRLINE_LOGO_SIZE = 16;

struct AirlineLogo {
  const char     *prefix;   // three letters, as in the callsign
  const uint16_t *img;      // AIRLINE_LOGO_SIZE squared, RGB565, row by row
};

extern const AirlineLogo AIRLINE_LOGOS[];
extern const size_t AIRLINE_LOGOS_N;

// The airline's three-letter prefix from an airline callsign: "BAW136" ->
// "BAW". Empty for anything else (a registration such as GKUBE, N123AB).
static inline void airlinePrefix(const char *callsign, char out[4]) {
  out[0] = '\0';
  if (!callsign) return;
  for (int i = 0; i < 3; i++)
    if (callsign[i] < 'A' || callsign[i] > 'Z') return;
  if (callsign[3] < '0' || callsign[3] > '9') return;
  memcpy(out, callsign, 3);
  out[3] = '\0';
}

// The mark for this prefix, or nullptr when the airline is not on the list.
static inline const uint16_t *airlineLogo(const char *prefix) {
  if (!prefix || !prefix[0]) return nullptr;
  for (size_t i = 0; i < AIRLINE_LOGOS_N; i++)
    if (strcmp(AIRLINE_LOGOS[i].prefix, prefix) == 0) return AIRLINE_LOGOS[i].img;
  return nullptr;
}

// How much the Flights mode wants to show a callsign: an airline on the
// list (2) over any other airline-style callsign (1, NetJets' NJE123) over
// a registration (0, a light aircraft or a helicopter). The nearest of the
// best rank in range is shown, however near a lower one is.
static inline uint8_t flightRank(const char *callsign) {
  char p[4];
  airlinePrefix(callsign, p);
  if (!p[0]) return 0;
  return airlineLogo(p) ? 2 : 1;
}
