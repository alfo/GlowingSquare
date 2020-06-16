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
 Glowing Square: Unifi Display              |___/
 For ESP32
 unifi.h
 *
 */

int failed_attempts = 0;
#define GRAPH_HEIGHT 8

// Function that runs from loop() every 30 seconds
boolean downloadAndDisplayNetworkInfo() {

  Serial.print("Fetching info... ");

  HTTPClient http;

  // This is the API endpoint that we fetch new departures for our station from
  char requestURL[256];
  sprintf(requestURL, "%s?width=%i&height=%i", script_url, MATRIX_WIDTH, GRAPH_HEIGHT);

  // Fetch the data from the server
  http.begin(requestURL);
  int httpCode = http.GET();

  // Check that the server gave a valid response
  if (httpCode != 200) {
    Serial.printf("Failed to get info with HTTP Code %i\n", httpCode); // Code -11 means the request timed out

    // Mark us as offline so the little icon will be drawn next time
    failed_attempts++;
    return false;

  } else {
    // All is good
    failed_attempts = 0;
  }

  // Fetch and parse the web request
  String payload = http.getString();

  StaticJsonDocument<2048> json;
  deserializeJson(json, payload);

  // Start writing to the display
  display.clearDisplay();

  // Draw the download icon and stats
  drawIcon(0, 0, 5, 7, down_icon);
  display.setCursor(6,0);
  display.setTextColor(display.color565(250, 50, 12));
  display.print(json["month_rx"].as<String>());

  // Draw the upload icon and stats
  drawIcon(MATRIX_WIDTH - 5, 0, 5, 7, up_icon);
  display.setTextColor(display.color565(110, 10, 204));
  drawRightAlignedText(MATRIX_WIDTH - 5, 0, json["month_tx"].as<String>());

  // Draw the client icon and stats
  drawIcon(0, 8, 7, 7, client_icon);
  display.setCursor(9,8);
  display.setTextColor(display.color565(247, 20, 12));
  display.print(json["clients"].as<int>());

  // Calculate where the guest icon and text needs to go
  int next_x = 14 + json["clients"].as<String>().length() * 5;

  // Draw the guest icon and stats
  drawIcon(next_x, 8, 7, 7, guest_icon);
  display.setCursor(next_x + 8, 8);
  display.print(json["guests"].as<int>());

  // Draw the graph of network activity
  // Loop along the x axis of the graph
  for (int x = 0; x < MATRIX_WIDTH; x++) {

    // Draw the columns pixel by pixel
    for (int y = 0; y < json["graph"][x]; y++) {
      display.drawPixel(x, MATRIX_HEIGHT - 1 - y, display.color565(21, 58, 234));
    }

  }

  // Draw the newest client stuff
  // We do this last because it is a blocking animation
  // Only display if the new device is less than 2 mins old
  if (json["min_uptime"] < (INFO_UPDATE_INTERVAL * 4 / 1000)) {
    drawStaticAndScrollingText(16, 50, "New:", json["newest"].as<String>(), 247, 10, 12, 128, 5, 6);
  } else {
    // Remove what was there before
    fillBlankRow(16);
  }

  display.showBuffer();

  Serial.printf("%s down %s up\n", json["month_rx"].as<String>(), json["month_tx"].as<String>());

 }
