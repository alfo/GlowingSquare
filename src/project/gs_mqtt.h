#pragma once

#include <stdint.h>

// ============================================================
//  MQTT
//
//  v1's topics, unchanged, so existing automations keep working:
//    <room>/<name>/mqtt   "connected" / "disconnected" (will, retained)
//    <room>/<name>/in     {"brightness": 0..255}
//    <room>/state         {"party": "off" | "chill" | "on"}
//  <name> is v1's device name for the mode (glowingsquare/tube,
//  glowingsquare/flight, glowingsquare/unifi) unless one is set.
//
//  With Home Assistant discovery on, also under glowingsquare/<id>/:
//    state         {"state","brightness","mode","party","rssi"}  (retained)
//    light/set     {"state":"ON"|"OFF","brightness":n}
//    mode/set      "Tube" | "Flights" | "Unifi"
//    party/set     "off" | "chill" | "on"
//    update        {"installed_version","latest_version"}
//  and the matching homeassistant/... config topics. The legacy will
//  topic doubles as HA's availability topic.
// ============================================================

void mqttBegin();
void mqttTick();           // from the loop: connect, read, publish when changed
void mqttStateChanged();   // something HA shows changed; published on the next tick
bool mqttConnected();
