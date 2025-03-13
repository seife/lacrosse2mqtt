/*
 * lacrosse2mqtt
 * Bridge LaCrosse IT+ sensors to MQTT
 * (C) 2021 Stefan Seyfried
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, got to https://www.gnu.org/licenses/
 */

#include <LittleFS.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <SSD1306Wire.h>
#include "wifi_functions.h"
#include "webfrontend.h"
#include "globals.h"

#include "lacrosse.h"
#include "SX127x.h"

//#define DEBUG_DAVFS

#ifdef DEBUG_DAVFS
#include <ESPWebDAV.h>
#endif

#define FORMAT_LITTLEFS_IF_FAILED false

/* if display is default to off, keep it on for this many seconds after power on
 * or a wifi change event */
#define DISPLAY_TIMEOUT 300

#ifdef DEBUG_DAVFS
WiFiServer tcp(81);
ESPWebDAV dav;
#endif

bool DEBUG = 0;
const int interval = 20;   /* toggle interval in seconds */
const int freq = 868290;   /* frequency in kHz, 868300 did not receive all sensors... */

unsigned long last_reconnect;
unsigned long last_switch = 0;
// unsigned long last_display = 0;
bool littlefs_ok;
bool mqtt_ok;
bool display_on = true;
uint32_t auto_display_on = 0;

Config config;
Cache fcache[SENSOR_NUM]; /* 128 IDs x 2 datarates */
String id2name[SENSOR_NUM];

/* TTGO board OLED pins to ESP32 GPIOs */
/*
  #define OLED_SDA 4
  #define OLED_SCL 15
  #define OLED_RST 16
  ==> already defined in board header!
 */
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

/* TTGO board SX127x ports
   GPIO5   SX1278s SCK
   GPIO19  SX1278s MISO
   GPIO27  SX1278s MOSI
   GPIO18  SX1278s CS
   GPIO14  SX1278s RESET
   GPIO26  SX1278s IRQ(Interrupt Request)
  #define SS 18
  #define RST 14
  #define DI0 26 // interrupt mode did not work well
  ==> already defined in board header, also MOSI, MISO,...!
 */
SX127x SX(LORA_CS, LORA_RST);

#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"

WiFiClient client;
PubSubClient mqtt_client(client);
const char *mqtt_id = "lacrosse2mqtt.esp";
const String pretty_base = "climate/";
const String pub_base = "lacrosse/id_";
bool mqtt_server_set = false;

void check_repeatedjobs()
{
    /* Toggle the data rate fast/slow */
    unsigned long now = millis();
    if (now - last_switch > interval * 1000) {
        SX.NextDataRate();
        last_switch = now;
    }
    if (config.changed) {
        Serial.println("MQTT config changed. Dis- and reconnecting...");
        config.changed = false;
        if (mqtt_ok)
            mqtt_client.disconnect();
        if (config.mqtt_server.length() > 0) {
            const char *_server = config.mqtt_server.c_str();
            mqtt_client.setServer(_server, config.mqtt_port);
            mqtt_server_set = true; /* to avoid trying connection with invalid settings */
        } else
            Serial.println("MQTT server name not configured");
        mqtt_client.setKeepAlive(60); /* same as python's paho.mqtt.client */
        Serial.print("MQTT SERVER: "); Serial.println(config.mqtt_server);
        Serial.print("MQTT PORT:   "); Serial.println(config.mqtt_port);
        last_reconnect = 0; /* trigger connect() */
    }
    if (!mqtt_client.connected() && now - last_reconnect > 5 * 1000) {
        if (mqtt_server_set) {
            const char *user = NULL;
            const char *pass = NULL;
            if (config.mqtt_user.length()) {
                user = config.mqtt_user.c_str();
                pass = config.mqtt_pass.c_str();
            }
            Serial.print("MQTT RECONNECT...");
            if (mqtt_client.connect(mqtt_id, user, pass))
                Serial.println("OK!");
            else
                Serial.println("FAILED");
        }
        last_reconnect = now;
    }
#if 0
    if (now - last_display > 10000) /* update display at least every 10 seconds, even if nothing */
        update_display(NULL);       /* is received. Indicates that the thing is still alive ;-) */
#endif
    mqtt_ok = mqtt_client.connected();
}

void expire_cache()
{
    /* clear all entries older than 300 seconds... */
    unsigned long now = millis();
    for (int i = 0; i < (sizeof(fcache) / sizeof(struct Cache)); i++) {
        if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) > 300000) {
            memset(&fcache[i], 0, sizeof(struct Cache));
            Serial.print("expired ID ");
            Serial.println(i);
        }
    }
}

String wifi_disp;
void update_display(LaCrosse::Frame *frame)
{
    char tmp[32];
    // last_display = millis();
    uint32_t now = uptime_sec();
    if (display_on)
        display.displayOn();
    else if (now < auto_display_on + DISPLAY_TIMEOUT) {
        // Serial.println("update_display: auto_on not yet expired " + String(now - auto_display_on));
        display.displayOn();
    } else {
        // Serial.println("auto_display_on: " + String(auto_display_on) + " now: " + String(now));
        display.displayOff();
        return;
    }
    snprintf(tmp, 31, "%dd %d:%02d:%02d", now / 86400, (now % 86400) / 3600, (now % 3600) / 60, now % 60);
    String status = "WiFi:" + wifi_disp + " up: " + String(tmp);
    bool s_invert = (now / 60) & 0x01; /* 60 seconds inverted, the next 60s not */
    display.setColor(WHITE);
    if (s_invert) {
        display.fillRect(0, 0, 128,64); /* clear() does fix set black background */
        display.setColor(BLACK);
    } else {
        display.clear();
    }

    display.drawString(0, 0, status);
    if (frame) {
        if (frame->valid) {
            if (id2name[frame->ID].length() > 0) {
                display.print(id2name[frame->ID]);
                display.printf(" %.1fC", frame->temp);
            } else {
                display.printf("id: %02d %.1fC", frame->ID, frame->temp);
            }
            if (frame->humi <= 100)
                display.printf(" %d%%", frame->humi);
        } else {
            display.print("invalid");
        }
        display.println();
    }
    display.drawLogBuffer(0, 14);
    display.display();
}

void receive()
{
    byte *payload;
    byte payLoadSize;
    int rssi, rate;
    if (!SX.Receive(payLoadSize))
        return;

    digitalWrite(LED_BUILTIN, HIGH);
    rssi = SX.GetRSSI();
    rate = SX.GetDataRate();
    payload = SX.GetPayloadPointer();

    if (DEBUG) {
        Serial.print("\nEnd receiving, HEX raw data: ");
        for (int i = 0; i < 16; i++) {
            Serial.print(payload[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }

    /* check if it can be decoded */
    LaCrosse::Frame frame;
    frame.rate = rate;
    if (LaCrosse::TryHandleData(payload, &frame)) {
        LaCrosse::Frame oldframe;
        byte ID = frame.ID;
        LaCrosse::TryHandleData(fcache[ID].data, &oldframe);
        fcache[ID].rssi = rssi;
        fcache[ID].timestamp = millis();
        memcpy(&fcache[ID].data, payload, FRAME_LENGTH);
        frame.rssi = rssi;
        LaCrosse::DisplayFrame(payload, &frame);
        String pub = pub_base + String(ID, DEC) + "/";
        mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
        if (frame.humi <= 100)
            mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
        String state = "";
        state += "{\"low_batt\": " + String(frame.batlo?"true":"false") +
                 ", \"init\": " + String(frame.init?"true":"false") +
                 ", \"RSSI\": " + String(rssi, DEC) +
                 ", \"baud\": " + String(rate / 1000.0, 3) +
                 "}";
        mqtt_client.publish((pub + "state").c_str(), state.c_str());
        if (id2name[ID].length() > 0) {
            pub = pretty_base + id2name[ID] + "/";
            if (abs(oldframe.temp - frame.temp) > 2.0)
                Serial.println(String("skipping invalid temp diff bigger than 2K: ") + String(oldframe.temp - frame.temp,1));
            else
                mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
            if (frame.humi <= 100) {
                if (abs(oldframe.humi - frame.humi) > 10)
                    Serial.println(String("skipping invalid humi diff > 10%: ") + String(oldframe.humi - frame.humi, DEC));
                else
                    mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
            }
        }

    } else {
        static unsigned long last;
        LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
        Serial.println();
    }

    update_display(&frame);
    SX.EnableReceiver(true);
    digitalWrite(LED_BUILTIN, LOW);
}

void setup(void)
{
    config.mqtt_port = 1883; /* default */
    Serial.begin(115200);
    start_WiFi("lacrosse2mqtt");
    littlefs_ok = LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
    if (!littlefs_ok)
        Serial.println("LittleFS Mount Failed");
    setup_web(); /* also loads config from LittleFS */
    display_on = config.display_on;

    pinMode(KEY_BUILTIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW); // set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(OLED_RST, HIGH);

    display.init();
    display.setContrast(16); /* it is for debug only, so dimming is ok */
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    delay(1000); /* for Serial to really work */

    Serial.println("TTGO LORA lacrosse2mqtt converter");
#if 0
    Serial.println("LaCrosse::Frame Cache fcache id2name size: ");
    Serial.println(sizeof(LaCrosse::Frame));
    Serial.println(sizeof(Cache));
    Serial.println(sizeof(fcache));
    Serial.println(sizeof(id2name));
#endif
    display.drawString(0,0,"LaCrosse2mqtt");
    display.display();
    display.setLogBuffer(4, 32);

    last_switch = millis();

    if (!SX.init()) {
        Serial.println("***** SX127x init failed! ****");
        display.drawString(0,24, "***** SX127x init failed! ****");
        display.display();
        while(true)
            delay(1000);
    }
    SX.SetupForLaCrosse();
    SX.SetFrequency(freq);
    SX.NextDataRate(0);
    SX.EnableReceiver(true);

#ifdef DEBUG_DAVFS
    tcp.begin();
    dav.begin(&tcp, &LittleFS);
    dav.setTransferStatusCallback([](const char* name, int percent, bool receive)
    {
        Serial.printf("%s: '%s': %d%%\n", receive ? "recv" : "send", name, percent);
    });
#endif
}

uint32_t check_button()
{
    static uint32_t low_at = 0;
    static bool pressed = false;
    if (digitalRead(KEY_BUILTIN) == LOW) {
        if (! pressed)
            low_at = millis();
        pressed = true;
        return 0;
    }
    if (! pressed)
        return 0;
    /* if button was pressed and now released, return how long
     * it has been down */
    pressed = false;
    return millis() - low_at;
}

static int last_state = -1;
void loop(void)
{
    handle_client();
#ifdef DEBUG_DAVFS
    dav.handleClient();
#endif
    uint32_t button_time = check_button();
    if (button_time > 0) {
        Serial.print("button_time: ");
        Serial.println(button_time);
    }
    if (wifi_state != STATE_WPS) {
        WiFiStatusCheck();
        if (button_time > 2000) {
            start_WPS();
            button_time = 0;
            auto_display_on = uptime_sec();
        }
    }
    if (button_time > 100 && button_time <= 2000) {
        display_on = ! display_on;
        /* ensure that display can be turned off while timeout is still active */
        auto_display_on = uptime_sec() - DISPLAY_TIMEOUT - 1;
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    if (last_state != wifi_state) {
        last_state = wifi_state;
        wifi_disp = String(_wifi_state_str[wifi_state]);
        auto_display_on = uptime_sec();
        update_display(NULL);
    }
}
