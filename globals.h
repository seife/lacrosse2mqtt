#ifndef _GLOBALS_H
#define _GLOBALS_H

/* if built with board "ttgo-lora32-v1" these are defined.
 * but this board does not define filesystem layouts.
 * plain "esp32 dev module" does not define these...
 */
#ifndef OLED_SDA
// I2C OLED Display works with SSD1306 driver
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16

// SPI LoRa Radio
#define LORA_SCK  5   // GPIO5 - SX1276 SCK
#define LORA_MISO 19  // GPIO19 - SX1276 MISO
#define LORA_MOSI 27  // GPIO27 - SX1276 MOSI
#define LORA_CS   18  // GPIO18 - SX1276 CS
#define LORA_RST  14  // GPIO14 - SX1276 RST
#define LORA_IRQ  26  // GPIO26 - SX1276 IRQ (interrupt request)

static const uint8_t KEY_BUILTIN = 0;
#endif
#ifndef LED_BUILTIN
static const uint8_t LED_BUILTIN = 2;
#define BUILTIN_LED  LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN
#endif

/* how many bytes is our data frame long? */
#define FRAME_LENGTH 5
/* maximum number of sensors: 64 x 2 channels x 2 datarates */
#define SENSOR_NUM 256

struct Cache {
    unsigned long timestamp;
    uint8_t data[FRAME_LENGTH];
    int8_t rssi;
};

struct Config {
    String mqtt_server;
    uint16_t mqtt_port;
    bool changed;
};

extern Config config;
extern Cache fcache[];
extern String id2name[SENSOR_NUM];
extern bool littlefs_ok;
extern bool mqtt_ok;

/* ugly... */
static inline uint32_t uptime_sec() { return (esp_timer_get_time()/(int64_t)1000000); }

#endif
