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

 boolean largerThan(JsonVariant first, JsonVariant second) {
    return first["timeToStation"].as<int>() > second["timeToStation"].as<int>();
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
   
    JsonArray array = json.as<JsonArray>();

    for (size_t i = 1; i < array.size(); i++) {
      
      for (size_t j = i; j > 0 && (largerThan(array[j-1], array[j])); j--) {
        JsonVariant tmp = array[j-1];
        array[j-1] = array[j];
        array[j] = tmp;
      }
      
    }
    
    for (JsonVariant v : array) {
      Serial.println(v.as<String>());
    }
    
  
 }
