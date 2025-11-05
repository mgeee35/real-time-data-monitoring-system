#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi settings
const char* ssid = "AndroidAP";           // Enter your WiFi network name here
const char* password = "btxz4859";        // Enter your WiFi password here

// MQTT settings
const char* mqtt_server = "192.168.43.109"; // Enter your MQTT broker IP address here //192.168.1.100
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Sensor_Client";

// Pin definitions
#define TRIG_PIN 22         // D22 - Distance sensor Trigger pin
#define ECHO_PIN 23         // D23 - Distance sensor Echo pin
#define LDR_PIN 32          // D32 - LDR analog pin
#define MAX_DISTANCE 400    // Maximum distance (cm)

// Time variables for distance sensor
#define SOUND_SPEED 0.034   // Speed of sound in cm/microsecond (34000 cm/s = 0.034 cm/µs)

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Variables
bool distanceSensorEnabled = false;
bool ldrSensorEnabled = false;
int dataRate = 1000; // Default 1 second (in milliseconds)
unsigned long lastSensorRead = 0;
unsigned long lastStatusSend = 0;

// Sensor values
float currentDistance = 0;
int currentLdrValue = 0;
int currentLdrPercent = 0;

// Function declarations
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleControlMessage(JsonDocument& doc);
void handleConfigMessage(JsonDocument& doc);
void readAndSendSensors();
void sendStatusMessage();
void testSensors();
float measureDistance();
void checkWiFiConnection();

void setup() {
  Serial.begin(9600);
  delay(1000);
  
  Serial.println("ESP32 Sensor System Starting...");
  
  // Set pin modes
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  
  // WiFi connection
  setupWiFi();
  
  // MQTT settings
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  
  Serial.println("System ready!");
}

void loop() {
  // Check MQTT connection
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Read and send sensor data
  unsigned long currentTime = millis();
  if (currentTime - lastSensorRead >= dataRate) {
    readAndSendSensors();
    lastSensorRead = currentTime;
  }
  
  // Send status message every 30 seconds
  if (currentTime - lastStatusSend >= 30000) {
    sendStatusMessage();
    lastStatusSend = currentTime;
  }
  
  delay(100);
}

void setupWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (client.connect(mqtt_client_id)) {
      Serial.println(" connected!");
      
      // Subscribe to control messages
      client.subscribe("esp32/control");
      client.subscribe("esp32/config");
      
      Serial.println("Subscribed to MQTT topics");
      
      // Send connection status message
      sendStatusMessage();
      
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("MQTT message received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  String topicStr = String(topic);
  
  if (topicStr == "esp32/control") {
    handleControlMessage(doc);
  } else if (topicStr == "esp32/config") {
    handleConfigMessage(doc);
  }
}

void handleControlMessage(JsonDocument& doc) {
  String type = doc["type"].as<String>();
  
  if (type == "control") {
    String sensor = doc["sensor"].as<String>();
    bool state = doc["state"].as<bool>();
    
    if (sensor == "distance") {
      distanceSensorEnabled = state;
      Serial.print("Distance sensor: ");
      Serial.println(state ? "Enabled" : "Disabled");
    } else if (sensor == "ldr") {
      ldrSensorEnabled = state;
      Serial.print("LDR sensor: ");
      Serial.println(state ? "Enabled" : "Disabled");
    }
    
    // Send status message
    sendStatusMessage();
  }
}

void handleConfigMessage(JsonDocument& doc) {
  String type = doc["type"].as<String>();
  
  if (type == "dataRate") {
    int rate = doc["rate"].as<int>();
    if (rate == 1 || rate == 2 || rate == 3 || rate == 5 || 
        rate == 10 || rate == 15 || rate == 20) {
      
      dataRate = rate * 1000; // Convert seconds to milliseconds
      Serial.print("Data transmission rate set to: ");
      Serial.print(rate);
      Serial.println(" seconds");
      
      // Send status message
      sendStatusMessage();
    }
  }
}

void readAndSendSensors() {
  JsonDocument doc;
  bool hasData = false;
  
  // Read distance sensor
  if (distanceSensorEnabled) {
    float distance = measureDistance();
    if (distance > 0 && distance <= MAX_DISTANCE) {
      currentDistance = distance;
      doc["distance"] = currentDistance;
      hasData = true;
      
      Serial.print("Distance: ");
      Serial.print(currentDistance);
      Serial.println(" cm");
    } else {
      Serial.println("Distance sensor: Invalid reading");
    }
  }
  
  // Read LDR sensor
  if (ldrSensorEnabled) {
    currentLdrValue = analogRead(LDR_PIN);
    currentLdrPercent = map(currentLdrValue, 0, 4095, 0, 100); // ESP32 ADC 12-bit (0-4095)
    doc["ldr"] = currentLdrPercent;
    hasData = true;
    
    Serial.print("LDR Raw Value: ");
    Serial.print(currentLdrValue);
    Serial.print(" - Light Intensity: ");
    Serial.print(currentLdrPercent);
    Serial.println("%");
  }
  
  // Send data via MQTT if available
  if (hasData) {
    doc["timestamp"] = millis();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    if (client.publish("esp32/sensors", jsonString.c_str())) {
      Serial.println("Sensor data sent");
    } else {
      Serial.println("Failed to send sensor data!");
    }
  }
}

void sendStatusMessage() {
  JsonDocument doc;
  
  doc["type"] = "status";
  doc["distanceSensorEnabled"] = distanceSensorEnabled;
  doc["ldrSensorEnabled"] = ldrSensorEnabled;
  doc["dataRate"] = dataRate / 1000; // Convert milliseconds to seconds
  doc["uptime"] = millis();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["wifiRSSI"] = WiFi.RSSI();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  if (client.publish("esp32/status", jsonString.c_str())) {
    Serial.println("Status message sent");
  } else {
    Serial.println("Failed to send status message!");
  }
}

// Sensor test functions
void testSensors() {
  Serial.println("\n=== Sensor Test ===");
  
  // Distance sensor test
  Serial.print("Distance sensor test: ");
  float distance = measureDistance();
  if (distance > 0 && distance <= MAX_DISTANCE) {
    Serial.print(distance);
    Serial.println(" cm");
  } else {
    Serial.println("Distance not readable");
  }
  
  // LDR sensor test
  Serial.print("LDR sensor test: ");
  int ldrValue = analogRead(LDR_PIN);
  int ldrPercent = map(ldrValue, 0, 4095, 0, 100);
  Serial.print("Raw value: ");
  Serial.print(ldrValue);
  Serial.print(" - Percentage: ");
  Serial.print(ldrPercent);
  Serial.println("%");
  
  Serial.println("==================\n");
}

// HC-SR04 distance sensor reading function
float measureDistance() {
  // Clear the Trigger pin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Set Trigger pin HIGH for 10 microseconds
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Measure the duration of the Echo pin signal
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout (~5m distance)
  
  // Return 0 if timeout
  if (duration == 0) {
    return 0;
  }
  
  // Calculate distance (divide by 2 because sound travels to object and back)
  float distance = (duration * SOUND_SPEED) / 2;
  
  // Check if within reasonable range
  if (distance < 2 || distance > MAX_DISTANCE) {
    return 0; // Invalid value
  }
  
  return distance;
}

// Check WiFi connection
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFailed to reconnect WiFi!");
    }
  }
}
