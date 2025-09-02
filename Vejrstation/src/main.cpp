#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;

const char* ssid = "h4prog";
const char* password = "1234567890";

const char* mqtt_server = "192.168.115.10";
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  Serial.println("Serial started");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("Herro");
  }
  Serial.println("WiFi connected");

  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
  client.setServer(mqtt_server, 1883);
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

  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;



  String payload = String("{\"temperature\":") + temp +
                   ",\"humidity\":" + hum +
                   ",\"pressure\":" + pres + "}";

  client.publish("verjstation/data", payload.c_str());

  delay(5000);
}