#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RH_RF95.h>

// LoRa Configuration
#define RFM95_CS   4
#define RFM95_RST  2
#define RFM95_INT  3
#define RF95_FREQ 433.0

#define UV_SENSOR_PIN A0

Adafruit_BME280 bme;
RH_RF95 rf95(RFM95_CS, RFM95_INT);

float lastTemp = 0, lastHum = 0, lastPres = 0, lastUV = 0;
byte stuckCount = 0;
int packetnum = 0;

void setup() {
  Serial.begin(9600);
  Serial.println(F("BME280 + UV LoRa Station"));
  
  Wire.begin();
  Wire.setClock(100000);

  // Initialize LoRa
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  if (!rf95.init()) {
    Serial.println(F("LoRa init failed"));
    while (1) delay(1000);
  }
  
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println(F("Freq failed"));
    while (1) delay(1000);
  }
  
  rf95.setTxPower(23, false);
  Serial.println(F("LoRa OK"));

  if (!bme.begin(0x76) && !bme.begin(0x77)) {
    Serial.println(F("BME280 failed"));
    while (1) delay(1000);
  }
  
  Serial.println(F("BME280 OK"));
  
  Serial.println(F("UV Sensor Ready"));
  
  Serial.println(F("Starting..."));
}

float readUVIndex() {
  int sensorValue = analogRead(UV_SENSOR_PIN);
  float voltage = sensorValue * (5.0 / 1023.0);
  
  float uvIndex = voltage / 0.1;
  
  if (uvIndex < 0) uvIndex = 0;
  if (uvIndex > 15) uvIndex = 15;
  
  return uvIndex;
}

bool isValidReading(float temp, float hum, float pres, float uv) {
  return !isnan(temp) && !isnan(hum) && !isnan(pres) && !isnan(uv) &&
         temp > -40 && temp < 85 &&
         hum >= 0 && hum <= 100 &&
         pres > 300 && pres < 1100 &&
         uv >= 0 && uv <= 15;
}

bool isStuckReading(float temp, float hum, float pres, float uv) {
  if (abs(temp - lastTemp) < 0.01 && 
      abs(hum - lastHum) < 0.01 && 
      abs(pres - lastPres) < 0.01 &&
      abs(uv - lastUV) < 0.01) {
    stuckCount++;
    if (stuckCount >= 3) return true;
  } else {
    stuckCount = 0;
  }
  
  lastTemp = temp;
  lastHum = hum;
  lastPres = pres;
  lastUV = uv;
  return false;
}

void resetBME280() {
  Wire.end();
  delay(100);
  Wire.begin();
  Wire.setClock(100000);
  delay(500);
  bme.begin(0x76) || bme.begin(0x77);
}

void floatToString(char* str, float value, int decimals) {
  int intPart = (int)value;
  int fracPart = abs((int)((value - intPart) * 100));
  
  if (decimals == 1) {
    fracPart = abs((int)((value - intPart) * 10));
    sprintf(str, "%d.%d", intPart, fracPart);
  } else {
    sprintf(str, "%d.%02d", intPart, fracPart);
  }
}

void loop() {
  bme.takeForcedMeasurement();
  delay(100);

  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float uvIndex = readUVIndex();

  Serial.print(F("T:")); Serial.print(temp);
  Serial.print(F(" H:")); Serial.print(hum);
  Serial.print(F(" P:")); Serial.print(pres);
  Serial.print(F(" UV:")); Serial.println(uvIndex);

  if (!isValidReading(temp, hum, pres, uvIndex)) {
    Serial.println(F("Invalid - reset"));
    resetBME280();
    delay(2000);
    return;
  }

  if (isStuckReading(temp, hum, pres, uvIndex)) {
    Serial.println(F("Stuck - reset"));
    resetBME280();
    stuckCount = 0;
    delay(2000);
    return;
  }

  if (temp > 50 || pres <= 100) {
    Serial.println(F("WARNING!"));
  }
  
  if (uvIndex >= 8) {
    Serial.println(F("UV WARNING: Very High!"));
  } else if (uvIndex >= 6) {
    Serial.println(F("UV CAUTION: High"));
  }

  char packet[80];
  char tempStr[10], humStr[10], presStr[10], uvStr[10];
  
  floatToString(tempStr, temp, 1);
  floatToString(humStr, hum, 1);  
  floatToString(presStr, pres, 1);
  floatToString(uvStr, uvIndex, 1);
  
  sprintf(packet, "{\"t\":%s,\"h\":%s,\"p\":%s,\"uv\":%s,\"n\":%d}", 
          tempStr, humStr, presStr, uvStr, packetnum++);

  Serial.print(F("TX: "));
  Serial.println(packet);

  if (rf95.send((uint8_t *)packet, strlen(packet))) {
    rf95.waitPacketSent();
    Serial.println(F("Sent OK"));
  } else {
    Serial.println(F("Send fail"));
  }

  delay(5000);
}