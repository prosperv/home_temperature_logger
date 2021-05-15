#include "DHTesp.h"
#include <Arduino.h>
#include "ThingSpeak.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

// #define DEBUG
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

auto startTime = millis();

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

TempAndHumidity dhtReading;
DHTesp::DHT_ERROR_t dhtStatus;
bool gotDhtReading = false;

#define SECONDS_TO_MILLISECONDS(x) x * 1000L
#define SECONDS_TO_MICROSECONDS(x) x * 1000000L
#define PERIOD_TIME_SECONDS 60
const unsigned long PERIOD_TIME_MS = SECONDS_TO_MILLISECONDS(PERIOD_TIME_SECONDS);
const unsigned long PERIOD_TIME_US = SECONDS_TO_MICROSECONDS(PERIOD_TIME_SECONDS);

// Testing average current consumption set period time to 10 seconds
// Record with the lower range of average from dmm
// Best average so far. 23.3 mA with debug on, 
// Best execution time 4230 ms.
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

void setup()
{
  SERIAL_BEGIN(115200);
  PRINTLN();
  String thisBoard = ARDUINO_BOARD;
  PRINTLN(thisBoard);

  dht.setup(D1, DHTesp::DHT22);
  WiFi.persistent(false); // don't store the connection each time to save wear on the flash
  WiFi.mode(WiFiMode_t::WIFI_STA);
  WiFi.begin(ssid, pass);

  delay(2300); 
}

void loop()
{
  while (true)
  {
    if (!gotDhtReading)
    {
      dhtReading = dht.getTempAndHumidity();
      dhtStatus = dht.getStatus();
    }

    const auto temperatureF = dht.toFahrenheit(dhtReading.temperature);
    const auto humidity = dhtReading.humidity;
    const auto heatIndex = dht.computeHeatIndex(temperatureF, humidity, true);

    if (dhtStatus != DHTesp::DHT_ERROR_t::ERROR_NONE)
    {
      PRINTF("Error read DHT22: %s \n\r",dht.getStatusString());
      //Try again however DHT22 needs 2 seconds between reads
      delay(2300);
      continue;
    }
    else
    {
      gotDhtReading = true;
      PRINTLN("DHT reading OK");
      PRINTF("Temperature(F): %.1f\tHumidity: %.1f\tHeat Index: %.1f\n",
             temperatureF, humidity, heatIndex);
    }

    auto wifiStatus = WiFi.status();
    for (int retry = 0; retry < 5; retry++)
    {
      PRINT("-");
      if (wifiStatus == WL_CONNECTED)
      {
        PRINTLN("Wifi connected");
        break;
      }
      wifiStatus = static_cast<wl_status_t>(WiFi.waitForConnectResult(5000));
    }
    if (wifiStatus != WL_CONNECTED)
    {
      PRINTF("Failed to connect to wifi: %d\n", (int)wifiStatus);
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
    PRINTF("Execution time: %d ms \n", millis() - startTime);
    ESP.deepSleep(PERIOD_TIME_US, WAKE_RF_DEFAULT); // Avg 26.4 mA
    // ESP.deepSleepInstant(PERIOD_TIME_US, WAKE_RF_DEFAULT); // Avg 27.2 mA
    
    // WAKE_RF_DISABLED has issue with turning RF back on
    // ESP.deepSleep(PERIOD_TIME_US, WAKE_RF_DISABLED);  // Avg  mA
    // ESP.deepSleepInstant(PERIOD_TIME_US, WAKE_RF_DISABLED);
    PRINTLN("Force sleep failed");
  }
}