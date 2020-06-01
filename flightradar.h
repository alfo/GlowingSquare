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

#define AREA "52.05,51.14,-1.30,0.68"

boolean offline = false;

// Struct to store the information about the downloaded flight
struct Flight {
  char* from;
  char* to;
  char* number;
  char* reg;
  char* aircraft;
  unsigned int speed;
  unsigned int altitude;
};

// This is what we'll store the processed departures in
Flight flight;

// Function that runs from loop() every 60 seconds
int downloadFlightInfo() {

  HTTPClient http;

  // This is the API endpoint that we fetch new departures for our station from
  char requestURL[256];  
  sprintf(requestURL, "http://data-live.flightradar24.com/zones/fcgi/feed.js?bounds=%s&faa=1&satellite=1&mlat=1&flarm=1&adsb=1&gnd=0&air=1&vehicles=0&estimated=0&maxage=14400&gliders=0&stats=0&ems=1&limit=1", AREA);

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

  if (payload.length() == 0) {

    // Sometimes there will just be no departures, cos the station is closed etc.
    Serial.println("No data received from FlightAware");
    return false;
  }

  // Close the HTTPClient to free memory
  http.end();

  // It's a chonky JSON document
  StaticJsonDocument<2048> json;
  deserializeJson(json, payload);

  serializeJsonPretty(json, Serial);

  JsonObject object = json.as<JsonObject>();

  const char* nodeID = object[2].begin()->key;
  JsonArray jsonFlight = object[nodeID];

  flight.altitude = jsonFlight[4].as<int>();
  flight.speed = jsonFlight[5].as<int>();
  strcpy(flight.aircraft, jsonFlight[8]);
  strcpy(flight.reg, jsonFlight[9]);
  strcpy(flight.from, jsonFlight[10]);
  strcpy(flight.to, jsonFlight[11]);
  strcpy(flight.number, jsonFlight[12]);

  Serial.printf("Downloaded flight %s from %s to %s altitude %ift speed %ikts\n", flight.number, flight.from, flight.to, flight.altitude, flight.speed); 

}

void displayFlightInfo() {

  

 }
