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

struct Departure {
  int timeToStation;
  const char* towards;
};


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

  std::vector<Departure> departures;

  // It's a chonky JSON document
  StaticJsonDocument<2048> json;
  deserializeJson(json, payload, DeserializationOption::Filter(filter));

  serializeJsonPretty(json, Serial);

  int departureCount = 0;

  for (JsonVariantConst v : json.as<JsonArray>()) {
    departures.push_back({v["timeToStation"], v["towards"]});
    departureCount++;
  }

  std::sort(departures.begin(), departures.end(), [](const Departure &a, const Departure &b) {
    return a.timeToStation < b.timeToStation;
  });

  // We have four possible slots on the disaply
  for (int i = 0; i < 4; i++) {

    // If this row has no matching departure entry
    // then clear the row and skip
    if (i >= departureCount) {
      fillBlankRow(i*8);
      break;
    }

    int mins = departures[i].timeToStation;
    mins = mins / 60;


    char formattedMins[10];
    sprintf(formattedMins, "%im", mins);

    Serial.print(formattedMins);
    Serial.println(departures[i].towards);

    drawStaticAndScrollingText(i*8, 50, formattedMins, departures[i].towards, 255, 80, 0, 150, 150, 150);

    delay(1000);
    
  }

 }
