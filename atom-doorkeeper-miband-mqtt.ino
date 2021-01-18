/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <M5Atom.h>
#include <FastLED.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define MSG_BUFFER_SIZE  (50)

/*Settings*/
/*Wifi network info*/
const char* ssid = "YOURSSID";
const char* password = "PASSWORD";
/*MQTT server address*/
const char* mqtt_server = "x.x.x.x";
const char* mqtt_topic = "YOUR-MQTT-TOPIC";
const char* mqtt_payload = "YOUR-PAYLOAD";

/*REST Service*/
const String endpoint = "http://x.x.x.x:8123/api/states/<entity-id>";
const String key = "Bearer YOURKEY";

static WiFiClient espClient;

/*BLE Scan variables*/
int scanTime = 5; //In seconds
BLEScan* pBLEScan;
BLEScanResults foundDevices;
static BLEAddress *Server_BLE_Address;
String Scaned_BLE_Address;
int16_t Scaned_BLE_Rssi;

/*ATOM variables*/
uint8_t DisBuff[2 + 5 * 5 * 3];

/*MQTT variables*/
PubSubClient client(espClient);
char msg[MSG_BUFFER_SIZE];

/*ArduinoJSON*/
StaticJsonDocument<1024> doc;

/*Definition of RGB colors for ATOM led*/
void setBuff(uint8_t Rdata, uint8_t Gdata, uint8_t Bdata)
{
  DisBuff[0] = 0x05;
  DisBuff[1] = 0x05;
  for (int i = 0; i < 25; i++)
  {
    DisBuff[2 + i * 3 + 0] = Rdata;
    DisBuff[2 + i * 3 + 1] = Gdata;
    DisBuff[2 + i * 3 + 2] = Bdata;
  }
}

/*Subscribe callback for MQTT*/
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    setBuff(0x00, 0x00, 0x40);
    M5.dis.displaybuff(DisBuff);
    delay(1000);
  } else {
    setBuff(0x40, 0x00, 0x00);
    M5.dis.displaybuff(DisBuff);
    delay(1000);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Connecting to the MQTT server...");
    // Create a random client ID
    String clientId = "ATOMLiteClient-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("Connected to the MQTT server!");
      // Once connected, publish an announcement...
      client.publish(mqtt_topic, "hello");
    } else {
      Serial.print("fail, rc=");
      Serial.print(client.state());
      Serial.println("...trying again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Server_BLE_Address = new BLEAddress(advertisedDevice.getAddress());
      Scaned_BLE_Address = Server_BLE_Address->toString().c_str();
      Scaned_BLE_Rssi = advertisedDevice.getRSSI();
    }
};

void setup_wifi() {
  Serial.println("Wifi initializing...");
  WiFi.begin(ssid, password);
  Serial.println("Trying to connect...");
  while (WiFi.status() != WL_CONNECTED) {
    /* LED Blue --> while trying to connect */
    setBuff(0x00, 0x00, 0x40);
    M5.dis.displaybuff(DisBuff);
    delay(1000);
    Serial.println("Connecting to Wifi...");
  }
  /* LED Green --> connected to wifi*/
  setBuff(0x00, 0x40, 0x00);
  M5.dis.displaybuff(DisBuff);
  Serial.println("Wifi connected");
  char infoBuffer[100];
  sprintf(infoBuffer, "IP: %s", WiFi.localIP().toString().c_str());
  Serial.println(infoBuffer);
  delay(3000);
}

void setup_bleScan() {
  Serial.println("Searching devices...");
  /*BLE scanning init*/
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  //pBLEScan->setInterval(1500);
  //pBLEScan->setWindow(1499);  // less or equal setInterval value
}

void setup() {
  Serial.begin(115200);

  /*ATOM start*/
  M5.begin(true, false, true);
  delay(500);

  /* LED red --> starting, without wifi connection */
  setBuff(0x40, 0x00, 0x00);
  M5.dis.displaybuff(DisBuff);

  delay(3000);

  setup_wifi();
  setup_bleScan();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

/*Publish to the mqtt server */
void mqtt_publish() {
  if (!client.connected() && !client.loop()) {
    reconnect();
  }
  client.publish(mqtt_topic, mqtt_payload);
}

void ble_loop() {
  foundDevices = pBLEScan->start(5);
  while (foundDevices.getCount() >= 1) {
    //Check if the ATOM button was pressed
    if (M5.Btn.wasPressed()) {
      Serial.println("Switch activated by pressing the button!");
      //Set LED green
      setBuff(0x00, 0x40, 0x00);
      M5.dis.displaybuff(DisBuff);
      //Publish to mqtt server
      mqtt_publish();
      M5.update();
      break;
    }
    //check if we have an scanned mac address
    if (Scaned_BLE_Address != NULL) {
      //Let's call the auth service
      if ((WiFi.status() == WL_CONNECTED)) {
        HTTPClient http;
        http.begin(endpoint); //Specify the URL
        http.addHeader("Content-Type", "application/json");
        http.addHeader("authorization", key);
        int httpCode = http.GET();  //Make the request
        if (httpCode == 200 || httpCode == 201) { //Check for the returning code
          String payload = http.getString();
          DeserializationError err = deserializeJson(doc, payload);
          if (err) {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(err.c_str());
          } else {
            deserializeJson(doc, payload);
            JsonObject attributes = doc["attributes"];
            JsonArray attributes_ble_keys = attributes["ble_keys"];
            const char* devices_0 = attributes_ble_keys[0]; // MAC_1
            const char* devices_1 = attributes_ble_keys[1]; // MAC_2
            const char* devices_2 = attributes_ble_keys[2]; // MAC_3
            const char* devices_3 = attributes_ble_keys[3]; // MAC_4
            const char* devices_4 = attributes_ble_keys[4]; // MAC_5

            /*Debug scanning*/
            Serial.print("RSSI: ");
            Serial.print(Scaned_BLE_Rssi);
            Serial.print(" MAC: ");
            Serial.println(Scaned_BLE_Address);

            //Check device,
            if (Scaned_BLE_Address == devices_0 || Scaned_BLE_Address == devices_1 || Scaned_BLE_Address == devices_2 || Scaned_BLE_Address == devices_3 || Scaned_BLE_Address == devices_4) {

              if (Scaned_BLE_Rssi >= -76) {
                Serial.println("**Turn on the switch**");
                //Set LED green
                setBuff(0x00, 0x40, 0x00);
                M5.dis.displaybuff(DisBuff);
                //Publish to mqtt server
                mqtt_publish();
                delay(1500);
                break;
              } else {
                blink_loop();
                Serial.println("**Awaiting approach**");
                break;
              }
            } else {
              if (Scaned_BLE_Rssi >= -76) {
                //Set LED red
                setBuff(0x40, 0x00, 0x00);
                M5.dis.displaybuff(DisBuff);
                Serial.println("**Unauthorized device**");
                delay(3000);
                blink_loop();
                break;
              }
            }
          }
        } else {
          Serial.println("Error on HTTP request");
        }
        http.end(); //Free the resources
      }
    }
    pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
    break;
  }
}

void blink_loop() {
  /* LED white blinking --> BLE scanning for devices*/
  setBuff(0x40, 0x00, 0x00);
  M5.dis.displaybuff(DisBuff);
  delay(100);

  setBuff(0x00, 0x00, 0x00);
  M5.dis.displaybuff(DisBuff);
  delay(100);
}

void loop() {
  if ((WiFi.status() == WL_CONNECTED)) {
    ble_loop();
    blink_loop();
    M5.update();
  } else {
    Serial.println("We don't have wifi connection... let's restart");
    ESP.restart();
  }
}
