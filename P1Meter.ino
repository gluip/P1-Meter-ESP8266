#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include "CRC16.h"
#include <MQTT.h>


//===Change values from here===
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PWD";

//=== MQTT broker setting===
#define MQTT_USERNAME "MQTT_USER"
#define MQTT_PASSWORD "MQTT_PWD"
#define MQTT_ADDRESS "MQTT_ADDRES"
#define MQTT_PORT 123

const bool outputOnSerial = true;
const String MQTT_TOPIC_GAS = "home/sensors/gas";
const String MQTT_TOPIC_ELECTRICITY = "home/sensors/electricity";
const String MQTT_TOPIC_ELECTRICITY_CONSUMPTION = "home/sensors/electricity/consumption";

//===Change values to here===

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - consumption low tariff
long mEVHT = 0; //Meter reading Electrics - consumption high tariff
long mEOLT = 0; //Meter reading Electrics - return low tariff
long mEOHT = 0; //Meter reading Electrics - return high tariff
long mEAV = 0;  //Meter reading Electrics - Actual consumption
long mEAT = 0;  //Meter reading Electrics - Actual return
long mGAS = 0;    //Meter reading Gas
long prevGAS = 0;


#define MAXLINELENGTH 64 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];

#define SERIAL_RX     D5  // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1, true, MAXLINELENGTH); // (RX, TX. inverted, buffer)
MQTT myMqtt("", MQTT_ADDRESS, MQTT_PORT);
unsigned int currentCRC=0;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  mySerial.begin(9600);

 
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("connect mqtt...");
  connectMQTT();
}

void connectMQTT(){
  Serial.println("Connecting to MQTT server");  
  //set client id
  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  //clientName += "esp8266-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);
  clientName += "-";
  clientName += String(micros() & 0xff, 16);
  myMqtt.setClientId((char*) clientName.c_str());

#ifdef DEBUG
  Serial.print("MQTT client id:");
  Serial.println(clientName);
#endif
  // setup callbacks
  myMqtt.onConnected(myConnectedCb);
  myMqtt.setUserPwd(MQTT_USERNAME, MQTT_PASSWORD);  
  myMqtt.connect();
  delay(500);
}


void myConnectedCb() {
  Serial.println("connected to MQTT server");
}

bool SendToMqqt(String topic,String sValue)
{
  Serial.println("Sending to MQTT topic");
  Serial.println(topic);
  Serial.println(sValue);
  
  return myMqtt.publish(topic, sValue);
}

void UpdateGas()
{
  //sends over the gas setting to domoticz
  if(prevGAS!=mGAS)
  {
    char sValue[10];
    sprintf(sValue, "%d", mGAS);
    if(SendToMqqt(MQTT_TOPIC_GAS, sValue))
      prevGAS=mGAS;
  }
}

void UpdateElectricity()
{
  char sValue[255];
  sprintf(sValue, "%d;%d;%d;%d;%d;%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  SendToMqqt(MQTT_TOPIC_ELECTRICITY,sValue);
}

void UpdateElectricityActualConsumption()
{
  char sValue[255];
  sprintf(sValue, "%d",mEAV);
  SendToMqqt(MQTT_TOPIC_ELECTRICITY_CONSUMPTION,sValue);
}

bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool parseLine(int len) {
  //need to check for start
  if(outputOnSerial)
  {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
      Serial.print('\n');
  }
  
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0) 
    mEVLT =  getValue(telegram, len);
  

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0) 
    mEVHT = getValue(telegram, len);
    

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) 
    mEOLT = getValue(telegram, len);
   

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) 
    mEOHT = getValue(telegram, len);
                                

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) 
    mEAV = getValue(telegram, len);
    
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
    mEAT = getValue(telegram, len);
   

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (strncmp(telegram, "0-1:24.2.1", strlen("0-1:24.2.1")) == 0) 
    mGAS = getValue(telegram, len);
                                      
  return true;
}

void readTelegram() {
  if (mySerial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      for (int i = 0;i<len;i++)
      {
        // --- 7 bits instelling ---
        telegram[i] &= ~(1 << 7);
      }
      if (telegram[0] == '!')
      {
         UpdateElectricity();
         UpdateElectricityActualConsumption();
         UpdateGas();
      }
      
      parseLine(len);
      yield();
      
    } 
  }
}

void loop() {
  readTelegram();
}

String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}


