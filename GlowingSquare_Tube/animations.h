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
unsigned long ms_animation_max_duration = 60000; // 10 seconds
unsigned long next_frame = 0;


void setupAnimations() {
  effects.Setup();

  patterns.setPattern(1);
  patterns.start();
}


void patternLoop() {
  ms_current = millis();


  // if ( (ms_current - ms_previous) > ms_animation_max_duration )
  // {
  //  //  patterns.moveRandom(1);
  //
  //    patterns.stop();
  //    patterns.move(1);
  //    patterns.start();
  //
  //
  //    Serial.print("Changing pattern to:  ");
  //    Serial.println(patterns.getCurrentPatternName());
  //
  //    ms_previous = ms_current;
  //
  //    // Select a random palette as well
  //    //effects.RandomPalette();
  // }

  if ( next_frame < ms_current)
  {
    next_frame = patterns.drawFrame() + ms_current;
    display.showBuffer();
  }
}
