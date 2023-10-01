#include "webfrontend.h"
#include "lacrosse.h"
#include "globals.h"
#include <HTTPUpdateServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <rom/rtc.h>

/* git version passed by compile.sh */
#ifndef LACROSSE2MQTT_VERSION
#define LACROSSE2MQTT_VERSION "unknown"
#endif

static WebServer server(80);
static HTTPUpdateServer httpUpdater;

int name2id(const char *fname, const int start = 0)
{
    if (strlen(fname) - start != 2) {
        Serial.printf("INVALID idmap file name: %s\r\n", fname);
        return -1;
    }
    char *end;
    errno = 0;
    int id = strtol(fname + start, &end, 16);
    if (*end != '\0' || errno != 0 || end - fname - start != 2) {
        Serial.printf("STRTOL error, %s, errno: %d\r\n", fname, errno);
        return -1;
    }
    return id;
}

String time_string(void)
{
    uint32_t now = uptime_sec();
    char timestr[10];
    String ret = "";
    if (now >= 24*60*60)
        ret += String(now / (24*60*60)) + "d ";
    now %= 24*60*60;
    snprintf(timestr, 10, "%02d:%02d:%02d", now / (60*60), (now % (60*60)) / 60, now % 60);
    ret += String(timestr);
    return ret;
}

String read_file(File &file)
{
    String ret;
    while (file.available())
        ret += String((char)file.read());
    return ret;
}

bool load_idmap()
{
    if (!littlefs_ok)
        return false;
    File idmapdir = LittleFS.open("/idmap");
    if (!idmapdir) {
        Serial.println("/idmap not found");
        return false;
    }
    if (!idmapdir.isDirectory()) {
        Serial.println("/idmap not a directory");
        idmapdir.close();
        return false;
    }
    for (int i = 0; i < SENSOR_NUM; i++)
        id2name[i] = String();
    int found = 0;
    File file = idmapdir.openNextFile();
    while (file) {
        const char *fname = file.name();
        int id = name2id(fname);
        if (id > -1) {
            Serial.printf("reading idmap file %s id:%2d ", fname, id);
            id2name[id] = read_file(file);
            Serial.println("content: " + id2name[id]);
            found++;
        }
        file.close();
        file = idmapdir.openNextFile();
    }
    idmapdir.close();
    return (found > 0);
}

bool load_config()
{
    if (!littlefs_ok)
        return false;
    File cfg = LittleFS.open("/config.json");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, cfg);
    if (error) {
        Serial.println("Failed to read /config.json");
        Serial.print("error code: ");
        Serial.println(error.code());
    } else {
        if (doc["mqtt_port"])
            config.mqtt_port = doc["mqtt_port"];
        if (doc["mqtt_server"]) {
            const char *tmp = doc["mqtt_server"];
            config.mqtt_server = String(tmp);
        }
        Serial.println("result of config.json: "
                       "mqtt_server '" + config.mqtt_server + "' "
                       "mqtt_port: " + String(config.mqtt_port));
        config.changed = true;
    }
    cfg.close();
    return !error;
}

bool save_config()
{
    bool ret = true;
    LittleFS.remove("/config.json");
    File cfg = LittleFS.open("/config.json", FILE_WRITE);
    if (! cfg) {
        Serial.println("Failed to open /config.json for writing");
        return false;
    }
    StaticJsonDocument<256> doc;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_server"] = config.mqtt_server;
    if (serializeJson(doc, cfg) == 0) {
        Serial.println(F("Failed to write /config.json"));
        ret = false;
    }
    cfg.close();
    Serial.println("---written config.json:");
    cfg = LittleFS.open("/config.json");
    Serial.println(read_file(cfg));
    cfg.close();
    Serial.println("---end config.json:");
    return ret;
}

bool save_idmap()
{
    if (!littlefs_ok)
        return false;
    File idmapdir = LittleFS.open("/idmap");
    if (!idmapdir) {
        Serial.println("SAVE: /idmapdir not found");
        if (LittleFS.mkdir("/idmap"))
            idmapdir = LittleFS.open("/idmap");
        else {
            Serial.println("SAVE: mkdir /idmap failed :-(");
            return false;
        }
    }
    if (!idmapdir.isDirectory()) {
        Serial.println("SAVE: /idmap not a directory");
        idmapdir.close();
        return false;
    }
    File file = idmapdir.openNextFile();
    while (file) {
        int id = name2id(file.name());
        String fullname = "/idmap/" + String(file.name());
        file.close();
        if (id > -1 && id2name[id].length() == 0) {
            Serial.print("removing ");
            Serial.println(fullname);
            if (!LittleFS.remove(fullname))
                Serial.println("failed?");
        }
        file = idmapdir.openNextFile();
    }
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (id2name[i].length() == 0)
            continue;
        String fullname = String("/idmap/") + String((i < 0x10)?"0":"") + String(i, HEX);
        if (LittleFS.exists(fullname)) {
            //Serial.println("Exists: " + fullname);
            File comp = LittleFS.open(fullname);
            if (comp) {
                //Serial.println("open: " + fullname);
                String tmp = read_file(comp);
                comp.close();
                //Serial.print("tmp:");Serial.print(tmp);Serial.println("'");
                //Serial.print("id2:");Serial.print(id2name[i]);Serial.println("'");
                if (tmp == id2name[i])
                    continue; /* skip unchanged settings */
            }
        }
        Serial.println("Writing file " +fullname+" content: " + id2name[i]);
        File file = LittleFS.open(fullname, FILE_WRITE);
        if (! file) {
            Serial.println("file open failed :-(");
            continue;
        }
        file.print(id2name[i]);
    }
    return true;
}

void add_current_table(String &s, bool rawdata)
{
    unsigned long now = millis();
    String h;
    s += "<table><tr><th>ID</th><th>Temperature</th><th>Humidity</th><th>RSSI</th><th>Name</th><th>Age (ms)</th><th>Battery</th><th>New?</th>";
    if (rawdata)
        s += "<th>Raw Frame Data</th>";
    s += "</tr>\n";
    for (int i = 0; i < SENSOR_NUM; i++) {
        LaCrosse::Frame f;
        bool stale = false;
        String name = id2name[i];
        if (fcache[i].timestamp == 0) {
            if (name.length() > 0)
                stale = true;
            else
                continue;
        }
        if (stale) {
            s +=  "<tr><td>" + String(i) +
                 "</td><td>-</td><td>-</td><td>-</td><td>" + name +
                 "</td><td>-</td><td>-</td><td>-</td>";
            if (rawdata)
                s += "<td>-</td>";
            s += "</tr>\n";
            continue;
        }
        if (i & 0x80)
            f.rate = 9579;
        else
            f.rate = 17241;
        if (! LaCrosse::TryHandleData(fcache[i].data, &f))
            continue;
        if (f.humi <= 100)
            h = String(f.humi) + "%";
        else
            h = "-";
        s +=  "<tr><td>" + String(i) +
             "</td><td>" + String(f.temp, 1) +
             "</td><td>" + h +
             "</td><td>" + String(fcache[i].rssi) +
             "</td><td>" + name +
             "</td><td>" + String(now - fcache[i].timestamp) +
             "</td><td>" + String(f.batlo ? "LOW!" : "OK") +
             "</td><td>" + String(f.init ? "YES!" : "no") +
             "</td>";
        if (rawdata) {
            s += "<td>0x";
            for (int j = 0; j < FRAME_LENGTH; j++) {
                char tmp[3];
                snprintf(tmp, 3, "%02X", fcache[i].data[j]);
                s += String(tmp);
            }
            s += "</td>";
        }
        s += "</tr>\n";
    }
    s += "</table>\n";
}

void add_header(String &s, String title)
{
    s += "<!DOCTYPE HTML><html lang=\"en\"><head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<meta name=\"description\" content=\"lacrosse sensors to mqtt converter\">\n"
        "<title>" + title + "</title>\n"
        "<style>\n"
        "td, th {\n"
        " text-align: right;\n"
        "}\n"
        "table td:nth-child(9) {\n"
        " font-family: monospace;\n"
        " font-size: 10pt;\n"
        "}\n"
        "</style>\n"
        "</head>\n<body>\n"
        "<H1>" + title + "</H1>\n";
}

/* from tasmota */
String ESP32GetResetReason(uint32_t cpu_no) {
    // tools\sdk\include\esp32\rom\rtc.h
    switch (rtc_get_reset_reason(cpu_no)) {
        case POWERON_RESET:         return F("Vbat power on reset");
        case SW_RESET:              return F("Software reset digital core");
        case OWDT_RESET:            return F("Legacy watch dog reset digital core");
        case DEEPSLEEP_RESET:       return F("Deep Sleep reset digital core");
        case SDIO_RESET:            return F("Reset by SLC module, reset digital core");
        case TG0WDT_SYS_RESET:      return F("Timer Group0 Watch dog reset digital core");
        case TG1WDT_SYS_RESET:      return F("Timer Group1 Watch dog reset digital core");
        case RTCWDT_SYS_RESET:      return F("RTC Watch dog Reset digital core");
        case INTRUSION_RESET:       return F("Instrusion tested to reset CPU");
        case TGWDT_CPU_RESET:       return F("Time Group reset CPU");
        case SW_CPU_RESET:          return F("Software reset CPU");
        case RTCWDT_CPU_RESET:      return F("RTC Watch dog Reset CPU");
        case EXT_CPU_RESET:         return F("for APP CPU, reseted by PRO CPU");
        case RTCWDT_BROWN_OUT_RESET:return F("Reset when the vdd voltage is not stable");
        case RTCWDT_RTC_RESET:      return F("RTC Watch dog reset digital core and rtc module");
        /* esp32-cX?
        case 17 : return F("Time Group1 reset CPU");                            // 17  -                 TG1WDT_CPU_RESET
        case 18 : return F("Super watchdog reset digital core and rtc module"); // 18  -                 SUPER_WDT_RESET
        case 19 : return F("Glitch reset digital core and rtc module");         // 19  -                 GLITCH_RTC_RESET
        case 20 : return F("Efuse reset digital core");                         // 20                    EFUSE_RESET
        case 21 : return F("Usb uart reset digital core");                      // 21                    USB_UART_CHIP_RESET
        case 22 : return F("Usb jtag reset digital core");                      // 22                    USB_JTAG_CHIP_RESET
        case 23 : return F("Power glitch reset digital core and rtc module");   // 23                    POWER_GLITCH_RESET
         */
        default: break;
    }
    return F("No meaning"); // 0 and undefined
}

void add_sysinfo_footer(String &s)
{
    s += "<p>"
        "System information: Uptime " + time_string() +
        ", Software version: " + LACROSSE2MQTT_VERSION +
        ", Built: " + __DATE__ + " " + __TIME__ +
        ", Reset reason: " + ESP32GetResetReason(0) +
        "</p>\n";
}

//void handle_index() {
void handle_index() {
    // TODO: use server.hostHeader()?
    String IP = WiFi.localIP().toString();
    String index;
    add_header(index, "LaCrosse2mqtt");
    add_current_table(index, false);
    index += "<p><a href=\"/config.html\">Configuration page</a></p>\n";
    add_sysinfo_footer(index);
    index += "</body></html>\n";
    server.send(200, "text/html", index);
}

static bool config_changed = false;
void handle_config() {
    static unsigned long token = millis();
    if (server.hasArg("id") && server.hasArg("name")) {
        String _id = server.arg("id");
        String name = server.arg("name");
        name.trim(); /* no leading / trailing whitespace to avoid strange surprises */
        if (_id[0] >= '0' && _id[0] <= '9') {
            int id = _id.toInt();
            if (id >= 0 && id < SENSOR_NUM) {
                id2name[id] = name;
                config_changed = true;
            }
        }
    }
    if (server.hasArg("mqtt_server")) {
        config.mqtt_server = server.arg("mqtt_server");
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("mqtt_port")) {
        String _port = server.arg("mqtt_port");
        config.mqtt_port = _port.toInt();
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("save")) {
        if (server.arg("save") == String(token)) {
            Serial.println("SAVE!");
            save_idmap();
            save_config();
            config_changed = false;
        }
    }
    if (server.hasArg("cancel")) {
        if (server.arg("cancel") == String(token)) {
            load_idmap();
            load_config();
            config_changed = false;
#if 0
            ESP.restart();
            while (true)
                delay(100);
#endif
        }
    }
    if (server.hasArg("format")) {
        if (server.arg("format") == String(token)) {
            LittleFS.begin(true);
            ESP.restart();
            while (true)
                delay(100);
        }
    }
    String resp;
    add_header(resp, "LaCrosse2mqtt Configuration");
    add_current_table(resp, true);
    token = millis();
    resp += "<p>\n"
        "<form action=\"/config.html\">\n"
        "<table>\n"
            " <tr>\n"
                "<td>ID (0-255):</td><td><input type=\"number\" name=\"id\" min=\"0\" max=\"255\"></td>"
                "<td>Name:</td><td><input name=\"name\" value=\"\"></td>"
                "<td><button type=\"submit\">Submit</button></td>\n"
            "</tr>\n"
        "</table>\n"
        "</form>\n"
        "<p></p>\n"
        "MQTT server configuration (Status: connection ";
    if (!mqtt_ok)
        resp += "NOT ";
    resp += "ok)\n"
        "<form action=\"/config.html\">\n"
        "<table>\n"
            "<tr>\n"
                "<td>name / IP address:</td><td><input name=\"mqtt_server\" value=\"" + config.mqtt_server + "\"></td>"
                "<td>Port:</td><td><input type=\"number\" name=\"mqtt_port\" value=\"" + String(config.mqtt_port) + "\"></td>"
                "<td><button type=\"submit\">Submit</button></td>\n"
            "</tr>\n"
        "</table>\n"
        "</form>\n";
    if (config_changed) {
        resp += "<p></p>\nConfig changed, please save or reload old config.\n"
            "<table>\n<tr>\n<td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"save\" value=\"" + String(token) + "\"><button type=\"submit\">Save</button>"
            "</form></td>\n<td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"cancel\" value=\"" + String(token) + "\"><button type=\"submit\">Reload</button>"
            "</form></td>\n</tr>\n</table>\n";
    }
    if (!littlefs_ok) {
        resp += "<p></p>\n"
            "<form action=\"/config.html\">"
            "LittleFS seems damaged. Format it?"
            "<input type=\"hidden\" name=\"format\" value=\"" + String(token) + "\"><button type=\"submit\">Yes, format!</button>"
            "</form>\n";
    }
    resp += "<p><a href=\"/update\">Update software</a></p>\n"
            "<p><a href=\"/\">Main page</a></p>\n";
    add_sysinfo_footer(resp);
    resp += "</body></html>\n";
    server.send(200, "text/html", resp);
}

void setup_web()
{
    if (!load_idmap())
        Serial.println("setup_web ERROR: load_idmap() failed?");
    if (!load_config())
        Serial.println("setup_web ERROR: load_config() failed?");
    server.on("/", handle_index);
    server.on("/index.html", handle_index);
    server.on("/config.html", handle_config);
    server.onNotFound([](){
        server.send(404, "text/plain", "The content you are looking for was not found.\n");
        Serial.println("404: " + server.uri());
    });
    httpUpdater.setup(&server);
    server.begin();
}

void handle_client()
{
    server.handleClient();
}
