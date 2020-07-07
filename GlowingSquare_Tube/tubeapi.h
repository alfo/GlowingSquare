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

// Include our TfL credentials
#include "secret.h"

// The TfL Station ID is saved into config, comes from here: https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/StopPoint/StopPoint_Search
// The TfL Route is saved into config, test it out here: https://api.tfl.gov.uk/swagger/ui/index.html?url=/swagger/docs/v1#!/Line/Line_Arrivals
// The TfL direction is saved into config, options are "outbound", "inbound", or "" for both

#define SHOW_PLATFORM // If you want to include the departure platform in the scrolling text

// Struct to store the information about each
// train departure
struct Departure {
  int timeToStation;
  const char* towards;
  const char* platform;
};

// This is what we'll store the processed departures in
std::vector<Departure> departures;

// Keep track of any failed web requests
int failed_attempts = 0;

// Function that runs from loop() every 60 seconds
int downloadTubeInfo() {

  Serial.print("Fetching info from Tfl... ");

  HTTPClient http;

  long start = millis();

  // This is the API endpoint that we fetch new departures for our station from
  char requestURL[256];
  sprintf(requestURL, "https://api.tfl.gov.uk/Line/%s/Arrivals/%s?direction=%s&app_key=%s&app_id=%s", tfl_route, tfl_station_id, tfl_direction, TFL_APP_KEY, TFL_APP_ID);

  // Fetch the data from the server
  http.begin(requestURL);
  int httpCode = http.GET();

  Serial.printf("took %ims... ", millis() - start);

  // Check that the server gave a valid response
  // Check that the server gave a valid response
  if (httpCode != 200) {
    Serial.printf("Failed to get info with HTTP Code %i\n", httpCode); // Code -11 means the request timed out

    // Mark us as offline so the little icon will be drawn next time
    failed_attempts++;
    return false;

  } else {
    // All is good
    if (failed_attempts > 0)
      displayOnline();

    failed_attempts = 0;
  }


  String payload = http.getString();

  if (payload.length() == 0) {

    // Sometimes there will just be no departures, cos the station is closed etc.
    Serial.println("No data received from TfL");
    return false;
  } else {
    Serial.printf("Got data of length %i\n", payload.length());
  }

  // Close the HTTPClient to free memory
  http.end();

  // Remove the previous departures
  departures.clear();

  /*
   * Because the JSON response from TfL can be and absolute chonker sometimes
   * (especially if you want to see departures in both directions), it's too
   * big to fit into RAM. To handle this, we can use a JSON filter
   * to only process the data we're interested in.
   * In this case that's the below parameters:
   */
  StaticJsonDocument<160> filter;
  filter[0]["lineName"] = true;
  filter[0]["platformName"] = true;
  // filter[0]["currentLocation"] = true; // if you want to know where exactly the train is
  filter[0]["timeToStation"] = true;
  filter[0]["towards"] = true;

  // It's a chonky JSON document
  StaticJsonDocument<2048> json;
  deserializeJson(json, payload, DeserializationOption::Filter(filter));

  // Loop over the returned entries and insert them into the vector list
  for (JsonVariantConst v : json.as<JsonArray>()) {
    departures.push_back({v["timeToStation"], v["towards"], v["platformName"]});
  }

  Serial.printf("Downloaded %i items from TfL\n", departures.size());

  // Sort the vector list by ascending arrival time
  // (I don't know why TfL don't do this for us but oh well)
  std::sort(departures.begin(), departures.end(), [](const Departure &a, const Departure &b) {
    return a.timeToStation < b.timeToStation;
  });

}

void displayTubeInfo() {

  // Loop over the four possible slots on the display
  for (int i = 0; i < 4; i++) {

    // If this row has no matching departure entry
    // then clear the row and skip
    if (i >= departures.size()) {
      fillBlankRow(i*8);
      break;
    }

    Serial.print("%%%");
    Serial.println(departures[i].towards);

    // Convert seconds to minutes and format it nicely
    char formattedMins[10];
    sprintf(formattedMins, "%im", departures[i].timeToStation / 60);

    Serial.printf("%s %s %s\n", formattedMins, departures[i].towards, departures[i].platform);

    // Append the platform name to the displayed text if required
    #ifdef SHOW_PLATFORM
      String scrollingText = (String)departures[i].towards + " " + (String)departures[i].platform;
    #else
      String scrollingText = (String)departures[i].towards
    #endif

    // Draw all of our data to the display
    drawStaticAndScrollingText(i*8, 40, formattedMins, scrollingText, 255, 80, 0, 150, 150, 150);

    // Wait before doing the next one
    delay(1000);

  }

 }
