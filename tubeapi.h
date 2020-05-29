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
 tubeapi.h
 * 
 */

 #define TFL_APP_ID "5ee457fb"
 #define TFL_APP_KEY "7bb4c9392c555bc1332b85f1714c2acf"
 #define STATION_ID "940GZZLUACY"
 #define TUBE_LINE "northern"
 #define DIRECTION "inbound"


void displayArrivalEntry(int line, JsonVariant entry) {

  Serial.println();
  
}

boolean downloadAndDisplayTubeInfo() {

  HTTPClient http;

  char requestURL[256];
  
  sprintf(requestURL, "https://api.tfl.gov.uk/Line/%s/Arrivals/%s?direction=%s&app_key=%s&app_id=%s", TUBE_LINE, STATION_ID, DIRECTION, TFL_APP_KEY, TFL_APP_ID);

  http.begin(requestURL);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("Failed to get info with HTTP Code %i\n", httpCode);
    return false;
  }

  String payload = http.getString();

  if (payload.length() == 0) {
    Serial.println("No data received from TFL");
    return false;
  }

  http.end();

  Serial.println("Downloaded info from TFL");
  
  StaticJsonDocument<160> filter;
  filter[0]["lineName"] = true;
  filter[0]["platformName"] = true;
  filter[0]["currentLocation"] = true;
  filter[0]["timeToStation"] = true;
  filter[0]["towards"] = true;

  // It's a chonky JSON document
  StaticJsonDocument<2048> json;
  deserializeJson(json, payload, DeserializationOption::Filter(filter));

  display.clearDisplay();
 
  for (int i = 0; i < 4; i++) {

    int mins = json[i]["timeToStation"];
    mins = mins / 60;

    char formattedMins[10];


    sprintf(formattedMins, "%im", mins);

    Serial.print(formattedMins);
    Serial.println(json[i]["towards"].as<String>());

    display.setTextWrap(false);

    display.setTextSize(1);
    display.setTextColor(display.color565(255, 80, 0));
    display.setCursor(0,i*8);
    display.print(formattedMins);
    scroll_text(i*8, 19, 50, json[i]["towards"].as<String>(), 255, 255, 255);
    
  }

 }
