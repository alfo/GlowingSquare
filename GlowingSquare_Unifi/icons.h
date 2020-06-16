/*
 *
 *
 *         _             ______
     /\   | |           |  ____|
    /  \  | | _____  __ | |__ ___  _ __ ___ _   _
   / /\ \ | |/ _ \ \/ / |  __/ _ \| '__/ _ \ | | |
  / ____ \| |  __/>  <  | | | (_) | | |  __/ |_| |
 /_/    \_\_|\___/_/\_\ |_|  \___/|_|  \___|\__, |
                                             __/ |
 Glowing Square: Unifi Display              |___/
 For ESP32
 icons.h
 *
 */

// Client Icon
const uint16_t ccolor = display.color565(247, 35, 12);

static const uint16_t client_icon[49] PROGMEM={
  ccolor, ccolor, ccolor, ccolor, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, ccolor, ccolor, 0x0000,
  ccolor, ccolor, ccolor, 0x0000, 0x0000, ccolor, 0x0000,
  0x0000, 0x0000, 0x0000, ccolor, 0x0000, 0x0000, ccolor,
  0x0000, 0x0000, 0x0000, 0x0000, ccolor, 0x0000, ccolor,
  ccolor, ccolor, 0x0000, 0x0000, ccolor, 0x0000, ccolor,
  ccolor, ccolor, 0x0000, 0x0000, ccolor, 0x0000, ccolor
};

// Guest Icon
const uint16_t gcolor = display.color565(247, 35, 12);

static const uint16_t guest_icon[49] PROGMEM={
  0x0000, 0x0000, 0x0000, gcolor, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, gcolor, gcolor, gcolor, 0x0000, 0x0000,
  0x0000, 0x0000, gcolor, gcolor, gcolor, 0x0000, 0x0000,
  0x0000, 0x0000, gcolor, gcolor, gcolor, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, gcolor, 0x0000, 0x0000, 0x0000,
  0x0000, gcolor, gcolor, gcolor, gcolor, gcolor, 0x0000,
  0x0000, gcolor, gcolor, gcolor, gcolor, gcolor, 0x0000
};

// Up Icon
const uint16_t ucolor = display.color565(204, 20, 170);

static const uint16_t up_icon[35] PROGMEM={
  0x0000, 0x0000, ucolor, 0x0000, 0x0000,
  0x0000, ucolor, ucolor, ucolor, 0x0000,
  ucolor, 0x0000, ucolor, 0x0000, ucolor,
  0x0000, 0x0000, ucolor, 0x0000, 0x0000,
  0x0000, 0x0000, ucolor, 0x0000, 0x0000,
  0x0000, 0x0000, ucolor, 0x0000, 0x0000,
  0x0000, 0x0000, ucolor, 0x0000, 0x0000
};

// Down Icon
const uint16_t dcolor = display.color565(250, 88, 12);

static const uint16_t down_icon[35] PROGMEM={
  0x0000, 0x0000, dcolor, 0x0000, 0x0000,
  0x0000, 0x0000, dcolor, 0x0000, 0x0000,
  0x0000, 0x0000, dcolor, 0x0000, 0x0000,
  0x0000, 0x0000, dcolor, 0x0000, 0x0000,
  dcolor, 0x0000, dcolor, 0x0000, dcolor,
  0x0000, dcolor, dcolor, dcolor, 0x0000,
  0x0000, 0x0000, dcolor, 0x0000, 0x0000
};
