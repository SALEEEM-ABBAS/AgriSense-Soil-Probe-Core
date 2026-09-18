#include <ArduinoJson.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

#include <ModbusMaster.h>

#define RXD2 16      // MAX485 RO -> ESP32 GPIO 16
#define TXD2 17      // MAX485 DI -> ESP32 GPIO 17
#define RS485_DIR 4  // MAX485 DE + RE -> ESP32 GPIO 4
#define RS485LED 22

int errorcnt = 0;
int cycle = 0;
float moisture, temperature, conductivity, pH, nitrogen, potassium, phosphorus;
uint8_t result;
String id = "M25-2603250003";

unsigned long previousMillis = 0;
const long interval = 5000;
int count = 0;

BluetoothSerial SerialBT;
ModbusMaster node;

// Hardware DE/RE Line Control
void preTransmission() {
  digitalWrite(RS485_DIR, HIGH); // Transmit Mode
  delay(2);
}

void postTransmission() {
  delay(3); // Line travel delay
  digitalWrite(RS485_DIR, LOW);  // Receive Mode
}

void function();

void setup() {
  SerialBT.begin("Crop2X-M25");
  Serial.begin(115200);

  pinMode(RS485LED, OUTPUT);
  digitalWrite(RS485LED, LOW);

  pinMode(RS485_DIR, OUTPUT);
  digitalWrite(RS485_DIR, LOW);

  pinMode(RXD2, INPUT_PULLUP);
  pinMode(TXD2, OUTPUT);
  digitalWrite(TXD2, HIGH);

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("=========================================");
  Serial.println("Crop2X-M25 Master Controller Initialized");
  Serial.println("Bluetooth & Modbus-RTU Master Ready");
  Serial.println("=========================================");

  delay(1000);
}

void loop() {
  // Read 7 Holding Registers (0 to 6) from Slave
  result = node.readHoldingRegisters(0, 7);
  function();
  delay(2000); // Poll every 2 seconds
}

void function() {
  if (result == node.ku8MBSuccess) {
    digitalWrite(RS485LED, LOW);

    temperature  = node.getResponseBuffer(0) / 10.0f;
    moisture     = node.getResponseBuffer(1) / 10.0f;
    conductivity = node.getResponseBuffer(2);
    pH           = node.getResponseBuffer(3) / 10.0f;
    nitrogen     = node.getResponseBuffer(4);
    phosphorus   = node.getResponseBuffer(5);
    potassium    = node.getResponseBuffer(6);

    Serial.println("-----------------------------------------");
    Serial.println("Actual Temp     : " + String(temperature) + " °C");
    Serial.println("Soil Moisture   : " + String(moisture) + " %");
    Serial.println("Conductivity EC : " + String(conductivity) + " uS/cm");
    Serial.println("Soil pH         : " + String(pH));
    Serial.println("Nitrogen (N)    : " + String(nitrogen) + " mg/kg");
    Serial.println("Phosphorus (P)  : " + String(phosphorus) + " mg/kg");
    Serial.println("Potassium (K)   : " + String(potassium) + " mg/kg");
    Serial.println("-----------------------------------------");

    StaticJsonDocument<256> doc;
    doc["id"] = id;
    doc["t"]  = temperature;
    doc["m"]  = moisture;
    doc["c"]  = conductivity;
    doc["pH"] = pH;
    doc["n"]  = nitrogen;
    doc["p"]  = phosphorus;
    doc["k"]  = potassium;

    // Stream over Bluetooth
    serializeJson(doc, SerialBT);
    SerialBT.println();

    // Stream over USB Serial
    Serial.print("JSON Data Payload: ");
    serializeJson(doc, Serial);
    Serial.println();

    Serial.print("ERROR count: "); Serial.println(errorcnt);    
    Serial.print("cycle: ");       Serial.println(cycle);  
    cycle++;
  } else {
    errorcnt++;
    cycle++;
    Serial.print("ERROR count: "); Serial.print(errorcnt);
    Serial.print(" | Modbus Error Code: 0x"); Serial.println(result, HEX);

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      if (count == 0) {
        digitalWrite(RS485LED, HIGH);
        count = 1;
        delay(500);
        digitalWrite(RS485LED, LOW);
      } else {
        digitalWrite(RS485LED, LOW);
        count = 0;
      }
    }
  }
}