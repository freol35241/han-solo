#include "config.h"

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ReactESP.h>
#include <ArduinoJson.h>

#include "HAN.h"

reactesp::ReactESP app;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void connectToWifi()
{
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PW);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println("Connected to MQTT.");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void setup()
{

  // Attach wifi handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // Configure mqtt client
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PW);

  // Configure Serial
  Serial.begin(115200);
  U0C0 |= BIT(UCRXI);          // Inverse RX
  Serial.setRxBufferSize(512); // Make sure buffer is large enough for us to have time for the processing

  Serial.println("Setting up serial callback");
  app.onAvailable(Serial, []()
                  {
                    Serial.println("Data available on Serial");

                    HAN::Telegram telegram = HAN::receive_telegram(Serial);

                    // Serialize
                    DynamicJsonDocument doc(4096);
                    doc["FLAG_ID"] = telegram.FLAG_ID;
                    doc["id"] = telegram.id;
                    doc["timestamp"] = telegram.timestamp;
                    doc["crc"] = telegram.crc;

                    for (auto& [name, payload]: telegram.payloads){
                      doc[name]["value"] = payload.value;
                      doc[name]["unit"] = payload.unit;
                    }

                    String output;
                    serializeJson(doc, output);
                    Serial.println("Serialized telegram:");
                    Serial.println(output);

                    // Publish to mqtt
                    if (mqttClient.connected())
                    {
                      Serial.println("Publishing to MQTT broker");

                      mqttClient.publish(String(MQTT_BASE_TOPIC).c_str(), 0, false, output.c_str());
                    }
                    else
                    {
                      Serial.println("MQTT not connected, skipping publishing...");
                    } }

  );

  // Connect to wifi (and subsequently mqtt)
  connectToWifi();
}

void loop()
{
  app.tick();
}