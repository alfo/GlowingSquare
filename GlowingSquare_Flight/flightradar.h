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
 flightradar.h
 *
 */

#define AREA "51.5672,51.4131,-0.4146,0.1107"
#define OFFSETX 0

boolean offline = false;

// Struct to store the information about the downloaded flight
struct Flight {
  char from[4];
  char to[4];
  char number[8];
  char reg[12];
  char aircraft[12];
  unsigned int speed;
  unsigned int altitude;
};

// This is what we'll store the processed departures in
Flight flight;

// Function that runs from loop() every 60 seconds
boolean downloadFlightInfo() {

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

  } else {

    // All is good
    offline = false;
  }

  String payload = http.getString();

  StaticJsonDocument<2048> json;
  deserializeJson(json, payload);

  JsonObject object = json.as<JsonObject>();

  if (object.size() < 3) {
    Serial.println("No flight found");
    return false;
  }

  auto it = object.begin();
  it += 2;
  JsonArray values = it->value();

  flight.altitude = values[4];
  flight.speed = values[5];
  strcpy(flight.aircraft, values[8]);
  strcpy(flight.reg, values[9]);
  strcpy(flight.from, values[11]);
  strcpy(flight.to, values[12]);
  strcpy(flight.number, values[13]);

  Serial.printf("Downloaded flight: %s (%s %s) from %s to %s at %ikts %ift\n", flight.number, flight.aircraft, flight.reg, flight.from, flight.to, flight.speed, flight.altitude);

  return true;

}

void displayFlightInfo() {

  display.clearDisplay();
  display.setTextColor(myCYAN);

  display.setCursor(OFFSETX,0);
  display.print(flight.from);
  display.setTextColor(myCYAN);
  display.print(">");

  display.print(flight.to);
  display.setCursor(OFFSETX,8);
  display.setTextColor(myWHITE);
  display.print(flight.number);
  if(flight.altitude>10000) display.setTextColor(myGREEN);
  else display.setTextColor(myRED);
  display.setCursor(OFFSETX,16);
  display.print(flight.altitude);
  display.print("ft");
  display.setCursor(OFFSETX,24);
  display.setTextColor(myWHITE);
  display.print(flight.speed);
  display.print("kts");
  display.setTextColor(myCYAN);
  display.print(flight.aircraft);

  display.showBuffer();

 }
