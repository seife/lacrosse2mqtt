/*
 * WiFi helper functions for:
 *    WPS
 *    check connection state
 */

#include "wifi_functions.h"

#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"

static esp_wps_config_t wps_config;
int wifi_state = STATE_DISC;
const char *_wifi_state_str[] = {
    "disc",
    "WPS",
    "conn",
    "fail"
};

void WiFiEvent(WiFiEvent_t event)
{
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("Station Mode Started");
            wifi_state = STATE_DISC;
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
            Serial.println("Station Mode Stopped");
            wifi_state = STATE_DISC;
            break;
        case ARDUINO_EVENT_WIFI_READY:
            Serial.println("WiFi is ready.");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("Connected to: %s (%s), Got IP: ",WiFi.SSID().c_str(),WiFi.BSSIDstr().c_str());
            Serial.println(WiFi.localIP());
            wifi_state = STATE_CONN;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (wifi_state == STATE_WPS) {
                Serial.println("WIFI_STA_DISCONNECTED while STATE_WPS");
                /* do not reconnect(), but just keep going */
                break;
            }
            Serial.println("Disconnected from station, attempting reconnection");
            wifi_state = STATE_DISC;
            WiFi.reconnect();
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("WIFI_STA_CONNECTED, waiting for GOT_IP");
            break;
        case ARDUINO_EVENT_WPS_ER_SUCCESS:
            Serial.printf("WPS Successful, stopping WPS and connecting to: %s\r\n", WiFi.SSID().c_str());
            esp_wifi_wps_disable();
            WiFi.disconnect(); /* this seems to make this more reliable (and quick) */
            wifi_state = STATE_DISC;
            delay(100);
            WiFi.begin();
            break;
        case ARDUINO_EVENT_WPS_ER_FAILED:
            Serial.println("WPS Failed, retrying normal connect");
            esp_wifi_wps_disable();
            // wifi_state = STATE_DISC;
            // delay(10);
            // WiFi.begin();
            /* not sure why? but has been in use like this and seems to work */
            start_WPS();
            break;
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:
            Serial.println("WPS Timedout, trying normal connect...");
            wifi_state = STATE_DISC;
            esp_wifi_wps_disable();
            wifi_state = STATE_DISC;
            WiFi.disconnect();
            delay(10);
            WiFi.begin();
            break;
        default:
            Serial.print("WPS/WIFI UNKNOWN EVENT: ");
            Serial.println(event);
            break;
    }
}

void start_WPS()
{
    esp_err_t err;
    Serial.println("Starting WPS");
    wifi_state = STATE_WPS;
    WiFi.mode(WIFI_MODE_STA);
    wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    err = esp_wifi_wps_enable(&wps_config);
    if (err != ESP_OK) {
        Serial.printf("WPS Enable Failed: 0x%x: %s\n", err, esp_err_to_name(err));
    }
    err = esp_wifi_wps_start(0);
    if (err != ESP_OK) {
        Serial.printf("WPS Start Failed: 0x%x: %s\n", err, esp_err_to_name(err));
    }
#if 0
    while (wifi_state == STATE_WPS) {
        delay(500);
        Serial.println(".");
    }
#endif
    Serial.println("end start_WPS()");
}

void start_WiFi(const char *hostname)
{
    if (hostname)
        WiFi.setHostname(hostname);
    WiFi.onEvent(WiFiEvent);
    WiFi.begin();
}


void WiFiStatusCheck()
{
    static wl_status_t last = WL_NO_SHIELD;
    wl_status_t now = WiFi.status();
    if (now == last)
        return;
    Serial.printf("WiFi status changed from: %d to: %d\r\n", last, now);
    if (now == WL_CONNECTED)
        wifi_state = STATE_CONN;
    else
        wifi_state = STATE_DISC;
    last = now;
}
