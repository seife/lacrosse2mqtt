#ifndef _WEBFRONTEND_H
#define _WEBFRONTEND_H
#include <ESPAsyncWebServer.h>

void setup_web();
void handle_index(AsyncWebServerRequest *request);
void handle_config(AsyncWebServerRequest *request);
#endif
