// ============================================================================
// 1. INCLUDE THE FRAMEWORK
// ============================================================================
#include "PicoW_IoT_Framework.h"

// ============================================================================
// 2. INCLUDE PROJECT-SPECIFIC LIBRARIES
// ============================================================================
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>

// ============================================================================
// 3. DEFINE GLOBALS REQUIRED BY THE FRAMEWORK
// ============================================================================
volatile RegistryItem registry[MAX_REGISTRY_ITEMS];
volatile int registry_count = 0;
volatile HistoryPoint sensor_history[HISTORY_SIZE];
volatile int history_write_index = 0;
auto_init_mutex(data_mutex);
struct _task_entry_type* sensor_tasks[MAX_REGISTRY_ITEMS];

String ssid_setting;
String pass_setting;
String device_name_setting = "Environment Controller";

// ============================================================================
// 4. PROJECT-SPECIFIC HARDWARE DEFINITIONS AND STATE VARIABLES
// ============================================================================
#define CONTROL_LOGIC_HZ 5
#define BTN_NONE 0
#define BTN_SHORT 1

Adafruit_BME280 bme;
#define LIGHT_SENSOR_PIN A0
#define MOISTURE_SENSOR_PIN A1
#define LIGHT_RELAY_PIN 15
#define HUMIDIFIER_RELAY_PIN 14
#define PUMP_RELAY_PIN 13

unsigned long pump_stop_time = 0;

// ============================================================================
// 5. PROJECT-SPECIFIC FUNCTIONS
// ============================================================================

// --- SENSOR READ CALLBACKS ---
void readBME280(int idx) {
  IoTFramework::setRegistryValue("temp", bme.readTemperature());
  IoTFramework::setRegistryValue("humidity", bme.readHumidity());
  IoTFramework::setRegistryValue("pressure", bme.readPressure() / 100.0F);
}
void readLightSensor(int idx) { IoTFramework::setRegistryValue("light_level", (100.0f * analogRead(LIGHT_SENSOR_PIN)) / 4095.0f); }
void readMoistureSensor(int idx) { IoTFramework::setRegistryValue("soil_moisture", (100.0f * analogRead(MOISTURE_SENSOR_PIN)) / 4095.0f); }

// --- CONTROL LOGIC TASK ---
int controlLogicUpdate(struct _task_entry_type *task, int, int) {
  AddTask(task, 1000 / CONTROL_LOGIC_HZ, &controlLogicUpdate, 0, 0);

  float current_humidity = IoTFramework::getRegistryValue("humidity");
  float target_humidity = IoTFramework::getRegistryValue("target_humidity");
  float light_level = IoTFramework::getRegistryValue("light_level");
  bool light_override = (IoTFramework::getRegistryValue("light_override") > 0);
  uint8_t feed_button_press = IoTFramework::getRegistryValue("feed_button");

  if (current_humidity < target_humidity) digitalWrite(HUMIDIFIER_RELAY_PIN, HIGH);
  else if (current_humidity > target_humidity + 2) digitalWrite(HUMIDIFIER_RELAY_PIN, LOW);

  if (light_override) digitalWrite(LIGHT_RELAY_PIN, HIGH);
  else digitalWrite(LIGHT_RELAY_PIN, (light_level < 20.0f) ? HIGH : LOW);
  
  if (feed_button_press == BTN_SHORT) {
    digitalWrite(PUMP_RELAY_PIN, HIGH);
    pump_stop_time = millis() + 5000;
    IoTFramework::setRegistryValue("feed_button", BTN_NONE);
  }
  if (pump_stop_time > 0 && millis() >= pump_stop_time) {
    digitalWrite(PUMP_RELAY_PIN, LOW);
    pump_stop_time = 0;
  }

  static unsigned long last_hist = 0;
  if (millis() - last_hist >= 1000) {
    last_hist = millis();
    mutex_enter_blocking(&data_mutex);
    history_write_index = (history_write_index + 1) % HISTORY_SIZE;
    sensor_history[history_write_index] = {millis(), IoTFramework::getRegistryValue("temp"), current_humidity, light_level, IoTFramework::getRegistryValue("soil_moisture")};
    mutex_exit(&data_mutex);
  }
  return 0;
}

void handleReboot() { IoTFramework::server.send(200, "text/plain", "Rebooting..."); delay(500); rp2040.restart(); }

// ============================================================================
// 6. IMPLEMENT THE REQUIRED FRAMEWORK CALLBACKS
// ============================================================================
void app_setup() {
  Wire.begin();
  if (!bme.begin(0x76)) Serial.println("Could not find BME280 sensor!");
  pinMode(LIGHT_SENSOR_PIN, INPUT); pinMode(MOISTURE_SENSOR_PIN, INPUT);
  pinMode(LIGHT_RELAY_PIN, OUTPUT); pinMode(HUMIDIFIER_RELAY_PIN, OUTPUT); pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(LIGHT_RELAY_PIN, LOW); digitalWrite(HUMIDIFIER_RELAY_PIN, LOW); digitalWrite(PUMP_RELAY_PIN, LOW);
  AddTaskMilli(CreateTask(), 1000, &controlLogicUpdate, 0, 0);
}

#define SENSOR_AUTO(ID, NAME, INTERVAL_MS, UNIT, CALLBACK) strcpy((char*)registry[i].id,ID);strcpy((char*)registry[i].name,NAME);registry[i].type=TYPE_SENSOR_GENERIC;registry[i].value=0;registry[i].min_val=0;registry[i].max_val=0;registry[i].step=0;strcpy((char*)registry[i].unit,UNIT);registry[i].update_interval_ms=INTERVAL_MS;registry[i].read_callback=CALLBACK;i++;
#define CONTROL_SLIDER(ID, NAME, DEFAULT, MIN, MAX, STEP, UNIT) strcpy((char*)registry[i].id,ID);strcpy((char*)registry[i].name,NAME);registry[i].type=TYPE_CONTROL_SLIDER;registry[i].value=DEFAULT;registry[i].min_val=MIN;registry[i].max_val=MAX;registry[i].step=STEP;strcpy((char*)registry[i].unit,UNIT);registry[i].update_interval_ms=0;registry[i].read_callback=NULL;i++;
#define CONTROL_TOGGLE(ID, NAME) strcpy((char*)registry[i].id,ID);strcpy((char*)registry[i].name,NAME);registry[i].type=TYPE_CONTROL_TOGGLE;registry[i].value=0;registry[i].min_val=0;registry[i].max_val=1;registry[i].step=1;strcpy((char*)registry[i].unit,"");registry[i].update_interval_ms=0;registry[i].read_callback=NULL;i++;
#define CONTROL_BUTTON(ID, NAME) strcpy((char*)registry[i].id,ID);strcpy((char*)registry[i].name,NAME);registry[i].type=TYPE_CONTROL_BUTTON;registry[i].value=0;registry[i].min_val=0;registry[i].max_val=1;registry[i].step=1;strcpy((char*)registry[i].unit,"");registry[i].update_interval_ms=0;registry[i].read_callback=NULL;i++;

void app_register_items() {
  mutex_enter_blocking(&data_mutex);
  int i = 0;
  SENSOR_AUTO("temp", "Temperature", 10000, "°C", readBME280);
  SENSOR_AUTO("humidity", "Humidity", 10000, "%", readBME280); // Inefficient, but demonstrates linking multiple items to one callback
  SENSOR_AUTO("pressure", "Pressure", 60000, "hPa", readBME280);
  SENSOR_AUTO("light_level", "Light Level", 5000, "%", readLightSensor);
  SENSOR_AUTO("soil_moisture", "Soil Moisture", 300000, "%", readMoistureSensor);
  CONTROL_SLIDER("target_humidity", "Target Humidity", 60, 40, 90, 1, "%");
  CONTROL_TOGGLE("light_override", "Light Manual On");
  CONTROL_BUTTON("feed_button", "Watering Cycle");
  registry_count = i;
  mutex_exit(&data_mutex);
}

void app_get_default_identity(String& project_name, String& device_id_prefix) { project_name = "Advanced Environment Controller"; device_id_prefix = "AdvEnv-Setup"; }
void app_get_identity(String& p) { char id[17]; sprintf(id, "%04X%04X", (uint16_t)(rp2040.getChipID() >> 16), (uint16_t)(rp2040.getChipID() >> 0)); p="{"; p+="\"project_name\":\"Advanced Environment Controller\","; p+="\"device_id\":\"env_ctrl_"+String(id)+"\","; p+="\"device_name\":\""+device_name_setting+"\","; p+="\"api_version\":\"1.0\""; p+="}"; }
void app_draw_graph(String& b) { int w=900, h=300, m=50; mutex_enter_blocking(&data_mutex); float minT=50, maxT=0, minH=101, maxH=0; for (int i=0; i<HISTORY_SIZE; i++) { if (sensor_history[i].timestamp > 0) { maxT=max(maxT, sensor_history[i].v1); minT=min(minT, sensor_history[i].v1); maxH=max(maxH, sensor_history[i].v2); minH=min(minH, sensor_history[i].v2); } } float tR=max(maxT-minT, 5.f); float hR=max(maxH-minH, 10.f); auto draw = [&](const char* c, auto get, float minVal, float range) { b += "<polyline fill='none' stroke='" + String(c) + "' stroke-width='2' points='"; for (int i=0; i<HISTORY_SIZE; i++) { int idx=(history_write_index+i)%HISTORY_SIZE; if (sensor_history[idx].timestamp > 0) { int x=m+(w*i/(HISTORY_SIZE-1)); int y=m+h-(int)(((get(sensor_history[idx]) - minVal) / range) * h); b+=String(x)+","+String(y)+" "; } } b+="'/>\n"; }; draw("#ff6384", [&](const HistoryPoint& p){ return p.v1; }, minT, tR); draw("#36a2eb", [&](const HistoryPoint& p){ return p.v2; }, minH, hR); draw("#ffce56", [&](const HistoryPoint& p){ return p.v3; }, 0, 100); mutex_exit(&data_mutex); b += "<text x='"+String(m+10)+"' y='20' fill='#ff6384' font-size='14'>Temp (°C)</text>"; b += "<text x='"+String(m+120)+"' y='20' fill='#36a2eb' font-size='14'>Humidity (%)</text>"; b += "<text x='"+String(m+240)+"' y='20' fill='#ffce56' font-size='14'>Light (%)</text>"; }
void app_add_api_endpoints(WebServer& server) { server.on("/api/reboot", HTTP_GET, handleReboot); }
bool app_load_settings() { if (LittleFS.exists("/config.json")) { File f = LittleFS.open("/config.json", "r"); if (f) { StaticJsonDocument<256> doc; if (deserializeJson(doc, f) == DeserializationError::Ok) { ssid_setting = doc["ssid"] | ""; pass_setting = doc["pass"] | ""; device_name_setting = doc["device_name"] | "Env Controller"; f.close(); Serial.println("Config loaded."); return true; } } } Serial.println("No/invalid config file."); return false; }
void app_save_settings() { StaticJsonDocument<256> doc; doc["ssid"] = ssid_setting; doc["pass"] = pass_setting; doc["device_name"] = device_name_setting; File f = LittleFS.open("/config.json", "w"); if (f) { serializeJson(doc, f); f.close(); Serial.println("Config saved."); } else { Serial.println("Failed to save config."); } }