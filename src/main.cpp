#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ReactESP.h>

#include "config.h"

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
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
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

                    float dt = now - _previous;

                    if (_previous == 0L)
                    {
                      _previous = now;
                      return;
                    }
                    _previous = now;

                    if (dt < DEBOUNCE_TIME)
                    {
                      return;
                    }

                    // We are ok, lets start calculating
                    _counter++;
                    float power = (1000 / BLINKS_PER_KWH) / ((dt / 1000) / 3600);


                    // Publish power
                    String payload = String(power);
                    mqttClient.publish((MQTT_BASE_TOPIC + "/power").c_str(), 0, false, payload.c_str());

                    // Publish incrementer
                    payload = String(_counter);
                    mqttClient.publish((MQTT_BASE_TOPIC + "/counter").c_str(), 0, false, payload.c_str()); });
}

void loop()
{
  // Nothing here, this sketch is event driven
}