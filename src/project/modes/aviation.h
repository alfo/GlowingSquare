#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "project/gs_http.h"

// ============================================================
//  What the two aircraft modes share
//
//  Flights shows the plane nearest overhead, Airport the ones landing at
//  and leaving an airport. Both walk the same two ADS-B feeds, look routes
//  up in the same adsbdb cache and draw the same airline marks. The feeds
//  and the cache are the fetch task's (one fetch runs at a time);
//  drawAirlineMark() is the loop's.
// ============================================================

// The aircraft around a point, walked one at a time through `filter` to
// `each`. `reset` empties `ctx` before each feed is asked: a feed that fails
// halfway must not leave half its sky behind.
struct AdsbQuery {
  float               lat, lon;
  int                 radius_nm;
  const JsonDocument *filter;   // of one aircraft
  GsJsonEach          each;
  void              (*reset)(void *ctx);
  void               *ctx;
};

// adsb.lol, then adsb.fi; the one that answered last is asked first. False
// when neither answered, with the last one's error.
bool adsbWalk(const AdsbQuery &q, char *err, size_t err_n);

struct Route {
  char     callsign[10];
  char     number[10];                 // the IATA flight number, "" when unknown
  char     from[5], to[5];             // IATA, or ICAO where there is none
  char     from_icao[5], to_icao[5];
  char     from_city[24], to_city[24]; // adsbdb's municipality
  bool     known;
  uint32_t at_ms;
};

// adsbdb's route for a callsign, from the cache or asked now. With `ask`
// false a callsign not in the cache returns nullptr instead: one lookup is a
// TLS handshake, and a mode may only afford a few per fetch.
const Route *routeFor(const char *callsign, bool ask = true);

// True when `code` (IATA or ICAO, any case) is that end of a route.
bool routeEndIs(const char *code, const char *iata, const char *icao);

// "BAW136  " -> "BAW136": the feeds pad callsigns to eight.
void trimRight(char *s);

// The airline's 16x16 mark at (x, y) (airline_logos.h), or a plain plane
// for an airline not on the list and for anything without one.
void drawAirlineMark(int16_t x, int16_t y, const char *prefix);
