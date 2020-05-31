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

// Uncomment to reset device memory
//#define START_ANEW

// Which room in the house is this project
char room[30] = "living_room";
int party_mode = 0; // 0 = off, 1 = chill, 2 = party
int last_party_mode = party_mode;

#define INFO_UPDATE_INTERVAL 60000

// Include the other sketch files
#include "settings.h"
#include "wifi.h"
#include "mqtt.h"
#include "display.h"
#include "tubeapi.h"
#include "animations.h"

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

  if (party_mode != last_party_mode) {
    display.clearDisplay();
    last_party_mode = party_mode;
  }

  // Show tube stuff only if party mode is off
  if (party_mode == 0) {

    // Only download new info every 10 seconds
    if (now - lastWebRequest > INFO_UPDATE_INTERVAL) {
  
      downloadAndDisplayTubeInfo();
  
      // Create a debug message
      lastWebRequest = now;
      
    }
    
  } else {

    patternLoop();
    
  }

  

}
