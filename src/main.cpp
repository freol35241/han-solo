#include "config.h"

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <PubSubClient.h>
#include <ReactESP.h>
#include <ArduinoJson.h>
#include <DebugLog.h>

#include "HAN.h"

// MQTT robustness settings. Each can be overridden in config.h.
//
// The largest telegram is the payload limit. A full one is ~1,580 bytes as
// published today; 26 OBIS fields with long Swedish names and their units
// stay well under 3 kB.
#ifndef MQTT_PAYLOAD_BUFFER_SIZE
#define MQTT_PAYLOAD_BUFFER_SIZE 3072
#endif
#ifndef MQTT_KEEPALIVE_S
#define MQTT_KEEPALIVE_S 15 // AsyncMqttClient's default, kept
#endif
#ifndef MQTT_RECONNECT_INTERVAL
#define MQTT_RECONNECT_INTERVAL 5000 // milliseconds between connection attempts
#endif
// Restart if Wi-Fi or MQTT has been down this long. Checked from loop() with
// millis(), so it does not depend on the ReactESP scheduler.
#ifndef CONNECTIVITY_WATCHDOG_TIMEOUT
#define CONNECTIVITY_WATCHDOG_TIMEOUT (5UL * 60 * 1000) // milliseconds
#endif
// The daily restart, now checked from loop() as well.
#ifndef DAILY_RESTART_INTERVAL
#define DAILY_RESTART_INTERVAL (24UL * 3600 * 1000) // milliseconds
#endif

reactesp::ReactESP app;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

char client_id[24];
uint32_t last_mqtt_attempt = 0;
uint32_t last_connected = 0;

// The serialized telegram. Static, so a telegram is not a fresh heap String
// every ~10 s.
char telegram_json[MQTT_PAYLOAD_BUFFER_SIZE];

void connectToWifi()
{
  LOG_INFO("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PW);
}

// Called from loop(), never from a callback.
void connectToMqtt()
{
  LOG_INFO("Connecting to MQTT...");
  if (mqttClient.connect(client_id, MQTT_USER, MQTT_PW))
  {
    LOG_INFO("Connected to MQTT.");
  }
  else
  {
    LOG_ERROR("MQTT connection failed, state: ", mqttClient.state());
  }
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  LOG_INFO("Connected to Wi-Fi.");
  last_mqtt_attempt = 0; // connect to MQTT at the next loop() rather than waiting
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  LOG_INFO("Disconnected from Wi-Fi.");
  wifiReconnectTimer.once(2, connectToWifi);
}

// Publish one telegram as one whole PUBLISH packet, or nothing (#11).
//
// AsyncMqttClient wrote a packet into the TCP send buffer as far as it fit,
// about 1 kB, and lost the rest; the next packet then landed in the middle of
// the last. On the ESP8266, WiFiClient::write blocks until every byte is
// accepted or it times out, so the packet goes out whole. If a write still
// falls short, the connection is dropped instead of writing the next packet
// onto a desynchronised stream: the broker discards a partial packet when the
// connection closes.
void publish_telegram(const char *topic, const char *payload, size_t length)
{
  if (!mqttClient.beginPublish(topic, length, false))
  {
    LOG_ERROR("Could not start the publish, dropping the connection");
    mqttClient.disconnect();
    return;
  }
  size_t written = mqttClient.write(reinterpret_cast<const uint8_t *>(payload), length);
  if (written != length || !mqttClient.endPublish())
  {
    LOG_ERROR("Telegram written partially, dropping the connection to resync");
    mqttClient.disconnect();
  }
}

void setup()
{

  // Attach wifi handlers
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // Configure mqtt client
  // The id AsyncMqttClient generated, so the broker sees the same client.
  snprintf(client_id, sizeof(client_id), "esp8266-%06x", ESP.getChipId());
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_S);

  // Configure Serial
  Serial.begin(115200);
  U0C0 |= BIT(UCRXI);          // Inverse RX
  Serial.setRxBufferSize(512); // Make sure buffer is large enough for us to have time for the processing

  LOG_INFO("Setting up serial callback");
  app.onAvailable(Serial, []()
                  {
                    LOG_INFO("Data available on Serial");

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

                    size_t length = measureJson(doc);
                    if (length >= sizeof(telegram_json))
                    {
                      LOG_ERROR("Telegram larger than MQTT_PAYLOAD_BUFFER_SIZE, skipping: ", length);
                      return;
                    }
                    serializeJson(doc, telegram_json, sizeof(telegram_json));
                    LOG_DEBUG("Serialized telegram:", telegram_json);

                    // Publish to mqtt
                    if (mqttClient.connected())
                    {
                      LOG_INFO("Publishing to MQTT broker");

                      publish_telegram(MQTT_BASE_TOPIC, telegram_json, length);
                    }
                    else
                    {
                      LOG_WARN("MQTT not connected, skipping publishing...");
                    } }

  );

  // Connect to wifi; loop() connects to the broker once Wi-Fi is up
  connectToWifi();
}

// The MQTT connection and both restarts live here rather than in ReactESP
// callbacks, so they need nothing but loop() running and millis(). The daily
// restart used to be a ReactESP onDelay.
void maintain_connectivity()
{
  uint32_t now = millis();

  if (WiFi.isConnected())
  {
    if (mqttClient.connected())
    {
      mqttClient.loop();
    }
    else if (last_mqtt_attempt == 0 || now - last_mqtt_attempt >= MQTT_RECONNECT_INTERVAL)
    {
      last_mqtt_attempt = now;
      connectToMqtt();
    }
  }

  if (WiFi.isConnected() && mqttClient.connected())
  {
    last_connected = now;
  }
  else if (now - last_connected >= CONNECTIVITY_WATCHDOG_TIMEOUT)
  {
    LOG_ERROR("Wi-Fi or MQTT down beyond the watchdog timeout, restarting");
    ESP.restart();
  }

  // Forcing restart of microprocessor every day
  if (now >= DAILY_RESTART_INTERVAL)
  {
    ESP.restart();
  }
}

void loop()
{
  maintain_connectivity();
  app.tick();
}
