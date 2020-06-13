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

  //Serial.printf("Downloaded flight: %s (%s %s) from %s to %s at %ikts %ift\n", flight.number, flight.aircraft, flight.reg, flight.from, flight.to, flight.speed, flight.altitude);

  return true;

}

void displayFlightInfo() {

//  display.clearDisplay();
//  display.setTextColor(myCYAN);
//  
//  display.setCursor(OFFSETX,0);
//  display.print(flight.from);
//  display.setTextColor(myCYAN);
//  display.print(">");
//  
//  display.print(flight.to);
//  display.setCursor(OFFSETX,8);
//  display.setTextColor(myWHITE);
//  display.print(flight.number);
//  if(flight.altitude>10000) display.setTextColor(myGREEN);
//  else display.setTextColor(myRED);
//  display.setCursor(OFFSETX,16);
//  display.print(flight.altitude);
//  display.print("ft");
//  display.setCursor(OFFSETX,24);
//  display.setTextColor(myWHITE);
//  display.print(flight.speed);
//  display.print("kts");
//  display.setTextColor(myCYAN);
//  display.print(flight.aircraft);

  display.showBuffer();
  
 }
