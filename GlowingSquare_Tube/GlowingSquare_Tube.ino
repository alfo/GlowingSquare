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
 GlowingSquare_Tube.ino
 *
 */

// Include Libraries
#include <FS.h>            // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager ~v2.0.0
#include <ArduinoJson.h>   // https://github.com/bblanchon/ArduinoJson ~v6.x.x
#include <PubSubClient.h>  // https://github.com/knolleary/pubsubclient ~v2.7.0
#include <ArduinoOTA.h>    // Included with core
#include <HTTPClient.h>    // To fetch data
                           // PxMatrix is included from inside the display.h file
#include <FastLED.h>       // For the animations during party mode

#ifdef ESP32
  #include <SPIFFS.h>
#endif

// Uncomment to reset device memory (useful to fix corruption / force new settings)
//#define START_ANEW

int party_mode = 0; // 0 = off, 1 = chill, 2 = party
int last_party_mode = party_mode;
uint8_t targetDisplayBrightness = 255;
uint8_t currentDisplayBrightness = 255;

#define INFO_UPDATE_INTERVAL 60000

// Include the other sketch files
#include "settings.h"
#include "display.h"
#include "wifi.h"
#include "tubeapi.h"
#include "animations.h"
#include "mqtt.h"

void setup() {

  // Start serial
  Serial.begin(115200);
  Serial.println();

  // Load config from the file system
  setupStorage();

  // Start up WiFi and config parameters etc.
  startWiFiManagerWithParameters();

  // Must go after the SPIFFs stuff or we crash the ESP32
  setupDisplay();

  // Start ArduinoOTA service
  // So not just anyone can re-flash our devices
  ArduinoOTA.setPassword("chvFSEebm9X4GtpY");
  ArduinoOTA.begin();

  // Instantiate MQTT
  setupMQTT();

  setupAnimations();

}

// Variables just for the example code below
long lastWebRequest = -INFO_UPDATE_INTERVAL;

void loop() {

  // All-purpose way to keep track of the time
  long now = millis();

  // Loop our network services
  mqttLoop();          // Non-blocking MQTT connect/re-connect
  ArduinoOTA.handle();    // In case we want to upload a new sketch

  // Show tube stuff only if party mode is off
  if (party_mode == 0) {

    // We need to use the blocking fade because the
    // actual displaying of info is blocking too
    changeBrightnessBlocking(3000);

    // Only download new info every sp often, and don't bother if display is off
    if (now - lastWebRequest > INFO_UPDATE_INTERVAL && currentDisplayBrightness != 0) {

      downloadTubeInfo();

      // Only display as offline if we've had 3 failed web requests in a row
      if (failed_attempts > 2) {
        displayOffline();
      } else {
        displayTubeInfo();
      }

      // Create a debug message
      lastWebRequest = now;

    }

  } else {

    // Use a non-blocking fade so the animation can continue
    // smoothly as we fade
    changeBrightnessNonBlocking();

    // Draw the next frame of our patterns
    patternLoop();

  }

}
