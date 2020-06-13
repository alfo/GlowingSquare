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
 tubeapi.h
 * 
 */

boolean offline = false;
#define URL "http://10.0.1.145:8080/"
#define GRAPH_HEIGHT 8

// Function that runs from loop() every 60 seconds
boolean downloadAndDisplayNetworkInfo() {

  HTTPClient http;

  // This is the API endpoint that we fetch new departures for our station from
  char requestURL[256];  
  sprintf(requestURL, "%s?width=%i&height=%i", URL, MATRIX_WIDTH, GRAPH_HEIGHT);

  // Fetch the data from the server
  http.begin(requestURL);
  int httpCode = http.GET();

  // Check that the server gave a valid response
  if (httpCode != 200) {
    Serial.printf("Failed to get info with HTTP Code %i\n", httpCode); // Code -11 means the request timed out
    
    // Mark us as offline so the little icon will be drawn next time
    offline = true;
    return false;
    
  } else {

    // All is good
    offline = false;
  }

  String payload = http.getString();

  StaticJsonDocument<2048> json;
  deserializeJson(json, payload);

  
  serializeJsonPretty(json, Serial);

  display.clearDisplay();
  display.setTextColor(myWHITE);
  
  display.setCursor(0,0);
  display.print(json["month_rx"].as<String>());
  display.print(" ");
  display.print(json["month_tx"].as<String>());
  

  display.setTextColor(myCYAN);
  display.setCursor(0,8);
  display.print(json["clients"].as<int>());
  display.print(" ");
  display.print(json["guests"].as<int>());


  for (int x = 0; x < MATRIX_WIDTH; x++) {

    for (int y = 0; y < json["graph"][x]; y++) {
      display.drawPixel(x, MATRIX_HEIGHT - 1 - y, myMAGENTA);
    }
    
  }

  drawStaticAndScrollingText(16, 50, "New:", json["newest"].as<String>(), 255, 255, 255, 255, 255, 255);

  display.showBuffer();
  
 }
