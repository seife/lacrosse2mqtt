# LaCrosse to MQTT gateway

This is a gateway to receive temperature and humidity from LaCrosse IT+ temperature sensors and publish them to a MQTT broker.

The code was originally inspired by [LaCrosseITPlusReader](https://github.com/rinie/LaCrosseITPlusReader) but the original code has been completely reworked since then.
It is designed to run on a "TTGO LORA" board which has a SX1276 RF chip and a SSD1306 OLED on board.

The on-board button triggers WPS connection to the WIFI network, it should be only needed on first installation.

The web page is showing the received sensors with their values, the configuration page allows to specify a name / label for every sensor ID.
The sensor ID is a 6 bit value (0-63). Because there are two different data rates for LaCrosse sensors, which can otherwise have the same ID, I decided to add 128 to the sensor ID if it comes from a sensor with the slow data rate. There are also two-channel temperature sensors which identify the second temperature channel with a "magic" humidity value. To distinguish the two channels, 64 is added to the sensor ID for the second channel. This gives a total of 256 sensor IDs.
To clear a label for a sensor, just enter an empty label.

## MQTT publishing of values
On the config page, you can enter the hostname / IP of your MQTT broker. The topics published are:

   * `climate/<LABEL>/temp` temperate
   * `climate/<LABEL>/humi` humidity (if available)
   * `lacrosse/id_<ID>/temp`, `lacrosse/id_<ID>/humi` the same but per ID. Note that the ID may change after a battery change! Labels can be rearranged after a battery change for stable naming.
   * `lacrosse/id_<ID>/state` additional flags "low_batt", "init" (for new battery state), "RSSI" (signal), "baud" (data rate) as JSON string

## Firmware update
The software update can be uploaded via the "Update software" link from the configuration page

## Debugging
More information about the current state is printed to the serial console, configured at 115200 baud.
You can also define `DEBUG_DAVFS` in the code, then WebDAV access to the LITTLEFS used for storing the configuration is possible on port 81.

## Dependencies / credits
The following libraries are needed for building (installed via arduino lib manager if no github url is given):

   * LittleFS_esp32
   * PubSubClient
   * ESP8266 and ESP32 OLED driver for SSD1306 displays
   * ArduinoJson
   * ESP Async WebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
   * AsyncTCP (https://github.com/me-no-dev/AsyncTCP)
   * AsyncElegantOTA (https://github.com/ayushsharma82/AsyncElegantOTA)
   * ESPWebDav (for debugging only)

