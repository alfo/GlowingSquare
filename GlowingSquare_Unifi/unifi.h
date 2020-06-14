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

// Function that runs from loop() every 30 seconds
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
  display.setTextColor(display.color565(250, 88, 12));
  
  display.setCursor(6,0);
  
  display.print(json["month_rx"].as<String>());

  display.setTextColor(display.color565(110, 10, 204));
  drawRightAlignedText(MATRIX_WIDTH - 5, 0, json["month_tx"].as<String>());
  
  drawIcon(0, 0, 5, 7, down);
  drawIcon(MATRIX_WIDTH - 5, 0, 5, 7, up);
  
  //drawIcon(0, 8, 8, 8, globe);

  display.setTextColor(display.color565(247, 34, 12));
  display.setCursor(9,8);
  display.print(json["clients"].as<int>());
  display.print(" ");
  display.print(json["guests"].as<int>());


  // Draw the graph of network activity
  for (int x = 0; x < MATRIX_WIDTH; x++) {

    for (int y = 0; y < json["graph"][x]; y++) {
      display.drawPixel(x, MATRIX_HEIGHT - 1 - y, display.color565(31, 88, 204));
    }
    
  }

  // Only show the newest client if it's been tere 
  if (json["min_uptime"] < (INFO_UPDATE_INTERVAL * 4 / 1000)) {
    drawStaticAndScrollingText(16, 50, "New:", json["newest"].as<String>(), 247, 15, 12, 128, 8, 6);
  } else {
    fillBlankRow(16);
  }
  

  display.showBuffer();
  
 }
