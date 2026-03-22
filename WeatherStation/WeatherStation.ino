// ============================================================================
// 1. INCLUDE THE FRAMEWORK
// ============================================================================

// ============================================================================
// 2. INCLUDE PROJECT-SPECIFIC LIBRARIES
// ============================================================================
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define MSG_TYPE_BYTES  1
#define MSG_ID_BYTES    1
#define MSG_INT_BYTES   2
#define MSG_FRAC_BYTES  1
#include "PicoCoreFifo.h"
#include "PicoW_IoT_Framework.h"

// ============================================================================
// 3. DEFINE GLOBALS REQUIRED BY THE FRAMEWORK
// ============================================================================

String ssid_setting;
String pass_setting;
String device_name_setting = "Weather Station";

// ============================================================================
// 4. PROJECT-SPECIFIC HARDWARE DEFINITIONS AND STATE VARIABLES
// ============================================================================
// Pins

// Hardware Objects
Adafruit_BME280 bme;
BH1750 lightMeter(0x23);

// Constants
// Globals



// ============================================================================
// 5. INTERRUPT SERVICE ROUTINES
// ============================================================================

// ============================================================================
// 6. PROJECT-SPECIFIC FUNCTIONS
// ============================================================================

int blinkLED(struct _task_entry_type* task, int mesgid, int led) {
    digitalWrite(led, mesgid == 1 ? HIGH : LOW);
    AddTaskMilli(task, 500, &blinkLED, mesgid == 1 ? 2 : 1, led);
    return 0;
}

int readFreeRAM(struct _task_entry_type* task, int idx, int idx2) {
    AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms, &readFreeRAM, idx, 0);

    float total_ram = 270336.0f; 
    float free_bytes = (float)rp2040.getFreeHeap();
    float percent = (free_bytes / total_ram) * 100.0f;

    registry.set_id(idx, percent);
        Serial.printf(">> readFreeRAM fired idx=%d\n", idx);

    Serial.printf(">> RAM Free: %.2f %% interval_ms: %lu index: %d index2: %d \n", percent, registry.getItem_id(idx)->update_interval_ms, idx, idx2);
    return 0;
}

#include <AM2302-Sensor.h>
constexpr unsigned int SENSOR_PINa {2U};
constexpr unsigned int SENSOR_PINb {3U};
AM2302::AM2302_Sensor am2302a{SENSOR_PINa};
AM2302::AM2302_Sensor am2302b{SENSOR_PINb};

int readAM2302a_temp(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms, &readAM2302a_temp, idx, 0);
    
    am2302a.read();
    float temp = am2302a.get_Temperature() * 1.8 + 32;
    if (temp > 180) return 0;
    registry.set_id(idx, temp);
    //Serial.printf(">> readAM2302a_temp: %.2f °F\n", temp);

    return 0;
}

int readAM2302a_humidity(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms, &readAM2302a_humidity, idx, 0);
    
    am2302a.read();
    float humidity = am2302a.get_Humidity();
    if (humidity < 0.3) return 0;
    registry.set_id(idx, humidity);

    return 0;
}

#include <CPU.h>
CPU cpu;

int readCPUTemp(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms, &readCPUTemp, idx, 0);

    float cpu_temp = cpu.getTemperature() * 1.8 + 32;
    registry.set_id (idx, cpu_temp);

    return 0;
}

// --- SENSOR READ CALLBACKS ---


int readLightSensor(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms, &readLightSensor, idx, 0);
 
    float lux = lightMeter.readLightLevel();
    registry.set_id(idx, lux);

   return 0;
}


// --- CONTROL LOGIC TASK ---
int controlLogicUpdate(struct _task_entry_type* task, int, int) {
    // Run at 1Hz (1000ms)
    AddTaskMilli(task, 1000, &controlLogicUpdate, 0, 0);
    
    // Check virtual button

    return 0;
}

// --- Custom API Handlers ---

// ============================================================================
// 7. IMPLEMENT THE REQUIRED FRAMEWORK CALLBACKS
// ============================================================================
void app_setup() {
    //Wire.begin();

    pinMode(LED_BUILTIN, OUTPUT);
    AddTaskMilli(CreateTask(), 500, &blinkLED, 1, LED_BUILTIN);
    AddTaskMilli(CreateTask(), 1000, &controlLogicUpdate, 0, 0);
}

RegistryDef app_register_items() {
    Serial.println(">> Starting app_register_items");
    RegistryDef def;
    def.count = 0;

    int i = 0;
    
    // Auto-polled sensors
    //SENSOR_AUTO("temp_f", "Temperature", 10000, "°F", readBME280); // Reads BME and updates others
    //SENSOR_AUTO("pressure_inhg", "Pressure", 60000, "inHg", readPressure);
    //SENSOR_AUTO("light_lux", "Light Level", 5000, "lux", readLightSensor);
    //SENSOR_AUTO("soil_moisture", "Soil Moisture", 300000, "%", readSoilMoisture);
    //SENSOR_AUTO("wind_direction", "Wind Direction", 2000, "°", readWindVane);
    //SENSOR_AUTO("wind_speed", "Wind Speed", 1000, "mph", calculateWindAndRain);
    SENSOR_AUTO("temp_a",     "Temperature A",    6004, "°F", readAM2302a_temp);
    SENSOR_AUTO("humidity_a", "Humidity A",       7003,  "%", readAM2302a_humidity);
    SENSOR_AUTO("cpu_temp_f", "CPU Temperature",  8002, "°F", readCPUTemp);
    SENSOR_AUTO("free_ram",   "Free RAM",         9001,  "%", readFreeRAM);
    
    // Manually updated sensors (Updated by other callbacks or logic)
    //SENSOR_MANUAL("temp_c", "Temperature (C)", "°C");
    //SENSOR_MANUAL("humidity", "Humidity", "%");
    //SENSOR_MANUAL("pressure_hpa", "Pressure (hPa)", "hPa");
    //SENSOR_MANUAL("rainfall", "Rainfall", "in");
    SENSOR_MANUAL("heat_index", "Heat Index", "°F");
    SENSOR_MANUAL("dew_point_f", "Dew Point", "°F");
    //SENSOR_MANUAL("irrigation_status", "Irrigation Status", "");
    //SENSOR_MANUAL("next_water_sec", "Next Water In", "sec");

    // Controls
    CONTROL_SLIDER("moisture_target", "Target Moisture", 40, 20, 80, 5, "%");
    CONTROL_SLIDER("water_duration", "Water Duration", 60, 10, 300, 10, "sec");
    CONTROL_SLIDER("water_cooldown", "Min Time Between", 6, 1, 24, 1, "hrs");
    CONTROL_BUTTON("water_now", "Manual Water");
    
    def.count = i;
    return def;
}

// ============================================================================
// LAYOUT TABLE  (the View Definition — completely separate from the registry)
//
// Declares pages, cards, and widgets. References registry items by string id.
// The framework resolves those strings to numeric indices once at boot.
// This layout mirrors the original 4-card layout exactly.
// ============================================================================

const LayoutNode layout_table[] = {
    // id               parent_id       name                 registry_id         widget      props
    {"root",            "",             "Root",              "",                  W_ROOT,     ""},

    // Two pages
    {"main_page",       "root",         "Weather Station",   "",                  W_PAGE,     ""},
    {"system_page",     "root",         "System",            "",                  W_PAGE,     ""},

    // Main page cards
    {"sensor_card",     "main_page",    "Live Sensors",      "",                  W_CARD,     "width:400"},
    {"control_card",    "main_page",    "Controls",          "",                  W_CARD,     ""},
    {"settings_card",   "main_page",    "Settings",          "",                  W_CARD,     ""},

    // System page cards
    {"status_card",     "system_page",  "Status",            "",                  W_CARD,     ""},

    // Sensor card
    {"w_temp_a",        "sensor_card",  "Temperature A",     "temp_a",            W_TEXT,     ""},
    {"help_temp",       "sensor_card",  "",                  "",                  W_HELP,     ""},
    {"w_humidity_a",    "sensor_card",  "Humidity A",        "humidity_a",        W_TEXT,     ""},
    {"help_humidity",   "sensor_card",  "",                  "",                  W_HELP,     ""},
    {"w_heat_index",    "sensor_card",  "Heat Index",        "heat_index",        W_TEXT,     ""},
    {"w_dew_point",     "sensor_card",  "Dew Point",         "dew_point_f",       W_TEXT,     ""},
    {"w_temp_a",        "sensor_card",  "Temperature A",     "temp_a",            W_DIAL,     "min:0,max:120"},
    {"help_temp",       "sensor_card",  "",                  "",                  W_HELP,     ""},
    {"w_humidity_a",    "sensor_card",  "Humidity A",        "humidity_a",        W_BAR,      "min:0,max:100"},
    {"help_humidity",   "sensor_card",  "",                  "",                  W_HELP,     ""},
    {"sensor_card_info", "sensor_card", "",                  "",                  W_HTML,     ""},

    // Status card — same items that landed in the status card before
    {"w_cpu_temp",      "status_card",  "CPU Temperature",   "cpu_temp_f",        W_TEXT,     ""},
    {"w_free_ram",      "status_card",  "Free RAM",          "free_ram",          W_TEXT,     ""},

    // Controls card — sliders
    {"w_moist_tgt",     "control_card", "Target Moisture",   "moisture_target",   W_SLIDER,   ""},
    {"w_water_dur",     "control_card", "Water Duration",    "water_duration",    W_SLIDER,   ""},
    {"w_water_cool",    "control_card", "Min Time Between",  "water_cooldown",    W_SLIDER,   ""},

    // Settings card — button
    {"w_water_now",     "settings_card","Manual Water",      "water_now",         W_BUTTON,   ""},
};

const int layout_count = sizeof(layout_table) / sizeof(LayoutNode);

// ============================================================================
// HELP TABLE  (static HTML strings streamed as hover tooltip content)
// Reference these from the layout_table using W_HELP and the help node id.
// ============================================================================

const HelpNode help_table[] = {
    {"help_temp",
     "<b>Temperature A</b><br>AM2302 sensor on GPIO 2.<br>Range: -40 to 80&deg;C / -40 to 176&deg;F.<br>Updates every ~6 seconds."},

    {"help_humidity",
     "<b>Humidity A</b><br>AM2302 sensor on GPIO 2.<br>Range: 0&ndash;100% RH.<br>Updates every ~7 seconds."},

    {"help_moisture_target",
     "<b>Target Moisture</b><br>The soil moisture % the irrigation system will try to maintain.<br>When soil drops below this value, watering begins."},

    {"help_water_now",
     "<b>Manual Water</b><br>Triggers an immediate watering cycle regardless of current soil moisture."},
    
    {"sensor_card_info",
     "<p><strong>Outdoor Conditions</strong></p><p>Live readings from the <strong>AM2302</strong> sensor on GPIO 2. Temperature and humidity are polled every 6&ndash;7 seconds. <strong>Heat Index</strong> and <strong>Dew Point</strong> are calculated from those readings.</p><p style='color:#888;font-size:.85em'>Sensor range: -40 to 80&deg;C &bull; 0&ndash;100% RH</p>"},
    
};

const int help_count = sizeof(help_table) / sizeof(HelpNode);

void app_get_default_identity(String& name, String& prefix) {
    Serial.println(">> Starting app_get_default_identity");
    name = "Weather Station"; 
    prefix = "Weather-Setup"; 
}

void app_get_identity(String& p) {
    Serial.println(">> Starting app_get_identity");
    char id[17];
    const char* full_id = rp2040.getChipID();
    strncpy(id, full_id + (strlen(full_id) - 8), 8);
    id[8] = '\0';
    
    p = "{";
    p += "\"project_name\":\"Weather Station\",";
    p += "\"device_id\":\"weather_" + String(id) + "\",";
    p += "\"device_name\":\"" + device_name_setting + "\",";
    p += "\"api_version\":\"1.0\"";
    p += "}";
}

#define CONFIG_FLASH_OFFSET (2048 * 1024 - FLASH_SECTOR_SIZE)
#define CONFIG_MAGIC 0xDEADBEEF

struct FlashConfig { uint32_t magic; char ssid[64]; char pass[64]; char device_name[64]; };

bool app_load_settings() {
  Serial.println(">> Starting app_load_settings");
  const FlashConfig* cfg = (const FlashConfig*)(XIP_BASE + CONFIG_FLASH_OFFSET);
  if (cfg->magic != CONFIG_MAGIC) { Serial.println("No/invalid config."); return false; }
  ssid_setting = String(cfg->ssid);
  pass_setting = String(cfg->pass);
  device_name_setting = String(cfg->device_name);
  Serial.println("Config loaded.");
  return true;
}

void app_save_settings() {
  Serial.println(">> Starting app_save_settings");
  FlashConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  strncpy(cfg.ssid, ssid_setting.c_str(), 63);
  strncpy(cfg.pass, pass_setting.c_str(), 63);
  strncpy(cfg.device_name, device_name_setting.c_str(), 63);
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(CONFIG_FLASH_OFFSET, (const uint8_t*)&cfg, sizeof(FlashConfig));
  restore_interrupts(ints);
  Serial.println("Config saved.");
}
