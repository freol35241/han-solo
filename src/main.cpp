#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ReactESP.h>
#include <DebugLog.h>

#include "config.h"

reactesp::ReactESP app;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void connectToWifi()
{
  LOG_INFO("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PW);
}

void connectToMqtt()
{
  LOG_INFO("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  LOG_INFO("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  LOG_INFO("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool sessionPresent)
{
  LOG_INFO("Connected to MQTT.");
  LOG_DEBUG("Session present: ");
  LOG_DEBUG(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  LOG_INFO("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId)
{
  LOG_DEBUG("Publish acknowledged.");
  LOG_DEBUG("  packetId: ");
  LOG_DEBUG(packetId);
}

void setup()
{
  // Serial output
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // Attach wifi handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // Configure mqtt client
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PW);

  // Connect to wifi (and subsequently mqtt)
  connectToWifi();

  // Pin config
  pinMode(INTERRUPT_PIN_NUMBER, INPUT_PULLUP);

  // Attach pin interrupt
  app.onInterrupt(INTERRUPT_PIN_NUMBER, FALLING, []()
                  {
                    static unsigned long _previous = 0L;
                    static unsigned long _counter = 0L;

                    auto now = millis();

                    auto dt = now - _previous;

                    if (_previous == 0L)
                    {
                      LOG_DEBUG("First falling edge detected, next one will start generating data.");
                      _previous = now;
                      return;
                    }
                    _previous = now;

                    if (dt < DEBOUNCE_TIME)
                    {
                      LOG_DEBUG("Detection debounced...");
                      return;
                    }

                    _counter++;
                    LOG_INFO("Falling edge (", _counter, ") detected!");

                    float power = (1000 / BLINKS_PER_KWH) / (float)((dt / 1000) / 3600);
                    LOG_INFO("Power: ", power, " W");


                    // Publish power
                    mqttClient.publish((MQTT_BASE_TOPIC + String("/power")).c_str(), 0, false, String(power).c_str());

                    // Publish incrementer
                    mqttClient.publish((MQTT_BASE_TOPIC + String("/counter")).c_str(), 0, false, String(_counter).c_str()); });
}

void loop()
{
  app.tick();
}