#include "webfrontend.h"
#include "lacrosse.h"
#include "globals.h"
#include <AsyncElegantOTA.h>
#include <LITTLEFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
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
    File idmapdir = LITTLEFS.open("/idmap");
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
        int id = name2id(fname, strlen("/idmap/"));
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
    File cfg = LITTLEFS.open("/config.json");
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
    LITTLEFS.remove("/config.json");
    File cfg = LITTLEFS.open("/config.json", FILE_WRITE);
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
    cfg = LITTLEFS.open("/config.json");
    Serial.println(read_file(cfg));
    cfg.close();
    Serial.println("---end config.json:");
    return ret;
}

bool save_idmap()
{
    if (!littlefs_ok)
        return false;
    File idmapdir = LITTLEFS.open("/idmap");
    if (!idmapdir) {
        Serial.println("SAVE: /idmapdir not found");
        if (LITTLEFS.mkdir("/idmap"))
            idmapdir = LITTLEFS.open("/idmap");
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
        int id = name2id(file.name(), strlen("/idmap/"));
        String fullname = String(file.name());
        file.close();
        if (id > -1 && id2name[id].length() == 0) {
            Serial.print("removing ");
            Serial.println(fullname);
            if (!LITTLEFS.remove(fullname))
                Serial.println("failed?");
        }
        file = idmapdir.openNextFile();
    }
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (id2name[i].length() == 0)
            continue;
        String fullname = String("/idmap/") + String((i < 10)?"0":"") + String(i, HEX);
        if (LITTLEFS.exists(fullname)) {
            //Serial.println("Exists: " + fullname);
            File comp = LITTLEFS.open(fullname);
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
        File file = LITTLEFS.open(fullname, FILE_WRITE);
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
    s += "<table><tr><th>ID</th><th>Temperature</th><th>RSSI</th><th>Name</th><th>Age (ms)</th><th>Battery</th><th>New?</th>";
    if (rawdata)
        s += "<th>Raw Frame Data</th>";
    s += "</tr>\n";
    for (int i = 0; i < 255; i++) {
        if (fcache[i].timestamp == 0)
            continue;
        LaCrosse::Frame f;
        if (i & 0x80)
            f.rate = 9579;
        else
            f.rate = 17241;
        if (! LaCrosse::TryHandleData(fcache[i].data, &f))
            continue;
        s +=  "<tr><td>" + String(f.ID) +
             "</td><td>" + String(f.temp, 1) +
             "</td><td>" + String(fcache[i].rssi) +
             "</td><td>" + id2name[f.ID] +
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
    s += "<!DOCTYPE HTML><html lang=\"en\"><head>"
        "<title>" + title + "</title>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<style>"
        "td, th {"
          "text-align: right;"
        "}"
        "table td:nth-child(8) {"
        " font-family: monospace;"
        " font-size: 10pt;"
        "}"
        "</style>"
        "</head><body>"
        "<H1>" + title + "</H1>";
}


//void handle_index() {
void handle_index(AsyncWebServerRequest *request) {
    // TODO: use server.hostHeader()?
    unsigned long uptime = millis();
    String IP = WiFi.localIP().toString();
    String index;
    add_header(index, "LaCrosse2mqtt");
    add_current_table(index, false);
    index +=
        "<br><a href=\"/config.html\">Configuration page</a>"
        "</body>";
    request->send(200, "text/html", index);
}

static bool config_changed = false;
void handle_config(AsyncWebServerRequest *request) {
    static unsigned long token = millis();
    if (request->hasArg("id") && request->hasArg("name")) {
        String _id = request->arg("id");
        String name = request->arg("name");
        if (_id[0] >= '0' && _id[0] <= '9') {
            int id = _id.toInt();
            if (id >= 0 && id < SENSOR_NUM) {
                id2name[id] = name;
                config_changed = true;
            }
        }
    }
    if (request->hasArg("mqtt_server")) {
        config.mqtt_server = request->arg("mqtt_server");
        config.changed = true;
        config_changed = true;
    }
    if (request->hasArg("mqtt_port")) {
        String _port = request->arg("mqtt_port");
        config.mqtt_port = _port.toInt();
        config.changed = true;
        config_changed = true;
    }
    if (request->hasArg("save")) {
        if (request->arg("save") == String(token)) {
            Serial.println("SAVE!");
            save_idmap();
            save_config();
            config_changed = false;
        }
    }
    if (request->hasArg("cancel")) {
        if (request->arg("cancel") == String(token)) {
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
    if (request->hasArg("format")) {
        if (request->arg("format") == String(token)) {
            LITTLEFS.begin(true);
            ESP.restart();
            while (true)
                delay(100);
        }
    }
    String resp;
    add_header(resp, "LaCrosse2mqtt Configuration");
    add_current_table(resp, true);
    token = millis();
    resp += "<br>\n"
        "<form action=\"/config.html\">"
        "<table>"
            "<tr>"
                "<td>ID (0-255):</td><td><input type=\"number\" name=\"id\" min=\"0\" max=\"255\"></td>"
                "<td>Name:</td><td><input type=\"text\" name=\"name\" value=\"\"></td>"
                "<td><input type=\"submit\" value=\"Submit\"></td>"
            "</tr>"
        "</table>"
        "</form>"
        "<br>MQTT server configuration (Status: connection ";
    if (!mqtt_ok)
        resp += "NOT ";
    resp += "ok)"
        "<form action=\"/config.html\">"
        "<table>"
            "<tr>"
                "<td>name / IP address:</td><td><input type=\"text\" name=\"mqtt_server\" value=\"" + config.mqtt_server + "\"></td>"
                "<td>Port:</td><td><input type=\"number\" name=\"mqtt_port\" value=\"" + String(config.mqtt_port) + "\"></td>"
                "<td><input type=\"submit\" value=\"Submit\"></td>"
            "</tr>"
        "</table>"
        "</form>";
    if (config_changed) {
        resp += "Config changed, please save or reload old config.<br>\n"
            "<table><tr><td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"save\" value=\"" + String(token) + "\"><button type=\"submit\">Save</button>"
            "</form></td><td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"cancel\" value=\"" + String(token) + "\"><button type=\"submit\">Reload</button>"
            "</form></td></tr></table>\n";
    }
    if (!littlefs_ok) {
        resp += "<br><br>"
            "<form action=\"/config.html\">"
            "LITTLEFS seems damaged. Format it?"
            "<input type=\"hidden\" name=\"format\" value=\"" + String(token) + "\"><button type=\"submit\">Yes, format!</button>"
            "</form>";
    }
    resp += "<br><a href=\"/update\">Update software</a>"
            "<br><a href=\"/\">Main page</a>"
            "</body></html>\n";
    request->send(200, "text/html", resp);
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
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "The content you are looking for was not found.\n");
        Serial.println("404: " + request->url());
    });
    AsyncElegantOTA.begin(&server);
    server.begin();
}
