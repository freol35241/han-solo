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
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PW);

  // Connect to wifi (and subsequently mqtt)
  connectToWifi();

  Serial.println("Setting up pin config and attaching callback");
  pinMode(INTERRUPT_PIN_NUMBER, INPUT_PULLUP);
  app.onInterrupt(INTERRUPT_PIN_NUMBER, FALLING, []()
                  {
                    static unsigned long _previous = 0L;
                    static unsigned long _counter = 0L;

                    auto now = millis();

                    if (_previous == 0L)
                    {
                      Serial.println("First falling edge detected, next one will start generating data.");
                      _previous = now;
                      return;
                    }

                    auto dt = now - _previous;
                    _previous = now;

                    Serial.print("Delta (ms): ");
                    Serial.println(dt);

                    if (dt < DEBOUNCE_TIME)
                    {
                      Serial.println("Detection debounced...");
                      return;
                    }

                    _counter++;
                    Serial.print("Falling edge (");
                    Serial.print(_counter);
                    Serial.println(") detected!");

                    float power = (1000 / BLINKS_PER_KWH) / (((float)dt / 1000) / 3600);
                    Serial.print("Power: ");
                    Serial.print(power);
                    Serial.println(" W");

                    if (mqttClient.connected())
                    {
                      Serial.println("Publishing to MQTT broker");
                      // Publish power
                      mqttClient.publish((MQTT_BASE_TOPIC + String("/power")).c_str(), 0, false, String(power).c_str());

                      // Publish incrementer
                      mqttClient.publish((MQTT_BASE_TOPIC + String("/counter")).c_str(), 0, false, String(_counter).c_str());
                    } else {
                      Serial.println("MQTT not connected, skipping publishing...");
                    } });
}

void loop()
{
}