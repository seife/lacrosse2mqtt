#ifndef _WIFI_FUNCTIONS_H
#define _WIFI_FUNCTIONS_H

#include "WiFi.h"
#include "esp_wps.h"

enum {
  STATE_DISC = 0,
  STATE_WPS,
  STATE_CONN,
  STATE_FAIL
};

extern const char *_wifi_state_str[];
extern int wifi_state;
void WiFiEvent(WiFiEvent_t event, system_event_info_t info);
void start_WPS();
void WiFiStatusCheck();

#endif
