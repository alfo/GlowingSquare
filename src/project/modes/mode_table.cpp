#include "project/gs_state.h"
#include "project/modes/modes.h"

const DisplayMode &modeOps(uint8_t mode) {
  switch (mode) {
    case MODE_FLIGHTS: return MODE_FLIGHTS_OPS;
    case MODE_UNIFI:   return MODE_UNIFI_OPS;
    default:           return MODE_TUBE_OPS;
  }
}

