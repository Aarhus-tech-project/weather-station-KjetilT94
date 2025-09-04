#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino_LED_Matrix.h>

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;
ArduinoLEDMatrix matrix;

const char* ssid = "h4prog";
const char* password = "1234567890";

const char* mqtt_server = "192.168.115.10";
WiFiClient espClient;
PubSubClient client(espClient);

const float TEMP_THRESHOLD = 50;
const float PRES_THRESHOLD = 100;


const float MIN_TEMP = -40;
const float MAX_TEMP = 85;
const float MIN_PRESSURE = 300;
const float MAX_PRESSURE = 1100;
const float MIN_HUMIDITY = 0;
const float MAX_HUMIDITY = 100;

float lastTemp = 0, lastHum = 0, lastPres = 0;
int stuckReadingCount = 0;
const int MAX_STUCK_READINGS = 3;

bool initializeBME280();
void configureBME280();
bool hardResetBME280();
bool isValidReading(float temp, float hum, float pres);
bool isStuckReading(float temp, float hum, float pres);
void showWarningPattern();
void reconnect();

const uint32_t warning_frames[][4] = {
    {
    0x19819981,
    0x81198119,
    0x81198119,
    0x19819981
  },
  {
    0x0,
    0x0,
    0x0,
    0x0
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Serial started");

  matrix.begin();

  Wire.begin();
  Wire.setClock(100000);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("Herro");
  }
  Serial.println("WiFi connected");

  if (!initializeBME280()) {
    Serial.println("Failed to initialize BME280 after multiple attempts!");
    while (1) {
      matrix.loadFrame(warning_frames[0]);
      delay(1000);
      matrix.loadFrame(warning_frames[1]);
      delay(1000);
    }
  }

  client.setServer(mqtt_server, 1883);
}

bool initializeBME280() {
  Serial.println("Initializing BME280...");

  for (int attempt = 0; attempt < 5; attempt++) {
    Serial.print("Attempt ");
    Serial.println(attempt + 1);

    if (bme.begin(0x76)) {
      Serial.println("BME280 found at address 0x76");
      configureBME280();
      return true;
    }

    if (bme.begin(0x77)) {
      Serial.println("BME280 found at address 0x77");
      configureBME280();
      return true;
    }

    Serial.println("BME280 not found, retrying...");
    delay(1000);

    Wire.end();
    delay(100);
    Wire.begin();
    Wire.setClock(100000);
    delay(500);
  }

  return false;
}

void configureBME280() {
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X2,
                  Adafruit_BME280::SAMPLING_X16,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_X16,
                  Adafruit_BME280::STANDBY_MS_500);
}

bool hardResetBME280() {
  Serial.println("Performing hard reset of BME280...");

  Wire.end();
  delay(100);

  Wire.begin();
  Wire.setClock(100000);
  delay(500);

  return initializeBME280();
}

bool isValidReading(float temp, float hum, float pres) {
  if (isnan(temp) || isnan(hum) || isnan(pres)) {
    Serial.println("NaN values detected");
    return false;
  }

  if (temp < MIN_TEMP || temp > MAX_TEMP) {
    Serial.print("Temperature out of range: ");
    Serial.println(temp);
    return false;
  }

  if (pres < MIN_PRESSURE || pres > MAX_PRESSURE) {
    Serial.print("Pressure out of range: ");
    Serial.println(pres);
    return false;
  }

  if (hum < MIN_HUMIDITY || hum > MAX_HUMIDITY) {
    Serial.print("Humidity out of range: ");
    Serial.println(hum);
    return false;
  }

  return true;
}

bool isStuckReading(float temp, float hum, float pres) {
  if (abs(temp - lastTemp) < 0.01 &&
      abs(hum - lastHum) < 0.01 &&
      abs(pres - lastPres) < 0.01) {
    stuckReadingCount++;
    Serial.print("Identical reading #");
    Serial.println(stuckReadingCount);

    if (stuckReadingCount >= MAX_STUCK_READINGS) {
      Serial.println("Sensor appears to be stuck!");
      return true;
    }
  } else {
    stuckReadingCount = 0;
  }

  lastTemp = temp;
  lastHum = hum;
  lastPres = pres;

  return false;
}

void showWarningPattern() {
  for (int i = 0; i < 5; i++) {
    matrix.loadFrame(warning_frames[0]);
    delay(200);
    matrix.loadFrame(warning_frames[1]);
    delay(200);
  }
  matrix.clear();
}

void reconnect () {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("VerjStationClient")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  bme.takeForcedMeasurement();
  delay(100);

  float temp = bme.readTemperature();
  float hum  = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;

  if (!isValidReading(temp, hum, pres)) {
    Serial.println("Invalid readings detected - attempting sensor reset");
    showWarningPattern();

    if (!hardResetBME280()) {
      Serial.println("Sensor reset failed! Check connections.");
      delay(10000);
      return;
    }

    Serial.println("Sensor reset successful");
    delay(2000);
    return;
  }

  if (isStuckReading(temp, hum, pres)) {
    Serial.println("Stuck readings detected - resetting sensor");
    showWarningPattern();

    if (!hardResetBME280()) {
      Serial.println("Sensor reset failed! Check connections.");
      delay(10000);
      return;
    }

    Serial.println("Sensor unstuck successfully");
    stuckReadingCount = 0;
    delay(2000);
    return;
  }

  if (temp > TEMP_THRESHOLD || pres <= PRES_THRESHOLD) {
    Serial.println("Critical thresholds exceeded!");
    showWarningPattern();
  }

  char payload[128];
  snprintf(payload, sizeof(payload),
          "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f}",
          temp, hum, pres);

  client.publish("verjstation/data", payload);
  Serial.println(payload);

  delay(5000);
}