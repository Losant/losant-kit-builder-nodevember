/**
   Workshop example for periodically sending temperature data.

   Visit https://www.losant.com/kit for full instructions.

   Copyright (c) 2016 Losant IoT. All rights reserved.
   https://www.losant.com
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <Losant.h>

// Configuration parameters.
// These are sent over serial in JSON format.
// They are saved to EEPROM and read on each boot.
String configWifiSSID;
String configWifiPass;
String configDeviceId;
String configAccessKey;
String configAccessSecret;
String configTmpEnabled; // whether or not to read temperature

// Above config is defined using the JSON spec below:
// {
//  "losant-config-wifi-ssid": "my-wifi-ssid",
//  "losant-config-wifi-pass": "my-wifi-pass",
//  "losant-config-device-id": "my-device-id",
//  "losant-config-access-key": "my-access-key",
//  "losant-config-access-secret": "my-access-secret",
//  "losant-config-tmp": false | false
//  }

const int BUTTON_PIN = 5;
const int LED_PIN = 4;

bool ledState = false;
bool deviceConfigured = false;

WiFiClientSecure wifiClient;

LosantDevice device;

void toggle() {
  Serial.println("Toggling LED.");
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
}

void handleCommand(LosantCommand *command) {
  Serial.print("Command received: ");
  Serial.println(command->name);

  if (strcmp(command->name, "toggle") == 0) {
    toggle();
  }
}

void connect() {
  // Connect to Wifi.
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(configWifiSSID);

  // WiFi fix: https://github.com/esp8266/Arduino/issues/2186
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(configWifiSSID.c_str(), configWifiPass.c_str());

  unsigned long wifiConnectStart = millis();

  while (WiFi.status() != WL_CONNECTED) {
    // Check to see if
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println("Failed to connect to WiFi. Please verify credentials: ");
      Serial.println();
      Serial.print("SSID: ");
      Serial.println(configWifiSSID);
      Serial.print("Password: ");
      Serial.println(configWifiPass);
      Serial.println();
      Serial.println("Trying again...");
      WiFi.begin(configWifiSSID.c_str(), configWifiPass.c_str());
      delay(10000);
    }

    delay(500);
    Serial.println("...");
    // Only try for 5 seconds.
    if(millis() - wifiConnectStart > 15000) {
      Serial.println("Failed to connect to WiFi");
      Serial.println("Please attempt to send updated configuration parameters.");
      deviceConfigured = false;
      return;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println();
  Serial.print("Connecting to Losant...");

  Serial.print("Authenticating Device...");
  HTTPClient http;
  http.begin("http://api.losant.com/auth/device");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  /* Create JSON payload to sent to Losant

       {
         "deviceId": "575ecf887ae143cd83dc4aa2",
         "key": "this_would_be_the_key",
         "secret": "this_would_be_the_secret"
       }

  */

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["deviceId"] = configDeviceId.c_str();
  root["key"] = configAccessKey.c_str();
  root["secret"] = configAccessSecret.c_str();
  String buffer;
  root.printTo(buffer);

  int httpCode = http.POST(buffer);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("This device is authorized!");
    } else {
      Serial.println("Failed to authorize device to Losant.");
      if (httpCode == 400) {
        Serial.println("Validation error: The device ID, access key, or access secret is not in the proper format.");
      } else if (httpCode == 401) {
        Serial.println("Invalid credentials to Losant: Please double-check the device ID, access key, and access secret.");
      } else {
        Serial.println("Unknown response from API");
      }
      Serial.println("Current Credentials: ");
      Serial.println("Device id: ");
      Serial.println(configDeviceId);
      Serial.println("Access Key: ");
      Serial.println(configAccessKey);
      Serial.println("Access Secret: ");
      Serial.println(configAccessSecret);
      Serial.println("Please attempt to send updated configuration parameters.");
      deviceConfigured = false;
      return;
    }
  } else {
    Serial.println("Failed to connect to Losant API.");
    Serial.println("Please attempt to send updated configuration parameters.");
    deviceConfigured = false;
    return;
  }

  http.end();

  device.setId(configDeviceId.c_str());
  device.connectSecure(wifiClient, configAccessKey.c_str(), configAccessSecret.c_str());

  while (!device.connected()) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Connected!");
  Serial.println("This device is now ready for use!");
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(2000);

  // 6 config fields each given 120 characters.
  EEPROM.begin(721);

  // Wait for serial to initialize.
  while(!Serial) { }

  Serial.println("Device Started");
  Serial.println("-------------------------------------");
  Serial.println("Running Nodevember Workshop Firmware!");
  Serial.println("-------------------------------------");

  if(EEPROM.read(720) != 88) {
    Serial.println("Config has not yet been stored. Please send configuration parameters.");
    return;
  }

  getConfig();
  deviceConfigured = true;

  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  device.onCommand(&handleCommand);
}

void buttonPressed() {
  Serial.println("Button Pressed!");
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["button"] = true;
  device.sendState(root);
}

void reportTemp(double degreesC, double degreesF) {
  if(configTmpEnabled.compareTo("true") == 0) {
    Serial.println();
    Serial.print("Temperature C: ");
    Serial.println(degreesC);
    Serial.print("Temperature F: ");
    Serial.println(degreesF);
    Serial.println();
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["tempC"] = degreesC;
    root["tempF"] = degreesF;
    device.sendState(root);
  }
}

int buttonState = 0;
int currentRead = 0;
int timeSinceLastRead = 0;
int tempSum = 0;
int tempCount = 0;

void loop() {

  bool toReconnect = false;

  if (Serial.available() > 0) {
    saveConfig();
    connect();
  }

  if (deviceConfigured != true) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Disconnected from WiFi");
    toReconnect = true;
  }

  if (!device.connected()) {
    Serial.println("Disconnected from MQTT");
    Serial.println(device.mqttClient.state());
    toReconnect = true;
  }

  if (toReconnect) {
    connect();
  }

  device.loop();

  currentRead = digitalRead(BUTTON_PIN);

  if (currentRead != buttonState) {
    buttonState = currentRead;
    if (buttonState) {
      buttonPressed();
    }
  }

  tempSum += analogRead(A0);
  tempCount++;

  // Report every 15 seconds.
  if (timeSinceLastRead > 15000) {
    // Take the average reading over the last 15 seconds.
    double raw = (double)tempSum / (double)tempCount;

    // The tmp36 documentation requires the -0.5 offset, but during
    // testing while attached to the Feather, all tmp36 sensors
    // required a -0.52 offset for better accuracy.
    double degreesC = (((raw / 1024.0) * 3.2) - 0.5) * 100.0;
    double degreesF = degreesC * 1.8 + 32;

    reportTemp(degreesC, degreesF);

    timeSinceLastRead = 0;
    tempSum = 0;
    tempCount = 0;
  }

  timeSinceLastRead += 50;

  delay(50);
}

// Saves the config to EEPROM.
void saveConfig() {

  Serial.println("");
  Serial.println("Received Serial input. Parsing as JSON: ");
  DynamicJsonBuffer jsonBuffer;
  String byteRead = Serial.readString();
  Serial.println(byteRead);
  JsonObject& root = jsonBuffer.parseObject(byteRead);
  root.printTo(Serial);
  Serial.println();

  if(root.containsKey("losant-config-clear")) {
    if(String((const char*) root["losant-config-clear"]).compareTo("true") == 0) {
      Serial.println("Clearing config from EEPROM and restarting board.");
      EEPROM.write(720, 0);
      EEPROM.commit();
      EEPROM.end();
      ESP.restart();
      return;
    }
  }
  
  if (root.containsKey("losant-config-wifi-ssid")) {
    saveConfigValue(String((const char*) root["losant-config-wifi-ssid"]), 0);
  }

  if (root.containsKey("losant-config-wifi-pass")) {
    saveConfigValue(String((const char*) root["losant-config-wifi-pass"]), 120);
  }

  if (root.containsKey("losant-config-device-id")) {
    saveConfigValue(String((const char*) root["losant-config-device-id"]), 240);
  }

  if (root.containsKey("losant-config-access-key")) {
    saveConfigValue(String((const char*) root["losant-config-access-key"]), 360);
  }

  if (root.containsKey("losant-config-access-secret")) {
    saveConfigValue(String((const char*) root["losant-config-access-secret"]), 480);
  }
  
  if (root.containsKey("losant-config-tmp")) {
    saveConfigValue(String((const char*) root["losant-config-tmp"]), 600);
  }

  Serial.println("Config saved. Restarting device.");
  // Special number 88 and index 720 means we have config to read.
  EEPROM.write(720, 88);
  EEPROM.commit();
  ESP.restart();
}

void saveConfigValue(String value, int addr) {
  byte val[120];
  value.getBytes(val, sizeof(val));
  for(int i = 0; i < value.length(); i++) {
    EEPROM.write(addr, val[i]);
    addr++;
  }
  // null terminate the string.
  EEPROM.write(addr, 0);
}

void getConfig() {

  Serial.println("Attempting to read config from EEPROM.");
  
  configWifiSSID = getConfigValue(0);
  configWifiPass = getConfigValue(120);
  configDeviceId = getConfigValue(240);
  configAccessKey = getConfigValue(360);
  configAccessSecret = getConfigValue(480);
  configTmpEnabled = getConfigValue(600);

  Serial.println("Configuration loaded.");
  Serial.print("WiFi SSID: ");
  Serial.println(configWifiSSID);
  Serial.print("WiFi Pass: ");
  Serial.println(configWifiPass);
  Serial.print("Device ID: ");
  Serial.println(configDeviceId);
  Serial.print("Access Key: ");
  Serial.println(configAccessKey);
  Serial.print("Access Secret: ");
  Serial.println(configAccessSecret);
  Serial.print("Temperature enabled: ");
  Serial.println(configTmpEnabled);
}

String getConfigValue(int addr) {
  byte val[120];
  for(int i = 0; i < 120; i++) {
    byte result = EEPROM.read(addr + i);
    val[i] = result;
    
    if(result == 0) {
      return String((char*)val);
    }
  }

  Serial.println("Failed to read config value.");
  return String("");
}

