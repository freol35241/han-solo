
This directory is intended for project header files.

More specifically, this project requires a header file named `config.h` to be placed in this directory with the following contents:

```cpp
#ifndef CONFIG_H
#define CONFIG_H

#define WIFI_SSID "YOUR WIFI SSID"
#define WIFI_PW "YOUR WIFI PASSWORD"

#define MQTT_HOST IPAddress(XXX, XXX, XXX, XXX)
#define MQTT_PORT XXXX
#define MQTT_USER "YOUR MQTT USERNAME"
#define MQTT_PW "YOUR MQTT PASSWORD"

#define MQTT_BASE_TOPIC "BASE/TOPIC/FOR/PUBLISHING"

#endif
```
