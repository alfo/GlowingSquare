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
 display.h
 *
 */

// We will use PxMatrix in double-buffer mode to allow us
// to change many pixels and then update the display once in one go
#define double_buffer true

// Include must go here because double_buffer muse be defined first
#include <PxMatrix.h>

//
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 32

// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
#define DISPLAY_DRAW_TIME 70 // 30-70 is usually fine

// Create our display with the correct pins
// LAT=22, OE=2, A=19, B=23, C=18, D=5 (if there's an E it would be 15)
PxMATRIX display(MATRIX_WIDTH, MATRIX_HEIGHT, 22, 2, 19, 23, 18, 5);

// Some standard colors
uint16_t myRED     = display.color565(255, 0, 0);
uint16_t myGREEN   = display.color565(0, 255, 0);
uint16_t myBLUE    = display.color565(0, 0, 255);
uint16_t myWHITE   = display.color565(255, 255, 255);
uint16_t myYELLOW  = display.color565(255, 255, 0);
uint16_t myCYAN    = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK   = display.color565(0, 0, 0);

// Display writer function from the example code
void IRAM_ATTR display_updater(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(DISPLAY_DRAW_TIME);
  portEXIT_CRITICAL_ISR(&timerMux);
}

// Draw a red pixel at the bottom of the display if we're
// offline, to show power is still on etc.
void displayOffline() {

  display.clearDisplay();
  display.drawPixel(MATRIX_WIDTH - 1, MATRIX_HEIGHT - 1, display.color565(255, 0, 0));
  display.showBuffer();
  display.copyBuffer();

}

// Function to clear all the pixels for a given row of text
// We have to do this otherwise the text always gets written over
// the previous text, instead of replacing it
void fillBlankRow(uint8_t ypos) {

  // Clear only the pixels on this part of the display
  for (int x = 0; x < MATRIX_WIDTH; x++) {
    for (int y = ypos; y < ypos + 8; y++) {
      display.drawPixel(x, y, display.color565(0, 0, 0 ));
    }
  }
}


void drawScrollingText(uint8_t ypos, int xpos, String scrollingText, uint8_t colorR_b, uint8_t colorG_b, uint8_t colorB_b) {
  // Write the scrolling text in its current position
  display.setTextColor(display.color565(colorR_b,colorG_b,colorB_b));
  display.setCursor(xpos, ypos);
  display.println(scrollingText);
}


void drawStaticText(uint8_t ypos, uint16_t offset, String staticText, uint8_t colorR_a, uint8_t colorG_a, uint8_t colorB_a) {

  // Write black over the area the static text will take up
  for (int x = 0; x < offset; x++) {
    for (int y = ypos; y < ypos + 8; y++) {
      display.drawPixel(x, y, 0);
    }
  }

  // Draw the static text
  display.setCursor(0, ypos);
  display.setTextColor(display.color565(colorR_a,colorG_a,colorB_a));
  display.println(staticText);

}

void drawRightAlignedText(uint8_t xpos, uint8_t ypos, String text) {

  uint8_t characterWidth = 5;
  uint16_t textLength = text.length();

  uint16_t offset = xpos - textLength * (characterWidth + 1);

  display.setCursor(offset, ypos);
  display.print(text);

}

void drawStaticAndScrollingText(uint8_t ypos, unsigned long scroll_delay, String staticText, String scrollingText, uint8_t colorR_a, uint8_t colorG_a, uint8_t colorB_a, uint8_t colorR_b, uint8_t colorG_b, uint8_t colorB_b) {

  // Asuming 5 pixel average character width
  uint8_t characterWidth = 5;

  uint16_t scrollingTextLength = scrollingText.length();
  uint16_t offset = staticText.length() * (characterWidth + 1);

  // Initial setup
  display.setTextSize(1);
  display.setRotation(0);

  // Each animation step of the scrolling
  for (int xpos = offset; xpos > -(MATRIX_WIDTH + scrollingTextLength * characterWidth) + 50; xpos--) {

    // Clear only the pixels on this part of the display
    fillBlankRow(ypos);

    drawScrollingText(ypos, xpos, scrollingText, colorR_b, colorG_b, colorB_b);

    drawStaticText(ypos, offset, staticText, colorR_a, colorG_a, colorB_a);

    // Draw the updates to the display
    display.showBuffer();

    // Copy the updates to the second buffer to avoid flashing
    display.copyBuffer();

    // Pause so we can set the speed of the animation
    delay(scroll_delay);
    yield();

  }

  // Write the scrolling text again, once the scroll has finished
  // so that it persists while we're scrolling the other lines
  fillBlankRow(ypos);
  drawScrollingText(ypos, offset, scrollingText, colorR_b, colorG_b, colorB_b);
  drawStaticText(ypos, 0, staticText, colorR_a, colorG_a, colorB_a);

  // Send the final line to the display
  display.showBuffer();
  display.copyBuffer();

}


void setupDisplay() {

  // Start up our display with a 1/16 scan rate
  display.begin(16);

  // This is required for my SRYLED P4 display to work properly
  display.setMuxDelay(0,2,0,0,0);

  // Empty anything in the display buffer
  display.flushDisplay();

  // We never want text to wrap in this project
  display.setTextWrap(false);

  display.setBrightness(currentDisplayBrightness);

  // More display setup bits taken from the double_buffer demo
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &display_updater, true);
  timerAlarmWrite(timer, 4000, true);
  timerAlarmEnable(timer);
}


void changeBrightnessBlocking(int fadeTime) {

  // Calculate how far we have to fade
  int change = targetDisplayBrightness - currentDisplayBrightness;

  // We have some fading to do
  if (change != 0) {

    // We fade at a 50Hz rate cos it makes the maths easier
    int step = change / (fadeTime / 20);

    // Prevent forever loops with 0 step
    if (step == 0 && change > 0) step = 1;
    if (step == 0 && change < 0) step = -1;

    Serial.printf("Fading from %i to %i brightness in steps of %i\n", currentDisplayBrightness, targetDisplayBrightness, step);

    while(currentDisplayBrightness != targetDisplayBrightness) {

      // This also supports steps being negative for fade downs
      currentDisplayBrightness += step;

      display.setBrightness(currentDisplayBrightness);

      // 50Hz there it is again
      delay(20);

      // In the case that the next step would take us past the desired target value
      if (abs(targetDisplayBrightness - currentDisplayBrightness) <= abs(step)) {

        // Stop there and set the display again
        currentDisplayBrightness = targetDisplayBrightness;
        display.setBrightness(currentDisplayBrightness);
      }

    }

    // Mark the fade as having completed
    currentDisplayBrightness = targetDisplayBrightness;

    Serial.println("Fade complete");

  }

}


void changeBrightnessNonBlocking() {

  int change = targetDisplayBrightness - currentDisplayBrightness;

  if (change != 0) {

    if (change > 0) {
      currentDisplayBrightness += 1;
    } else if (change < 0) {
      currentDisplayBrightness -= 1;
    }

    display.setBrightness(currentDisplayBrightness);

  }

}

void drawIcon(int x, int y, int width, int height, const uint16_t image[]) {

//uint16_t * buffer = new uint16_t[2048];
  //memcpy_P(buffer, image, 2048);

  int counter = 0;
  for (int yy = 0; yy < height; yy++) {
    for (int xx = 0; xx < width; xx++) {
      display.drawPixel(xx + x , yy + y, image[counter]);
      counter++;
    }
  }

  //free(buffer);

}
