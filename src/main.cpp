#include "DHTesp.h"
#include <Arduino.h>
#include "ThingSpeak.h"
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include "secrets.h"

DHTesp dht;

char ssid[] = SECRET_SSID; //  your network SSID (name)
char pass[] = SECRET_PASS; // your network password
int keyIndex = 0;          // your network key Index number (needed only for WEP)
WiFiClient client;

unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  String thisBoard = ARDUINO_BOARD;
  Serial.println(thisBoard);

  // Wait for DHT22 to power up.
  delay(2000);
  dht.setup(D4, DHTesp::DHT22);

  ThingSpeak.begin(client); //Initialize ThingSpeak
  delay(2000);
}

void loop()
{
  while (true)
  {

    TempAndHumidity dhtReading = dht.getTempAndHumidity();
    DHTesp::DHT_ERROR_t dhtStatus = dht.getStatus();

    if (dhtStatus != DHTesp::DHT_ERROR_t::ERROR_NONE)
    {
      Serial.println(dht.getStatusString());
      //Try again however DHT22 needs 2 seconds between reads
      delay(2000);
      continue;
    }

    const float temperatureF = dht.toFahrenheit(dhtReading.temperature);
    const float humidity = dhtReading.humidity;
    const float heatIndex = dht.computeHeatIndex(temperatureF, humidity, true);

    Serial.printf("Temperature(F): %f.1\tHumidity: %f.1\tHeat Index: %f.1\n",
                  temperatureF, humidity, heatIndex);
    // Connect or reconnect to WiFi
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(SECRET_SSID);
      WiFi.begin(ssid, pass); // Connect to WPA/WPA2 network. Change this line if using open or WEP network
      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("Failed to connect to wifi.");
        delay(4000);
        continue;
      }
      Serial.println("\nConnected.");
    }

    // Write to ThingSpeak. There are up to 8 fields in a channel, allowing you to store up to 8 different
    // pieces of information in a channel.  Here, we write to field 1.
    int x = ThingSpeak.writeField(myChannelNumber, 1, temperatureF, myWriteAPIKey);
    if (x == 200)
    {
      Serial.println("Channel update successful.");
    }
    else
    {
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }
    // Wait for next update cycle.
    delay(30000);
  }
}