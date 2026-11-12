#pragma once

#include <Arduino.h>
#include "project/matrix/matrix.h"

// v1's Unifi icons, drawn pixel by pixel in 2020.
#define IC_C  0xF101   // rgb565(247, 35, 12): clients and guests
#define IC_U  0xC8B5   // rgb565(204, 20, 170): upload
#define IC_D  0xFAC1   // rgb565(250, 88, 12): download
#define IC_0  0x0000

static const uint16_t CLIENT_ICON[49] PROGMEM = {
  IC_C, IC_C, IC_C, IC_C, IC_0, IC_0, IC_0,
  IC_0, IC_0, IC_0, IC_0, IC_C, IC_C, IC_0,
  IC_C, IC_C, IC_C, IC_0, IC_0, IC_C, IC_0,
  IC_0, IC_0, IC_0, IC_C, IC_0, IC_0, IC_C,
  IC_0, IC_0, IC_0, IC_0, IC_C, IC_0, IC_C,
  IC_C, IC_C, IC_0, IC_0, IC_C, IC_0, IC_C,
  IC_C, IC_C, IC_0, IC_0, IC_C, IC_0, IC_C,
};

static const uint16_t GUEST_ICON[49] PROGMEM = {
  IC_0, IC_0, IC_0, IC_C, IC_0, IC_0, IC_0,
  IC_0, IC_0, IC_C, IC_C, IC_C, IC_0, IC_0,
  IC_0, IC_0, IC_C, IC_C, IC_C, IC_0, IC_0,
  IC_0, IC_0, IC_C, IC_C, IC_C, IC_0, IC_0,
  IC_0, IC_0, IC_0, IC_C, IC_0, IC_0, IC_0,
  IC_0, IC_C, IC_C, IC_C, IC_C, IC_C, IC_0,
  IC_0, IC_C, IC_C, IC_C, IC_C, IC_C, IC_0,
};

static const uint16_t UP_ICON[35] PROGMEM = {
  IC_0, IC_0, IC_U, IC_0, IC_0,
  IC_0, IC_U, IC_U, IC_U, IC_0,
  IC_U, IC_0, IC_U, IC_0, IC_U,
  IC_0, IC_0, IC_U, IC_0, IC_0,
  IC_0, IC_0, IC_U, IC_0, IC_0,
  IC_0, IC_0, IC_U, IC_0, IC_0,
  IC_0, IC_0, IC_U, IC_0, IC_0,
};

static const uint16_t DOWN_ICON[35] PROGMEM = {
  IC_0, IC_0, IC_D, IC_0, IC_0,
  IC_0, IC_0, IC_D, IC_0, IC_0,
  IC_0, IC_0, IC_D, IC_0, IC_0,
  IC_0, IC_0, IC_D, IC_0, IC_0,
  IC_D, IC_0, IC_D, IC_0, IC_D,
  IC_0, IC_D, IC_D, IC_D, IC_0,
  IC_0, IC_0, IC_D, IC_0, IC_0,
};
