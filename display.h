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
 Glowing Square: Tube Display               |___/
 For ESP32
 display.h
 *
 */

#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 2
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define matrix_width 64
#define matrix_height 32

// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
uint8_t display_draw_time=60; //30-70 is usually fine

PxMATRIX display(64,32,P_LAT, P_OE,P_A,P_B,P_C,P_D);


void IRAM_ATTR display_updater(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable)
{
  if (is_enable)
  {
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 4000, true);
    timerAlarmEnable(timer);
  }
  else
  {
    timerDetachInterrupt(timer);
    timerAlarmDisable(timer);
  }
}


void drawStaticAndScrollingText(uint8_t ypos, unsigned long scroll_delay, String staticText, String scrollingText, uint8_t colorR_a, uint8_t colorG_a, uint8_t colorB_a, uint8_t colorR_b, uint8_t colorG_b, uint8_t colorB_b) {

    uint8_t characterWidth = 5;

    uint16_t scrollingTextLength = scrollingText.length();
    uint16_t offset = staticText.length() * 6;
    display.setTextWrap(false);
    display.setTextSize(1);
    display.setRotation(0);


    // Asuming 5 pixel average character width
    for (int xpos = offset; xpos >- (matrix_width + scrollingTextLength * characterWidth); xpos--) {

      display_update_enable(false);

      // Clear only the pixels on this part of the display
      for (int x = 0; x < matrix_width; x++) {
        for (int y = ypos; y < ypos + 8; y++) {
          display.drawPixel(x, y, 0); 
        }
      }

      display_update_enable(true);
      display_update_enable(false);
      
      display.setTextColor(display.color565(colorR_b,colorG_b,colorB_b));
      display.setCursor(xpos, ypos);
      display.println(scrollingText);

      for (int x = 0; x < offset; x++) {
        for (int y = ypos; y < ypos + 8; y++) {
          display.drawPixel(x, y, 0); 
        }
      }

      display.setCursor(0, ypos);
      display.setTextColor(display.color565(colorR_a,colorG_a,colorB_a));
      display.println(staticText);

      display_update_enable(true);
      
      delay(scroll_delay);
      yield();

      // This might smooth the transition a bit if we go slow
      // display.setTextColor(display.color565(colorR/4,colorG/4,colorB/4));
      // display.setCursor(xpos-1,ypos);
      // display.println(text);

      delay(scroll_delay/5);
      yield();

    }

    display.setTextColor(display.color565(colorR_b,colorG_b,colorB_b));
    display.setCursor(offset, ypos);
    display.println(scrollingText);

    
}


void setupDisplay() {

  display.begin(16);
  display.setFastUpdate(true);
  display.setMuxDelay(0,2,0,0,0);

  display.clearDisplay();
  display_update_enable(true);

  Serial.println("Finished setting up display");

  //scroll_text(1,50,"Welcome to PxMatrix!",96,96,250);
  display.clearDisplay();

  Serial.println("Finished scrolling text");
  
}
