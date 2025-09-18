#include <SPI.h>
#include <RH_RF95.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Arduino_LED_Matrix.h"

// == LoRa Pinout ==
#define RFM95_CS   4
#define RFM95_RST  2
#define RFM95_INT  3
#define RF95_FREQ 433.0

// == WiFi & MQTT Config ==
const char* ssid = "h4prog";
const char* password = "1234567890";
const char* mqtt_server = "192.168.115.10";
const char* mqtt_topic_basic = "verjstation/data";           // Basic data (original format)
const char* mqtt_topic_enhanced = "weatherstation/enhanced"; // Enhanced data with calculations

ArduinoLEDMatrix matrix;

const uint32_t sunny[][4] = {
  { 0x40404040, 0x404e4040, 0x4040404, 0x40404040 }  // Sun with rays
};

const uint32_t sunscreen[][4] = {
  { 0x7e424242, 0x7e7e7e7e, 0x7e7e7e7e, 0x7e7e7e7e }  // Sunscreen bottle
};

const uint32_t rain[][4] = {
  { 0x1c7c1c00, 0x7efe7e00, 0x08080808, 0x08080808 }  // Rain cloud with drops
};

const uint32_t cold[][4] = {
  { 0x18244282, 0x42241818, 0x24424218, 0x82244200 }  // Snowflake pattern
};

const uint32_t hot[][4] = {
  { 0x18244281, 0x81422418, 0x18244281, 0x81422418 }  // Heat waves
};

const uint32_t humid[][4] = {
  { 0x18244200, 0x81428100, 0x18244200, 0x81428100 }  // Water droplets
};

const uint32_t warning[][4] = {
  { 0x10381010, 0x10381010, 0x10001010, 0x10101010 }  // Exclamation mark
};

const uint32_t pleasant[][4] = {
  { 0x24244281, 0x00004200, 0x42008100, 0x81424200 }  // Smiley face
};

const uint32_t umbrella[][4] = {
  { 0x3c7e7e3c, 0x18181818, 0x18181818, 0x18303000 }  // Umbrella
};

const uint32_t jacket[][4] = {
  { 0x3c666666, 0x66666666, 0x66666666, 0x3c3c3c3c }  // Jacket/clothing
};

RH_RF95 rf95(RFM95_CS, RFM95_INT);
WiFiClient espClient;
PubSubClient client(espClient);

// Connection status
bool wifiConnected = false;
bool mqttConnected = false;
unsigned long lastConnectionAttempt = 0;
const unsigned long CONNECTION_RETRY_INTERVAL = 30000; // 30 seconds

float calculateDewPoint(float temperature, float humidity) {
  float a = 17.27;
  float b = 237.7;
  float alpha = ((a * temperature) / (b + temperature)) + log(humidity / 100.0);
  return (b * alpha) / (a - alpha);
}

float calculateHeatIndex(float temperature, float humidity) {
  if (temperature < 27.0) {
    return temperature;
  }
  
  float T = temperature;
  float RH = humidity;
  
  float HI = -8.78469475556 + 1.61139411 * T + 2.33854883889 * RH 
           + -0.14611605 * T * RH + -0.012308094 * T * T 
           + -0.0164248277778 * RH * RH + 0.002211732 * T * T * RH 
           + 0.00072546 * T * RH * RH + -0.000003582 * T * T * RH * RH;
  
  return HI;
}

float calculateAbsoluteHumidity(float temperature, float humidity) {
  float dewPoint = calculateDewPoint(temperature, humidity);
  float absoluteHumidity = 216.7 * ((6.112 * exp(17.62 * dewPoint / (243.12 + dewPoint))) / (273.15 + temperature));
  return absoluteHumidity;
}

float calculateSeaLevelPressure(float pressure, float temperature, float altitude = 0.0) {
  if (altitude == 0.0) {
    return pressure;
  }
  float seaLevelPressure = pressure * pow((1 - (0.0065 * altitude) / (temperature + 0.0065 * altitude + 273.15)), -5.257);
  return seaLevelPressure;
}

String getUVRiskLevel(float uvIndex) {
  if (uvIndex < 3) return "Low";
  else if (uvIndex < 6) return "Moderate"; 
  else if (uvIndex < 8) return "High";
  else if (uvIndex < 11) return "Very High";
  else return "Extreme";
}

String getWeatherAdviceAndDisplayLED(float temp, float hum, float pres, float uv) {
  String advice = "";
  bool ledSet = false;
  
  
  if (temp >= 35) {
    advice += "🥵 EXTREME HEAT! Stay hydrated and indoors. ";
    matrix.loadFrame(warning[0]);
    ledSet = true;
  } else if (uv >= 8) {
    advice += "🧴 APPLY SUNSCREEN! UV is Very High/Extreme. ";
    matrix.loadFrame(sunscreen[0]);
    ledSet = true;
  } else if (temp <= 5) {
    advice += "🧥 Dress warmly! Very cold outside. ";
    matrix.loadFrame(cold[0]);
    ledSet = true;
  }
  
  else if (temp >= 30) {
    advice += "🌡️  Very hot! Drink water frequently. ";
    matrix.loadFrame(hot[0]);
    ledSet = true;
  } else if (uv >= 6) {
    advice += "☀️  Wear sunscreen and hat. UV is High. ";
    matrix.loadFrame(sunny[0]);
    ledSet = true;
  } else if (pres <= 1000) {
    advice += "🌧️  Low pressure - rain possible. ";
    matrix.loadFrame(rain[0]);
    ledSet = true;
  } else if (temp <= 15) {
    advice += "🧤 Bring a jacket. It's chilly. ";
    matrix.loadFrame(jacket[0]);
    ledSet = true;
  }
  
  else if (uv >= 3) {
    advice += "🌤️  Consider sun protection. UV is Moderate. ";
    if (!ledSet) {
      matrix.loadFrame(sunny[0]);
      ledSet = true;
    }
  }
  
  if (hum >= 80) {
    advice += "💧 High humidity - feels muggy. ";
    if (!ledSet) {
      matrix.loadFrame(humid[0]);
      ledSet = true;
    }
  } else if (hum <= 30) {
    advice += "🌵 Low humidity - stay hydrated. ";
  }
  
  if (pres >= 1020 && advice.indexOf("pressure") == -1) {
    advice += "☀️  High pressure - clear skies likely. ";
  }
  
  if (advice.length() == 0) {
    advice = "👍 Weather conditions are pleasant!";
    matrix.loadFrame(pleasant[0]);
  } else if (!ledSet) {
    matrix.loadFrame(pleasant[0]);
  }
  
  return advice;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("🌡️🌞 Arduino UNO R4 WiFi - LoRa Weather + UV Station");
  Serial.println("====================================================");
  
  // Initialize LED Matrix
  matrix.begin();
  matrix.loadFrame(sunny[0]);
  Serial.println("✅ LED Matrix (8x12) initialized");

  if (!initializeLoRa()) {
    Serial.println("❌ LoRa initialization failed!");
    matrix.loadFrame(warning[0]);
    while (1) delay(1000);
  }

  initializeWiFi();
  client.setServer(mqtt_server, 1883);
  
  Serial.println("🎧 Ready to receive weather + UV data...");
  Serial.println("");
}

bool initializeLoRa() {
  Serial.println("🔧 Initializing LoRa...");
  
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);

  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  if (!rf95.init()) {
    Serial.println("❌ LoRa radio init failed");
    return false;
  }

  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("❌ setFrequency failed");
    return false;
  }

  rf95.setTxPower(23, false);
  Serial.print("✅ LoRa initialized at ");
  Serial.print(RF95_FREQ);
  Serial.println(" MHz");
  return true;
}

void initializeWiFi() {
  Serial.println("🔧 Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.println("✅ WiFi connected!");
    Serial.print("   IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println();
    Serial.println("⚠️  WiFi connection failed - continuing without database");
  }
}

void reconnectMQTT() {
  if (!wifiConnected) return;
  
  if (millis() - lastConnectionAttempt < CONNECTION_RETRY_INTERVAL) {
    return;
  }
  lastConnectionAttempt = millis();
  
  String clientId = "VerjStationClient";
  clientId += String(millis() % 10000);
  
  Serial.print("🔄 Attempting MQTT connection with ID: ");
  Serial.print(clientId);
  Serial.print("...");
  
  if (client.connect(clientId.c_str())) {
    mqttConnected = true;
    Serial.println(" ✅ Connected!");
  } else {
    mqttConnected = false;
    Serial.print(" ❌ Failed, rc=");
    Serial.print(client.state());
    
    switch(client.state()) {
      case -4: Serial.println(" (Connection timeout)"); break;
      case -3: Serial.println(" (Connection lost)"); break;
      case -2: Serial.println(" (Connect failed - ID rejected)"); break;
      case -1: Serial.println(" (Disconnected)"); break;
      case 1: Serial.println(" (Bad protocol)"); break;
      case 2: Serial.println(" (Bad client ID)"); break;
      case 3: Serial.println(" (Server unavailable)"); break;
      case 4: Serial.println(" (Bad credentials)"); break;
      case 5: Serial.println(" (Not authorized)"); break;
      default: Serial.println(" (Unknown error)"); break;
    }
  }
}

void publishToDatabase(float temp, float hum, float pres, float uvIndex, int packetNum, int rssi) {
  if (!wifiConnected || !mqttConnected) {
    if (wifiConnected) reconnectMQTT();
    return;
  }

  float dewPoint = calculateDewPoint(temp, hum);
  float heatIndex = calculateHeatIndex(temp, hum);
  float absoluteHumidity = calculateAbsoluteHumidity(temp, hum);
  float seaLevelPressure = calculateSeaLevelPressure(pres, temp);
  String uvRisk = getUVRiskLevel(uvIndex);
  
  char basicPayload[160];
  snprintf(basicPayload, sizeof(basicPayload),
          "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f}",
          temp, hum, pres);

  bool basicSuccess = client.publish(mqtt_topic_basic, basicPayload);
  
  char enhancedPayload[500];
  snprintf(enhancedPayload, sizeof(enhancedPayload),
          "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,\"uv_index\":%.2f,\"uv_risk\":\"%s\",\"dew_point\":%.2f,\"heat_index\":%.2f,\"absolute_humidity\":%.2f,\"sea_level_pressure\":%.2f,\"rssi\":%d}",
          temp, hum, pres, uvIndex, uvRisk.c_str(), dewPoint, heatIndex, absoluteHumidity, seaLevelPressure, rssi);

  bool enhancedSuccess = client.publish(mqtt_topic_enhanced, enhancedPayload);

  if (basicSuccess && enhancedSuccess) {
    Serial.println("   💾 Data saved to database ✅");
    Serial.println("   🧮 Enhanced data calculated and saved ✅");
  } else {
    Serial.print("   💾 Database save - Basic: ");
    Serial.print(basicSuccess ? "✅" : "❌");
    Serial.print(", Enhanced: ");
    Serial.println(enhancedSuccess ? "✅" : "❌");
    if (!basicSuccess || !enhancedSuccess) {
      mqttConnected = false;
    }
  }
  
  Serial.print("   📤 Basic MQTT: ");
  Serial.println(basicPayload);
  Serial.print("   🧮 Enhanced MQTT: ");
  Serial.println(enhancedPayload);
}

void parseAndDisplayWeatherData(char* message, uint8_t len, int rssi) {
  Serial.println("📦 New Weather + UV Data Received!");
  Serial.print("   📤 Raw Data: ");
  Serial.println(message);
  
  float temp = -999, hum = -999, pres = -999, uvIndex = -999;
  int packetNum = -1;
  
  char* tPos = strstr(message, "\"t\":");
  if (tPos) temp = atof(tPos + 4);
  
  char* hPos = strstr(message, "\"h\":");
  if (hPos) hum = atof(hPos + 4);
  
  char* pPos = strstr(message, "\"p\":");
  if (pPos) pres = atof(pPos + 4);
  
  char* uvPos = strstr(message, "\"uv\":");
  if (uvPos) uvIndex = atof(uvPos + 5);
  
  char* nPos = strstr(message, "\"n\":");
  if (nPos) packetNum = atoi(nPos + 4);
  
  Serial.println("   🔍 Parsed Weather Data:");
  
  bool validData = true;
  
  if (temp != -999) {
    Serial.print("      🌡️  Temperature: ");
    Serial.print(temp);
    Serial.println(" °C");
  } else {
    Serial.println("      🌡️  Temperature: PARSE ERROR");
    validData = false;
  }
  
  if (hum != -999) {
    Serial.print("      💧 Humidity: ");
    Serial.print(hum);
    Serial.println(" %");
  } else {
    Serial.println("      💧 Humidity: PARSE ERROR");
    validData = false;
  }
  
  if (pres != -999) {
    Serial.print("      🌀 Pressure: ");
    Serial.print(pres);
    Serial.println(" hPa");
  } else {
    Serial.println("      🌀 Pressure: PARSE ERROR");
    validData = false;
  }
  
  if (uvIndex != -999) {
    Serial.print("      ☀️  UV Index: ");
    Serial.print(uvIndex);
    Serial.print(" (");
    Serial.print(getUVRiskLevel(uvIndex));
    Serial.println(")");
  } else {
    Serial.println("      ☀️  UV Index: PARSE ERROR");
    validData = false;
  }
  
  Serial.print("      📡 Signal: ");
  Serial.print(rssi);
  Serial.println(" dBm");
  
  if (packetNum != -1) {
    Serial.print("      📋 Packet #: ");
    Serial.println(packetNum);
  }
  
  if (validData && temp != -999 && hum != -999 && pres != -999 && uvIndex != -999) {
    float dewPoint = calculateDewPoint(temp, hum);
    float heatIndex = calculateHeatIndex(temp, hum);
    float absoluteHumidity = calculateAbsoluteHumidity(temp, hum);
    
    Serial.println("   🧮 Calculated Metrics:");
    Serial.print("      🌫️  Dew Point: ");
    Serial.print(dewPoint);
    Serial.println(" °C");
    
    Serial.print("      🔥 Heat Index: ");
    Serial.print(heatIndex);
    Serial.println(" °C");
    
    Serial.print("      💨 Absolute Humidity: ");
    Serial.print(absoluteHumidity);
    Serial.println(" g/m³");
    
    String advice = getWeatherAdviceAndDisplayLED(temp, hum, pres, uvIndex);
    Serial.println("   💡 Weather Advisory:");
    Serial.print("      ");
    Serial.println(advice);
    Serial.println("   📱 LED Matrix updated with weather symbol");
    
    publishToDatabase(temp, hum, pres, uvIndex, packetNum, rssi);
  } else {
    Serial.println("   ⚠️  Invalid data - not saving to database");
    matrix.loadFrame(warning[0]);
    Serial.println("   📱 LED Matrix showing error symbol");
  }
  
  Serial.println("   ================================");
  Serial.println("");
}

void loop() {
  if (wifiConnected && mqttConnected) {
    client.loop();
  }
  
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    mqttConnected = false;
    Serial.println("⚠️  WiFi connection lost");
  } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    Serial.println("✅ WiFi reconnected");
  }
  
  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);

    if (rf95.recv(buf, &len)) {
      if (len < RH_RF95_MAX_MESSAGE_LEN) {
        buf[len] = '\0';
      } else {
        buf[RH_RF95_MAX_MESSAGE_LEN - 1] = '\0';
      }
      
      parseAndDisplayWeatherData((char*)buf, len, rf95.lastRssi());
      
    } else {
      Serial.println("❌ LoRa receive failed");
    }
  }
  
  delay(10);
}