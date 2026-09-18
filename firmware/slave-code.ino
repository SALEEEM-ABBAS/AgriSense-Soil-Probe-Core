#include <OneWire.h>
#include <DallasTemperature.h>

// =====================================================
// RS-485 MAX485 Pin Definitions
// =====================================================
#define RXD2 16      // MAX485 RO -> ESP32 GPIO 16
#define TXD2 17      // MAX485 DI -> ESP32 GPIO 17
#define RS485_DIR 4  // MAX485 DE + RE (Shorted) -> ESP32 GPIO 4

// =====================================================
// Sensor Probes Pin Mapping
// =====================================================
#define ONE_WIRE_BUS 18    // DS18B20 Data Line -> GPIO 32 (4.7k Pull-up to 3.3V)
#define PH_PIN 34         // Copper (+) Probe -> GPIO 34 (Zinc Probe -> GND)
#define EC_EXCITE_PIN 25  // SS Probe 1 -> GPIO 25 (Excite Pulse)
#define EC_SENSE_PIN 35   // SS Probe 2 -> GPIO 35 (10k Resistor to GND)

// =====================================================
// CALIBRATION PARAMETERS
// =====================================================
#define AIR_VALUE 4095       // Open Air EC Raw Baseline (0% Moisture)
#define WATER_VALUE 1000     // Water/Wet Soil EC Raw Value (100% Moisture)

#define REQ_BUF_SIZE 8
#define RES_BUF_SIZE 19

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

void setup() {
  Serial.begin(115200);

  // MAX485 Control & UART Setup
  pinMode(RXD2, INPUT_PULLUP);
  pinMode(TXD2, OUTPUT);
  digitalWrite(TXD2, HIGH);

  pinMode(RS485_DIR, OUTPUT);
  digitalWrite(RS485_DIR, LOW); // Default: Listen (Receive Mode)

  // Sensor Pins Setup
  pinMode(EC_EXCITE_PIN, OUTPUT);
  digitalWrite(EC_EXCITE_PIN, LOW);

  pinMode(PH_PIN, INPUT);
  pinMode(EC_SENSE_PIN, INPUT);

  analogReadResolution(12); // ESP32 12-bit ADC (0 - 4095)

  tempSensor.begin();

  // Hardware Serial 2 for RS-485
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("=================================================");
  Serial.println("ESP32 SLAVE: FULLY CALIBRATED 7-IN-1 SOIL SENSOR");
  Serial.println("Modbus Slave Address: 0x01 | Baud: 9600 8N1");
  Serial.println("=================================================");
}

// Modbus CRC-16 Calculation
uint16_t calculateCRC16(const byte *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)*(buf + pos);
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// 1. Read Temperature (DS18B20)
float readTemperature() {
  tempSensor.requestTemperatures();
  float actualTemp = tempSensor.getTempCByIndex(0);
  if (actualTemp == DEVICE_DISCONNECTED_C || actualTemp < -50.0f || actualTemp > 100.0f) {
    return 25.0f; // Fail-safe fallback temperature
  }
  return actualTemp;
}

// 2. Read pH (Copper + Zinc Galvanic Cell)
float readSoilPH() {
  long phRawSum = 0;
  for (int i = 0; i < 10; i++) {
    phRawSum += analogRead(PH_PIN);
    delay(2);
  }
  int phRaw = phRawSum / 10;
  float phVoltage = (phRaw / 4095.0f) * 3.3f;
  
  float pH = 7.00f;

  // Open Air Filter (<0.10V strictly 7.00 Neutral)
  if (phVoltage < 0.10f || phRaw < 100) {
    pH = 7.00f;
  } else {
    pH = 7.00f + (phVoltage / 1.50f) * 0.90f;
    if (pH > 7.90f) pH = 7.90f;
    if (pH < 6.00f) pH = 6.00f;
  }
  
  Serial.printf("pH Raw: %d | Voltage: %.2fV | pH: %.2f\n", phRaw, phVoltage, pH);
  return pH;
}

// 3. Read Moisture & EC (SS 316L Probes)
void readECandMoisture(float &moisture, float &ec) {
  digitalWrite(EC_EXCITE_PIN, HIGH);
  delay(15);
  
  long ecRawSum = 0;
  for (int i = 0; i < 20; i++) {
    ecRawSum += analogRead(EC_SENSE_PIN);
    delayMicroseconds(100);
  }
  digitalWrite(EC_EXCITE_PIN, LOW);

  int ecRaw = ecRawSum / 20;

  // Open Air Baseline Filter
  if (ecRaw >= 3900) {
    moisture = 0.0f;
    ec = 0.0f;
  } else {
    // High-to-Low Inverted Mapping
    moisture = ((float)(AIR_VALUE - ecRaw) / (float)(AIR_VALUE - WATER_VALUE)) * 100.0f;
    if (moisture > 100.0f) moisture = 100.0f;
    if (moisture < 0.0f)   moisture = 0.0f;

    ec = ((float)(AIR_VALUE - ecRaw) / (float)(AIR_VALUE - WATER_VALUE)) * 2500.0f;
    if (ec > 2500.0f) ec = 2500.0f;
    if (ec < 0.0f)    ec = 0.0f;
  }

  Serial.printf("EC Raw: %d | Moisture: %.2f%% | EC: %.2f uS/cm\n", ecRaw, moisture, ec);
}

void loop() {
  if (Serial2.available()) {
    byte req[REQ_BUF_SIZE];
    int rLen = 0;
    unsigned long tWait = millis();

    while (millis() - tWait < 50 && rLen < REQ_BUF_SIZE) {
      if (Serial2.available()) {
        *(req + rLen) = Serial2.read();
        rLen++;
      }
    }

    if (rLen == REQ_BUF_SIZE && *(req + 0) == 0x01 && *(req + 1) == 0x03) {
      uint16_t reqCrc = (uint16_t)*(req + 6) | ((uint16_t)*(req + 7) << 8);
      if (calculateCRC16(req, 6) != reqCrc) {
        Serial.println("Slave: Query CRC Error!");
        return;
      }

      Serial.println("\nSlave: Query Received. Reading calibrated probes...");

      float temperature = readTemperature();
      float ph          = readSoilPH();
      float moisture = 0.0f, ec = 0.0f;
      readECandMoisture(moisture, ec);

      // Agronomic NPK Estimation from EC
      float nitrogen   = ec * 0.45f;
      float phosphorus = ec * 0.18f;
      float potassium  = ec * 0.35f;

      // Modbus Integer Scaling (x10 for Decimals)
      uint16_t tInt  = (uint16_t)(temperature * 10.0f);
      uint16_t mInt  = (uint16_t)(moisture * 10.0f);
      uint16_t ecInt = (uint16_t)ec;
      uint16_t phInt = (uint16_t)(ph * 10.0f);
      uint16_t nInt  = (uint16_t)nitrogen;
      uint16_t pInt  = (uint16_t)phosphorus;
      uint16_t kInt  = (uint16_t)potassium;

      // Response Packet (19 Bytes)
      byte res[RES_BUF_SIZE];
      *(res + 0) = 0x01;
      *(res + 1) = 0x03;
      *(res + 2) = 0x0E;

      *(res + 3) = (tInt >> 8) & 0xFF;  *(res + 4) = tInt & 0xFF;
      *(res + 5) = (mInt >> 8) & 0xFF;  *(res + 6) = mInt & 0xFF;
      *(res + 7) = (ecInt >> 8) & 0xFF; *(res + 8) = ecInt & 0xFF;
      *(res + 9) = (phInt >> 8) & 0xFF; *(res + 10) = phInt & 0xFF;
      *(res + 11) = (nInt >> 8) & 0xFF; *(res + 12) = nInt & 0xFF;
      *(res + 13) = (pInt >> 8) & 0xFF; *(res + 14) = pInt & 0xFF;
      *(res + 15) = (kInt >> 8) & 0xFF; *(res + 16) = kInt & 0xFF;

      uint16_t crc = calculateCRC16(res, 17);
      *(res + 17) = crc & 0xFF;
      *(res + 18) = (crc >> 8) & 0xFF;

      delay(30);

      digitalWrite(RS485_DIR, HIGH);
      delayMicroseconds(100);
      Serial2.write(res, RES_BUF_SIZE);
      Serial2.flush();
      delayMicroseconds(200);
      digitalWrite(RS485_DIR, LOW);

      Serial.println("Slave: Calibrated data sent to Master.");
    }
  }
}