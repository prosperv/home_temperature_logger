#include "DHTesp.h"
#include <Arduino.h>
#include "ThingSpeak.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

#ifdef DEBUG
#define SERIAL_BEGIN(x) Serial.begin(x)
#define PRINT(x) Serial.print(x)
#define PRINTLN(x) Serial.println(x)
#define PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SERIAL_BEGIN(x)
#define PRINT(x)
#define PRINTLN(x)
#define PRINTF(...)
#endif

DHTesp dht;

const char ssid[] = SECRET_SSID; //  your network SSID (name)
const char pass[] = SECRET_PASS; // your network password

char mqttUserName[] = "HomeTemperatureLogger"; // Use any name.
char mqttPass[] = SECRET_MQTT_API_KEY;         // Change to your MQTT API Key from Account > MyProfile.

const char *mqtt_server = "mqtt.thingspeak.com";
WiFiClient espClient;
PubSubClient mqttClient(mqtt_server, 1883, espClient);

const unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;

static const char alphanum[] = "0123456789"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz"; // For random generation of client ID.

unsigned long lastCycleTime = 0;
#define SECONDS_TO_MILLISECONDS(x) x * 1000L
#define SECONDS_TO_MICROSECONDS(x) x * 1000000L
#define PERIOD_TIME_SECONDS 20
#define EXECUTION_TIME 1
const unsigned long PERIOD_TIME_MS = SECONDS_TO_MILLISECONDS(PERIOD_TIME_SECONDS);
const unsigned long PERIOD_TIME_US = SECONDS_TO_MICROSECONDS(PERIOD_TIME_SECONDS);

bool reconnect()
{
  bool ret = false;
  char clientID[9];

  // Loop until reconnected.
  for (int retry = 0; retry < 5; retry++)
  {
    // Generate ClientID
    for (int i = 0; i < 8; i++)
    {
      clientID[i] = alphanum[random(51)];
    }
    clientID[8] = '\0';

    // Connect to the MQTT broker.
    if (mqttClient.connect(clientID, mqttUserName, mqttPass))
    {
      ret = true;
      break;
    }
    else
    {
      PRINT("failed, rc=");
      // Print reason the connection failed.
      // See https://pubsubclient.knolleary.net/api.html#state for the failure code explanation.
      PRINT(mqttClient.state());
      PRINTLN(" try again in 5 seconds");
      delay(5000);
    }
  }
  return ret;
}

bool publishReadings(const float &temperatureF, const float &humdity, const float &heatIndex)
{
  // Create data string to send to ThingSpeak.
  String data = String("field1=") + String(temperatureF, 1) + "&field2=" + String(humdity, 2) + "&field3=" + String(heatIndex, 2);
  const char *msgBuffer;
  msgBuffer = data.c_str();

  // Create a topic string and publish data to ThingSpeak channel feed.
  String topicString = "channels/" + String(myChannelNumber) + "/publish/" + String(myWriteAPIKey);
  const char *topicBuffer;
  topicBuffer = topicString.c_str();
  bool ret = mqttClient.publish(topicBuffer, msgBuffer);
  delay(100); // Ensure packet has been written
  return ret;
}

void waitWhileConnecting()
{
  do
  {
    PRINT(".");
    delay(1000);
  } while (wifi_station_get_connect_status() == STATION_CONNECTING);
}

void setup()
{
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  
  SERIAL_BEGIN(115200);
  PRINTLN();
  String thisBoard = ARDUINO_BOARD;
  PRINTLN(thisBoard);

  // Wait for DHT22 to power up.
  delay(2000);

  dht.setup(D1, DHTesp::DHT22);

void loop()
{
  while (true)
  {

    auto dhtReading = dht.getTempAndHumidity();
    auto dhtStatus = dht.getStatus();

    const auto temperatureF = dht.toFahrenheit(dhtReading.temperature);
    const auto humidity = dhtReading.humidity;
    const auto heatIndex = dht.computeHeatIndex(temperatureF, humidity, true);

    if (dhtStatus != DHTesp::DHT_ERROR_t::ERROR_NONE)
    {
      PRINTLN(dht.getStatusString());
      //Try again however DHT22 needs 2 seconds between reads
      delay(2000);
      continue;
    }
    else
    {
      PRINTLN("DHT reading OK");
      PRINTF("Temperature(F): %.1f\tHumidity: %.1f\tHeat Index: %.1f\n",
             temperatureF, humidity, heatIndex);
    }


    auto wifiStatus = WiFi.status();
    for (int retry = 0; retry < 5; retry++)
    {
      PRINT("-");
      WiFi.mode(WiFiMode_t::WIFI_STA);
      WiFi.begin(ssid, pass);
      waitWhileConnecting();
      wifiStatus = WiFi.status();
      if (wifiStatus == WL_CONNECTED)
      {
        PRINTLN("Wifi connected");
        break;
      }
      delay(1000);
    }
    if (wifiStatus != WL_CONNECTED)
    {
      PRINTF("Failed to connect to wifi: %d\n", (int)wifiStatus);
      WiFi.forceSleepBegin();
      delay(2000);
      continue;
    }

    if (!mqttClient.connected())
    {
      if (!reconnect())
      {
        //Unable to reconnect to mqtt server. Retry from begining.
        PRINTLN("Unable to reconnect to mqtt server.");
        delay(2000);
        continue;
      }
    }
    if (!publishReadings(temperatureF, humidity, heatIndex))
    {
      PRINTLN("Unable to publish readings.");
    }
    else
    {
      PRINTLN("Uploaded readings");
    }

    PRINTLN("Going to deep sleep");
    ESP.deepSleep(PERIOD_TIME_US, WAKE_RF_DEFAULT);
    // ESP.deepSleep(PERIOD_TIME_US, WAKE_RFCAL);
    // ESP.deepSleep(PERIOD_TIME_US, WAKE_NO_RFCAL);
    // ESP.deepSleep(PERIOD_TIME_US, WAKE_RF_DISABLED);
    PRINTLN("Force sleep failed");
  }
}