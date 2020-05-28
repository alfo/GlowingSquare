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
 settings.h
 *
 */

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

char deviceTopic[40];
char inTopic[40];
char willTopic[40];
char roomStateTopic[40];

long lastMQTTReconnectAttempt = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  // Convert the input bytes into a usable JSON buffer
  StaticJsonDocument<256> json;
  deserializeJson(json, payload, length);

  Serial.print("Receive message:");
  serializeJsonPretty(json, Serial);
  Serial.print("\n");

  // Handle room-wide topic messages
  if (strcmp (topic, roomStateTopic) == 0) {

    if (strcmp (json["party"], "chill") == 0) {
      Serial.println("^^^ Mode change: Chill party");
      party_mode = 1;
    } else if (strcmp (json["party"], "on") == 0) {
      Serial.println("^^^ Mode change: Normal party");
      party_mode = 1;
    } else if (strcmp (json["party"], "off") == 0) {
      Serial.println("^^^ Mode change: No party");
      party_mode = 0;
    }
    
  }

  //mqttClient.publish("octodisplay/debug", message);

}

// This runs once from setup() in the main sketch
// It just takes the config (which has been loaded by this point)
void setupMQTT() {

  // Format the topics to include the hostname
  sprintf(willTopic,"%s/mqtt", hostname);
  sprintf(inTopic,"%s/in", hostname);
  sprintf(deviceTopic,"%s/out", hostname);
  sprintf(roomStateTopic,"house/%s/state", room);

  // We store the port as a char[6] so need to convert
  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setCallback(mqttCallback);
}

boolean mqttConnect() {

  Serial.print("=== MQTT connecting to ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(atoi(mqtt_port));

  // Use hostname as our client ID
  // Use our will topic to broadcast a "disconnected" message when device goes down
  if (mqttClient.connect(hostname, mqtt_username, mqtt_password, willTopic, 0, 1, "disconnected")) {

    // Announce the connection, and make our subscriptions
    mqttClient.publish(willTopic, "connected");
    mqttClient.subscribe(inTopic);

    // This one is so we can set room-wide settings (like party mode etc.)
    mqttClient.subscribe(roomStateTopic);

  } else {

    // Sadness and tears. Check your MQTT params in portal
    Serial.println("%%% Failed to connect to MQTT");

  }

  // Return true or false
  return mqttClient.connected();

}

void mqttLoop(long now) {

  // Maintain connection
  // This is also how we connect for the first time
  if(!mqttClient.connected()) {

    // Has it been 5 seconds since we last tried to connect
    if (now - lastMQTTReconnectAttempt > 5000) {
  
      lastMQTTReconnectAttempt = now;

      // If successful, reset the counters
      if (mqttConnect()) {
        lastMQTTReconnectAttempt = 0;
      }
    }

  } else {

    // We are connected
    // Check for incoming messages
    mqttClient.loop();
    
  }

}
