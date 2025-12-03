// ============================================================================
// 1. INCLUDE THE FRAMEWORK
// ============================================================================
#include "PicoW_IoT_Framework.h"

// ============================================================================
// 2. INCLUDE PROJECT-SPECIFIC LIBRARIES
// ============================================================================
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ============================================================================
// 3. DEFINE GLOBALS REQUIRED BY THE FRAMEWORK
// ============================================================================
volatile RegistryItem registry[MAX_REGISTRY_ITEMS];
volatile int registry_count = 0;
volatile HistoryPoint sensor_history[HISTORY_SIZE];
volatile int history_write_index = 0;

struct _task_entry_type* sensor_tasks[MAX_REGISTRY_ITEMS];

String ssid_setting;
String pass_setting;
String device_name_setting = "Weather Station";

// ============================================================================
// 4. PROJECT-SPECIFIC HARDWARE DEFINITIONS AND STATE VARIABLES
// ============================================================================
// Pins
#define ANEMOMETER_PIN 15
#define WIND_VANE_PIN 26
#define RAIN_GAUGE_PIN 14
#define SOIL_MOISTURE_PIN 27
#define WATER_RELAY_PIN 16

// Hardware Objects
Adafruit_BME280 bme;
BH1750 lightMeter(0x23);

// Constants
#define WIND_CALIBRATION_FACTOR 1.492
#define RAIN_PER_TIP 0.011
#define SOIL_DRY_VALUE 3000
#define SOIL_WET_VALUE 1200

// Globals
volatile unsigned long wind_pulse_count = 0;
volatile unsigned long rain_pulse_count = 0;

// Irrigation State Machine
enum IrrigationState { IRRIGATION_IDLE, IRRIGATION_WATERING, IRRIGATION_COOLDOWN };
IrrigationState irrigation_state = IRRIGATION_IDLE;
unsigned long watering_start_time = 0;
unsigned long last_watering_time = 0;
bool manual_water_trigger = false;

// Wind Direction Map
const float VANE_VOLTAGES[] = {3.84,1.98,2.25,0.41,0.45,0.32,0.90,0.62,1.40,1.19,3.08,2.93,4.62,4.04,4.78,3.43};
const int VANE_DIRECTIONS[] = {0,22,45,67,90,112,135,157,180,202,225,247,270,292,315,337};

// ============================================================================
// 5. INTERRUPT SERVICE ROUTINES
// ============================================================================
void IRAM_ATTR windSpeedISR() { wind_pulse_count++; }
void IRAM_ATTR rainGaugeISR() { rain_pulse_count++; }

// ============================================================================
// 6. PROJECT-SPECIFIC FUNCTIONS
// ============================================================================

// --- SENSOR READ CALLBACKS ---

void readBME280(int idx) {
    float temp_c = bme.readTemperature();
    float humidity = bme.readHumidity();
    float temp_f = (temp_c * 9.0 / 5.0) + 32.0;
    
    // Derived Calculations
    float heat_index = (temp_f < 80.0) ? temp_f : (-42.379 + 2.04901523*temp_f + 10.14333127*humidity - 0.22475541*temp_f*humidity - 0.00683783*temp_f*temp_f - 0.05481717*humidity*humidity + 0.00122874*temp_f*temp_f*humidity + 0.00085282*temp_f*humidity*humidity - 0.00000199*temp_f*temp_f*humidity*humidity);
    
    // Magnus formula for Dew Point
    float a=17.27, b=237.7; 
    float alpha=((a*temp_c)/(b+temp_c))+log(humidity/100.0); 
    float dew_point_c=(b*alpha)/(a-alpha);
    
    setRegistryValue("temp_c", temp_c);
    setRegistryValue("temp_f", temp_f);
    setRegistryValue("humidity", humidity);
    setRegistryValue("heat_index", heat_index);
    setRegistryValue("dew_point_f", (dew_point_c * 9.0 / 5.0) + 32.0);
}

void readPressure(int idx) {
    float pressure_hpa = bme.readPressure() / 100.0F;
    setRegistryValue("pressure_hpa", pressure_hpa);
    setRegistryValue("pressure_inhg", pressure_hpa * 0.02953);
}

void readLightSensor(int idx) {
    float lux = lightMeter.readLightLevel();
    setRegistryValue("light_lux", lux);
}

void readSoilMoisture(int idx) {
    int raw = analogRead(SOIL_MOISTURE_PIN);
    float percent = 100.0 - ((raw - SOIL_WET_VALUE) * 100.0 / (SOIL_DRY_VALUE - SOIL_WET_VALUE));
    setRegistryValue("soil_moisture", constrain(percent, 0.0, 100.0));
}

void readWindVane(int idx) {
    float voltage = analogRead(WIND_VANE_PIN) * (3.3 / 4095.0);
    int closest_index = 0; 
    float min_diff = 10.0;
    for (int i = 0; i < 16; i++) {
        float diff = abs(voltage - VANE_VOLTAGES[i]);
        if (diff < min_diff) { min_diff = diff; closest_index = i; }
    }
    setRegistryValue("wind_direction", VANE_DIRECTIONS[closest_index]);
}

void calculateWindAndRain(int idx) {
    static unsigned long last_calc_time = 0;
    unsigned long elapsed = millis() - last_calc_time;
    if (elapsed == 0) return;
    last_calc_time = millis();
    
    noInterrupts();
    unsigned long current_wind_pulses = wind_pulse_count; 
    wind_pulse_count = 0;
    unsigned long total_rain_pulses = rain_pulse_count;
    // We don't reset rain count here, it accumulates until manual reset
    interrupts();
    
    float wind_speed_mph = (current_wind_pulses * WIND_CALIBRATION_FACTOR * 1000.0) / elapsed;
    setRegistryValue("wind_speed", wind_speed_mph);
    setRegistryValue("rainfall", total_rain_pulses * RAIN_PER_TIP);
}

// --- CONTROL LOGIC TASK ---
int controlLogicUpdate(struct _task_entry_type* task, int, int) {
    // Run at 1Hz (1000ms)
    AddTaskMilli(task, 1000, &controlLogicUpdate, 0, 0);

    float soil_moisture = getRegistryValue("soil_moisture");
    float target_moisture = getRegistryValue("moisture_target");
    float water_duration_s = getRegistryValue("water_duration");
    float cooldown_h = getRegistryValue("water_cooldown");
    
    // Check virtual button
    if (getRegistryValue("water_now") > 0) {
        manual_water_trigger = true;
        setRegistryValue("water_now", 0); // Reset button
    }
    
    unsigned long now = millis();
    unsigned long cooldown_ms = (unsigned long)(cooldown_h * 3600.0 * 1000.0);
    
    switch (irrigation_state) {
        case IRRIGATION_IDLE: {
            bool should_water = manual_water_trigger || (soil_moisture < target_moisture && (last_watering_time == 0 || (now - last_watering_time) >= cooldown_ms));
            if (should_water) {
                manual_water_trigger = false;
                irrigation_state = IRRIGATION_WATERING;
                watering_start_time = now;
                digitalWrite(WATER_RELAY_PIN, HIGH);
                setRegistryValue("irrigation_status", 1);
            } else {
                setRegistryValue("irrigation_status", 0);
            }
            break;
        }
        case IRRIGATION_WATERING:
            if (now - watering_start_time >= (unsigned long)(water_duration_s * 1000.0)) {
                irrigation_state = IRRIGATION_COOLDOWN;
                last_watering_time = now;
                digitalWrite(WATER_RELAY_PIN, LOW);
                setRegistryValue("irrigation_status", 2);
            }
            break;
        case IRRIGATION_COOLDOWN:
            if (now - last_watering_time >= cooldown_ms) {
                irrigation_state = IRRIGATION_IDLE;
            }
            break;
    }

    // Update countdown timer for UI
    if (irrigation_state == IRRIGATION_COOLDOWN && last_watering_time > 0) {
        unsigned long elapsed = now - last_watering_time;
        float remaining = (elapsed < cooldown_ms) ? (cooldown_ms - elapsed) / 1000.0 : 0;
        setRegistryValue("next_water_sec", remaining);
    } else {
        setRegistryValue("next_water_sec", 0);
    }

    // Update History for Graph
    static unsigned long last_hist = 0;
    if (millis() - last_hist >= 1000) {
        last_hist = millis();
        mutex_enter_blocking(&data_mutex);
        history_write_index = (history_write_index + 1) % HISTORY_SIZE;
        sensor_history[history_write_index].timestamp = millis();
        sensor_history[history_write_index].v1 = getRegistryValue("temp_f");
        sensor_history[history_write_index].v2 = getRegistryValue("humidity");
        sensor_history[history_write_index].v3 = soil_moisture;
        sensor_history[history_write_index].v4 = getRegistryValue("rainfall");
        mutex_exit(&data_mutex);
    }
    return 0;
}

// --- Custom API Handlers ---
void handleResetRain() { 
    noInterrupts(); 
    rain_pulse_count = 0; 
    interrupts(); 
    setRegistryValue("rainfall", 0.0); 
    server.send(200, "text/plain", "Rain gauge reset."); 
}

void handleWaterNow() { 
    manual_water_trigger = true; 
    server.send(200, "text/plain", "Manual watering triggered."); 
}

// ============================================================================
// 7. IMPLEMENT THE REQUIRED FRAMEWORK CALLBACKS
// ============================================================================
void app_setup() {
    Wire.begin();
    if (!bme.begin(0x76)) Serial.println("BME280 not found!");
    if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) Serial.println("BH1750 not found!");
    
    pinMode(ANEMOMETER_PIN, INPUT_PULLUP); 
    attachInterrupt(digitalPinToInterrupt(ANEMOMETER_PIN), windSpeedISR, FALLING);
    
    pinMode(RAIN_GAUGE_PIN, INPUT_PULLUP); 
    attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE_PIN), rainGaugeISR, FALLING);
    
    pinMode(WIND_VANE_PIN, INPUT); 
    pinMode(SOIL_MOISTURE_PIN, INPUT);
    pinMode(WATER_RELAY_PIN, OUTPUT); 
    digitalWrite(WATER_RELAY_PIN, LOW);
    
    // Start logic task
    AddTaskMilli(CreateTask(), 1000, &controlLogicUpdate, 0, 0);
}

// Macros to make app_register_items clean (copied from NonEvent example)
#define SENSOR_AUTO(ID, NAME, INTERVAL_MS, UNIT, CALLBACK) \
  strcpy((char*)registry[i].id, ID); \
  strcpy((char*)registry[i].name, NAME); \
  registry[i].type = TYPE_SENSOR_GENERIC; \
  registry[i].value = 0; \
  registry[i].min_val = 0; \
  registry[i].max_val = 0; \
  registry[i].step = 0; \
  strcpy((char*)registry[i].unit, UNIT); \
  registry[i].update_interval_ms = INTERVAL_MS; \
  registry[i].read_callback = CALLBACK; \
  i++;

#define SENSOR_MANUAL(ID, NAME, UNIT) \
  strcpy((char*)registry[i].id, ID); \
  strcpy((char*)registry[i].name, NAME); \
  registry[i].type = TYPE_SENSOR_GENERIC; \
  registry[i].value = 0; \
  registry[i].min_val = 0; \
  registry[i].max_val = 0; \
  registry[i].step = 0; \
  strcpy((char*)registry[i].unit, UNIT); \
  registry[i].update_interval_ms = 0; \
  registry[i].read_callback = NULL; \
  i++;

#define CONTROL_SLIDER(ID, NAME, DEFAULT, MIN, MAX, STEP, UNIT) \
  strcpy((char*)registry[i].id, ID); \
  strcpy((char*)registry[i].name, NAME); \
  registry[i].type = TYPE_CONTROL_SLIDER; \
  registry[i].value = DEFAULT; \
  registry[i].min_val = MIN; \
  registry[i].max_val = MAX; \
  registry[i].step = STEP; \
  strcpy((char*)registry[i].unit, UNIT); \
  registry[i].update_interval_ms = 0; \
  registry[i].read_callback = NULL; \
  i++;

#define CONTROL_BUTTON(ID, NAME) \
  strcpy((char*)registry[i].id, ID); \
  strcpy((char*)registry[i].name, NAME); \
  registry[i].type = TYPE_CONTROL_BUTTON; \
  registry[i].value = 0; \
  registry[i].min_val = 0; \
  registry[i].max_val = 1; \
  registry[i].step = 1; \
  strcpy((char*)registry[i].unit, ""); \
  registry[i].update_interval_ms = 0; \
  registry[i].read_callback = NULL; \
  i++;

void app_register_items() {
    mutex_enter_blocking(&data_mutex);
    int i = 0;
    
    // Auto-polled sensors
    SENSOR_AUTO("temp_f", "Temperature", 10000, "°F", readBME280); // Reads BME and updates others
    SENSOR_AUTO("pressure_inhg", "Pressure", 60000, "inHg", readPressure);
    SENSOR_AUTO("light_lux", "Light Level", 5000, "lux", readLightSensor);
    SENSOR_AUTO("soil_moisture", "Soil Moisture", 300000, "%", readSoilMoisture);
    SENSOR_AUTO("wind_direction", "Wind Direction", 2000, "°", readWindVane);
    SENSOR_AUTO("wind_speed", "Wind Speed", 1000, "mph", calculateWindAndRain);
    
    // Manually updated sensors (Updated by other callbacks or logic)
    SENSOR_MANUAL("temp_c", "Temperature (C)", "°C");
    SENSOR_MANUAL("humidity", "Humidity", "%");
    SENSOR_MANUAL("pressure_hpa", "Pressure (hPa)", "hPa");
    SENSOR_MANUAL("rainfall", "Rainfall", "in");
    SENSOR_MANUAL("heat_index", "Heat Index", "°F");
    SENSOR_MANUAL("dew_point_f", "Dew Point", "°F");
    SENSOR_MANUAL("irrigation_status", "Irrigation Status", "");
    SENSOR_MANUAL("next_water_sec", "Next Water In", "sec");
    
    // Controls
    CONTROL_SLIDER("moisture_target", "Target Moisture", 40, 20, 80, 5, "%");
    CONTROL_SLIDER("water_duration", "Water Duration", 60, 10, 300, 10, "sec");
    CONTROL_SLIDER("water_cooldown", "Min Time Between", 6, 1, 24, 1, "hrs");
    CONTROL_BUTTON("water_now", "Manual Water");
    
    registry_count = i;
    mutex_exit(&data_mutex);
}

void app_get_default_identity(String& name, String& prefix) {
    name = "Weather Station"; 
    prefix = "Weather-Setup"; 
}

void app_get_identity(String& p) {
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

void app_draw_graph(String& b) {
    int w = 900, h = 300, m = 50;
    mutex_enter_blocking(&data_mutex);
    
    // Auto-scale ranges
    float minT=200,maxT=-200,minH=101,maxH=0,minM=101,maxM=0,maxR=0;
    for(int i=0;i<HISTORY_SIZE;i++){
        if(sensor_history[i].timestamp>0){
            minT=min(minT,sensor_history[i].v1); maxT=max(maxT,sensor_history[i].v1);
            minH=min(minH,sensor_history[i].v2); maxH=max(maxH,sensor_history[i].v2);
            minM=min(minM,sensor_history[i].v3); maxM=max(maxM,sensor_history[i].v3);
            maxR=max(maxR,sensor_history[i].v4);
        }
    }
    float tR=max(maxT-minT,10.f);
    float hR=max(maxH-minH,10.f);
    float mR=max(maxM-minM,10.f);
    float rR=max(maxR,0.5f);
    
    // Lambda drawing helper
    auto draw=[&](const char*c,auto get,float minV,float range){
        b+="<polyline fill='none' stroke='"+String(c)+"' stroke-width='2' points='";
        for(int i=0;i<HISTORY_SIZE;i++){
            int idx=(history_write_index+i)%HISTORY_SIZE;
            if(sensor_history[idx].timestamp>0){
                int x=m+(w*i/(HISTORY_SIZE-1));
                int y=m+h-(int)(((get(sensor_history[idx])-minV)/range)*h);
                b+=String(x)+","+String(y)+" ";
            }
        }
        b+="'/>\n";
    };
    
    draw("#ff6384",[&](const volatile HistoryPoint&p){return p.v1;},minT,tR); // Temp
    draw("#36a2eb",[&](const volatile HistoryPoint&p){return p.v2;},minH,hR); // Humidity
    draw("#4bc0c0",[&](const volatile HistoryPoint&p){return p.v3;},minM,mR); // Soil
    draw("#9966ff",[&](const volatile HistoryPoint&p){return p.v4;},0,rR);    // Rain
    
    mutex_exit(&data_mutex);
    
    b+="<text x='"+String(m+10)+"' y='20' fill='#ff6384'>Temp(F)</text>";
    b+="<text x='"+String(m+110)+"' y='20' fill='#36a2eb'>Hum(%)</text>";
    b+="<text x='"+String(m+200)+"' y='20' fill='#4bc0c0'>Soil(%)</text>";
    b+="<text x='"+String(m+280)+"' y='20' fill='#9966ff'>Rain(in)</text>";
}

void app_add_api_endpoints(WebServer& s) {
    s.on("/api/reset_rain", HTTP_GET, handleResetRain);
    s.on("/api/water_now", HTTP_GET, handleWaterNow);
}

bool app_load_settings() {
    if(LittleFS.exists("/config.json")){
        File f=LittleFS.open("/config.json","r");
        if(f){
            StaticJsonDocument<256>doc;
            if(deserializeJson(doc,f)==DeserializationError::Ok){
                ssid_setting = doc["ssid"] | "";
                pass_setting = doc["pass"] | "";
                device_name_setting = doc["device_name"] | "Weather Station";
                f.close();
                Serial.println("Config loaded.");
                return true;
            }
        }
    }
    Serial.println("No/invalid config.");
    return false;
}

void app_save_settings() {
    StaticJsonDocument<256>doc;
    doc["ssid"] = ssid_setting;
    doc["pass"] = pass_setting;
    doc["device_name"] = device_name_setting;
    File f = LittleFS.open("/config.json", "w");
    if(f){
        serializeJson(doc,f);
        f.close();
        Serial.println("Config saved.");
    } else {
        Serial.println("Failed to save config.");
    }
}