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

struct _task_entry_type* sensor_tasks[MAX_REGISTRY_ITEMS];

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

int blinkLED(struct _task_entry_type* task, int led, int mesgid) {
    digitalWrite(led, mesgid == 1 ? HIGH : LOW);
    AddTaskMilli(task, 500, &blinkLED, mesgid == 1 ? 2 : 1, led);
    return 0;
}

#include <AM2302-Sensor.h>
constexpr unsigned int SENSOR_PINa {2U};
constexpr unsigned int SENSOR_PINb {3U};
AM2302::AM2302_Sensor am2302a{SENSOR_PINa};
AM2302::AM2302_Sensor am2302b{SENSOR_PINb};

int readAM2302a_temp(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem(idx)->update_interval_ms, &readAM2302a_temp, idx, 0);
    
    am2302a.read();
    float temp = am2302a.get_Temperature() * 1.8 + 32;
    if (temp > 180) return 0;
    registry.set("temp_a", temp);
    //Serial.printf(">> readAM2302a_temp: %.2f °F\n", temp);

    return 0;
}

int readAM2302a_humidity(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem(idx)->update_interval_ms, &readAM2302a_humidity, idx, 0);
    
    am2302a.read();
    float humidity = am2302a.get_Humidity();
    if (humidity < 0.3) return 0;
    registry.set("humidity_a", humidity);

    return 0;
}

#include <CPU.h>
CPU cpu;

int readCPUTemp(struct _task_entry_type* task, int idx, int) {
    float cpu_temp = cpu.getTemperature() * 1.8 + 32;
    registry.set("cpu_temp_f", cpu_temp);

    AddTaskMilli(task, registry.getItem(idx)->update_interval_ms, &readCPUTemp, idx, 0);
    return 0;
}

// --- SENSOR READ CALLBACKS ---


int readLightSensor(struct _task_entry_type* task, int idx, int) {
    AddTaskMilli(task, registry.getItem(idx)->update_interval_ms, &readLightSensor, idx, 0);
 
    float lux = lightMeter.readLightLevel();
    registry.set("light_lux", lux);

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
    SENSOR_AUTO("temp_a",     "Temperature A", 6000, "°F", readAM2302a_temp);
    SENSOR_AUTO("humidity_a", "Humidity A",    6000, "%",  readAM2302a_humidity);
    SENSOR_AUTO("cpu_temp_f", "CPU Temperature", 12000, "°F", readCPUTemp);
    
    // Manually updated sensors (Updated by other callbacks or logic)
    //SENSOR_MANUAL("temp_c", "Temperature (C)", "°C");
    //SENSOR_MANUAL("humidity", "Humidity", "%");
    //SENSOR_MANUAL("pressure_hpa", "Pressure (hPa)", "hPa");
    //SENSOR_MANUAL("rainfall", "Rainfall", "in");
    //SENSOR_MANUAL("heat_index", "Heat Index", "°F");
    //SENSOR_MANUAL("dew_point_f", "Dew Point", "°F");
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
