/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
   Has a characteristic of: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E - used for receiving data with "WRITE"
   Has a characteristic of: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E - used to send data with  "NOTIFY"

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   In this example rxValue is the data received (only accessible inside that function).
   And txValue is the data to be sent, in this example just a byte incremented every second.
*/
#include "BluetoothSerial.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <SparkFunTSL2561.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Wire.h>

boolean gain, good; 
String dataMessage;
unsigned int ms, data0, data1;
double lux;
unsigned char ID;
SFE_TSL2561 light;
int LED_BUILTIN = 2;
BluetoothSerial SerialBT;
DHT dht(15, DHT11); //pin sensor dht di pin 15

File sensorData;

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
float txValue = 0;
const int readPin = 32; // Use GPIO number. See ESP32 board pinouts
const int LED = 2; // Could be different depending on the dev board. I used the DOIT ESP32 dev board.
const int SSR = 5;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        Serial.println("*********");
        Serial.print("Received Value: ");

        for (int i = 0; i < rxValue.length(); i++) {
          Serial.print(rxValue[i]);
        }

        Serial.println();

        // Do stuff based on the command received from the app
        if (rxValue.find("A") != -1) {
          Serial.print("Turning ON!");
          digitalWrite(LED_BUILTIN, HIGH);
        }
        else if (rxValue.find("B") != -1) {
          Serial.print("Turning OFF!");
          digitalWrite(LED_BUILTIN, LOW);
        }

        Serial.println();
        Serial.println("*********");
      }
    }
};

void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void logSDCard() {
  Serial.print("Save data: ");
  Serial.println(dataMessage);
  appendFile(SD, "/DataLogCS.csv", dataMessage.c_str());
}
void printError(byte error) { //Error print from gy2561
  Serial.print("I2C error: ");
  Serial.print(error,DEC);
  Serial.print(", ");
  
  switch(error)
  {
    case 0:
      Serial.println("success");
      break;
    case 1:
      Serial.println("data too long for transmit buffer");
      break;
    case 2:
      Serial.println("received NACK on address (disconnected?)");
      break;
    case 3:
      Serial.println("received NACK on data");
      break;
    case 4:
      Serial.println("other error");
      break;
    default:
      Serial.println("unknown error");
  }
}

void setup() {
  Serial.begin(115200);
  light.begin();

  if (!SD.begin()) { //SD indicator
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  if (light.getID(ID)) { //GY2561 indicator
    Serial.print("Got factory ID: 0X");
    Serial.print(ID, HEX);
    Serial.println(", should be 0X5X");
  }
  else {
    byte error = light.getError();
    printError(error);
  }

  gain = 0;
  unsigned char time = 2;

  light.setTiming(gain, time, ms);
  light.setPowerUp();
  writeFile(SD, "/DataLogCS.csv", "Humidity,Temperature(C),Light0, Light1, Lumunosity\n");


  if (!SerialBT.begin("ESP32")) {
    Serial.println("An error occurred initializing Bluetooth");
  }
  pinMode(LED, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Create the BLE Device
  BLEDevice::init("ESL 2019 PENS x UNAIR"); // Give it a name

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");
}




void loop() {
  while (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }

  light.getData(data0, data1);
  light.getLux(gain, ms, data0, data1, lux);

  int kelembapan = dht.readHumidity();
  int suhu = dht.readTemperature();
  char kelembapanStr[2];
  char suhuStr[2];
  char luxStr[2];
  dtostrf(kelembapan, 1, 2, kelembapanStr);
  dtostrf(suhu, 1, 2, suhuStr);
  dtostrf(lux, 1, 2, luxStr);
  char dhtStr[16];
  //sprintf(dhtStr, "Tmp : %d, Hmdt :%d, Lintensity: %d", suhu, kelembapan, intensitas);
  sprintf(dhtStr, "T:%d,H:%d,L:%d",suhu, kelembapan, (int)lux );
  
    pCharacteristic->setValue(dhtStr);

    pCharacteristic->notify(); // Send the value to the app!

    
        if (suhu >= 31) {
          Serial.println("Temperature more than 55, Exhaust ON ! ");
          digitalWrite(SSR, HIGH);
          digitalWrite(LED_BUILTIN, HIGH);
        }
        else if (suhu < 31) {
          Serial.println("Temperature less than 55, Exhast OFF !");
          digitalWrite(SSR, LOW); 
          digitalWrite(LED_BUILTIN, LOW);
        }
  
    dataMessage = String(kelembapan) + "," + String(suhu) + "," + String(data0) + "," + String(data1) + "," + String(lux) + "\r\n";

    logSDCard();
  delay(1000);
}
