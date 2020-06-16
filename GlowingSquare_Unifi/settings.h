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
 settings.h
 *
 */

// Default values for config common to all projects
char hostname[24] = "glowingsquare_unifi";
char script_url[50] = "http://10.0.1.145:8080/";
char room[30] = "living_room";
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_username[40];
char mqtt_password[40];

// Default custom static IP (not always used)
char static_ip[16] = "10.0.3.255";
char static_gw[16] = "10.0.1.1";
char static_sn[16] = "255.255.0.0";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupStorage(){
  //clean FS, for testing, if flag enabled
   #if defined(START_ANEW)
    Serial.println("^^^^^^^^ Clearing SPIFFS");
    SPIFFS.format();
   #endif

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        StaticJsonDocument<256> json;
        DeserializationError jsonError = deserializeJson(json, buf.get());

        serializeJsonPretty(json, Serial);
        if (!jsonError) {
          Serial.println("\nparsed json");

          strcpy(hostname, json["hostname"]);
          strcpy(room, json["room"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(script_url, json["script_url"]);


//           if(json["ip"]) {
//             Serial.println("setting custom ip from config");
//             strcpy(static_ip, json["ip"]);
//             strcpy(static_gw, json["gateway"]);
//             strcpy(static_sn, json["subnet"]);
//             Serial.println(static_ip);
//           } else {
//             Serial.println("no custom ip in config");
//           }

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void saveConfig() {
  Serial.println("saving config");

  StaticJsonDocument<256> json;
  json["mqtt_server"]    = mqtt_server;
  json["mqtt_port"]      = mqtt_port;
  json["mqtt_username"]  = mqtt_username;
  json["mqtt_password"]  = mqtt_password;
  json["hostname"]       = hostname;
  json["room"]           = room;
  json["script_url"]     = script_url;

  // json["ip"]          = WiFi.localIP().toString();
  // json["gateway"]     = WiFi.gatewayIP().toString();
  // json["subnet"]      = WiFi.subnetMask().toString();

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);

  serializeJson(json, configFile);
  configFile.close();
  //end save
  shouldSaveConfig = false;
}
