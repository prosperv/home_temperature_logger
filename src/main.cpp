#include "DHTesp.h"
#include <Arduino.h>
#include "ThingSpeak.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

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
const unsigned long PERIOD_TIME_MS = 60000L;

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
      Serial.print("failed, rc=");
      // Print reason the connection failed.
      // See https://pubsubclient.knolleary.net/api.html#state for the failure code explanation.
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
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

void setup()
{
  Serial.begin(115200);
  Serial.println();
  String thisBoard = ARDUINO_BOARD;
  Serial.println(thisBoard);

  // Wait for DHT22 to power up.
  delay(2000);

  dht.setup(D1, DHTesp::DHT22);

  WiFi.mode(WiFiMode_t::WIFI_STA);
  delay(2000);
}

void waitWhileConnecting()
{
  do
  {
    Serial.print(".");
    delay(100);
  } while (wifi_station_get_connect_status() == STATION_CONNECTING);
}

void loop()
{
  while (true)
  {
    // Wait for next update cycle.
    auto timeDifference = millis() - lastCycleTime;
    if (timeDifference < PERIOD_TIME_MS)
    {
      delay(PERIOD_TIME_MS / 20);
      continue;
    }

    WiFi.forceSleepWake();
    waitWhileConnecting();

    auto wifiStatus = WiFi.status();
    if (wifiStatus != WL_CONNECTED)
    {
      for (int retry = 0; retry < 5; retry++)
      {
        Serial.print(" . ");
        WiFi.mode(WiFiMode_t::WIFI_STA);
        WiFi.begin();
        waitWhileConnecting();
        wifiStatus = WiFi.status();
        if (wifiStatus == WL_CONNECTED)
        {
          break;
        }
        delay(500);
      }
      if (wifiStatus != WL_CONNECTED)
      {
        Serial.printf("Failed to connect to wifi: %d\n", (int)wifiStatus);
        delay(2000);
        continue;
      }
    }

    lastCycleTime = millis();
    auto dhtReading = dht.getTempAndHumidity();
    auto dhtStatus = dht.getStatus();

    if (dhtStatus != DHTesp::DHT_ERROR_t::ERROR_NONE)
    {
      Serial.println(dht.getStatusString());
      //Try again however DHT22 needs 2 seconds between reads
      delay(2000);
      continue;
    }

    const auto temperatureF = dht.toFahrenheit(dhtReading.temperature);
    const auto humidity = dhtReading.humidity;
    const auto heatIndex = dht.computeHeatIndex(temperatureF, humidity, true);

    if (!mqttClient.connected())
    {
      if (!reconnect())
      {
        //Unable to reconnect to mqtt server. Retry from begining.
        Serial.print("Unable to reconnect to mqtt server.");
        delay(2000);
        continue;
      }
    }
    if (!publishReadings(temperatureF, humidity, heatIndex))
    {
      Serial.print("Unable to publish readings.");
    }
    else
    {
      Serial.printf("Temperature(F): %.1f\tHumidity: %.1f\tHeat Index: %.1f\n",
                    temperatureF, humidity, heatIndex);
    }

    if (!WiFi.forceSleepBegin())
    {
      Serial.println("Force sleep failed");
    }
  }
}