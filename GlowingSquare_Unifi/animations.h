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
 Glowing Square: Flight Display             |___/
 For ESP32
 animations.h
 *
 */

#include "Patterns/Effects.h"
Effects effects;

#include "Patterns/Drawable.h"
#include "Patterns/Playlist.h"

#include "Patterns/Patterns.h"
Patterns patterns;

/*

Patterns:
"0: Noise",
"1: Wave",
"2: Flock",
"3: Snake",
"4: Spiral"

*/

unsigned long ms_current  = 0;
unsigned long ms_previous = 0;
unsigned long next_frame = 0;


void setupAnimations() {
  // Load the effects into memory
  effects.Setup();

  // Just a placeholder until we set it again later
  patterns.setPattern(1);
  patterns.start();
}


void patternLoop() {

  // Switch the pattern when the party_mode is changed
  // But only if it's changed since the last loop
  if (party_mode != last_party_mode) {

    // Required to make the transition good
    patterns.stop();

    // Set the patterns for each mode
    if (party_mode == 1)
      patterns.setPattern(1);
    else
      patterns.setPattern(4);

    // Make sure we don't run this again
    last_party_mode = party_mode;

    // Resume the pattern display
    patterns.start();
  }

  ms_current = millis();

  int frame_delay = 0;

  if (party_mode == 1) {
    frame_delay = 20;
  }

  if (next_frame < ms_current) {
    next_frame = patterns.drawFrame() + ms_current + frame_delay;
    display.showBuffer();
  }
}
