# HAN Solo
My own take on interfacing swedish electricity meters having a HAN (P1) port.

This code reads the serial data outputted on the P1 port and posts it as a JSON struct on a single (configurable) MQTT topic.

## Hardware
The code is developed for esp8266-based hardware and was tested on the Wemos D1 mini during development.

For information about the required connection to the electricity meter, check https://www.hanporten.se (in Swedish).

## Configuration

See [here](include/README.md)

## Building
Clone this repository and buildusing [platformIO](https://platformio.org/)
