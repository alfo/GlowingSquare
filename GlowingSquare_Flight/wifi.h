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
 wifi.h
 *
 */

const unsigned long CONNECT_TIMEOUT = 30; // How long to attempt to connect to saved WiFi before going into AP mode
const unsigned long AP_TIMEOUT = 60; // Wait 20 Seconds in the config portal before trying again the original WiFi creds

// In case we want to do something when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("[CALLBACK] configModeCallback fired");
  display.setCursor(0,0);
  display.setBrightness(255);
  display.println("AP started...");
  display.showBuffer();
}

// In case we want to do something when the WiFi settings are saved
void saveWifiCallback(){
  Serial.println("[CALLBACK] saveCallback fired");
  display.clearDisplay();
  display.showBuffer();
}

// To be called once in setup()
// Just to make the main sketch cleaner
void startWiFiManagerWithParameters() {

  // WiFiManager bits and pieces
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConnectTimeout(CONNECT_TIMEOUT);
  wm.setTimeout(AP_TIMEOUT);
  wm.setCountry("GB");

  // Set hostname from settings
  wm.setHostname(hostname);

  // WiFiManager custom config
  WiFiManagerParameter custom_hostname("hostname", "Hostname", hostname, 24);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "MQTT Username", mqtt_username, 32);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 32);
  WiFiManagerParameter custom_room("room", "Room Name", room, 30);
  WiFiManagerParameter custom_flight_area("flight_area", "Long/Lat Bounds", flight_area, 36);
  wm.addParameter(&custom_hostname);
  wm.addParameter(&custom_room);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_username);
  wm.addParameter(&custom_mqtt_password);
  wm.addParameter(&custom_flight_area);

  //reset settings - wipe credentials for testing, if defined
  #if defined(START_ANEW)
    Serial.println("^^^^^^^^ Clearing WiFi credentials");
    wm.resetSettings();
  #endif

  // Run the routine to connect to the network
  if (!wm.autoConnect(hostname, "password")) {

    // If we've hit the config portal timeout, then retstart

    Serial.println("%%% Failed to connect and hit timeout, restarting");
    delay(100);
    ESP.restart();

    // Not sure if this line is necessary
    delay(5000);
  }

  // Keeping this line cos it's cute
  Serial.println("Connected ...yeey :)");

  // Update parameters from the new values set in the portal
  strcpy(hostname, custom_hostname.getValue());
  strcpy(room, custom_room.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(flight_area, custom_flight_area.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  }
}
