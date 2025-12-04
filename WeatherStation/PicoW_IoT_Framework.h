#ifndef PICOW_IOT_FRAMEWORK_H
#define PICOW_IOT_FRAMEWORK_H

// ============================================================================
// SECTION 1: FRAMEWORK LIBRARIES & SHARED DEFINITIONS
// ============================================================================
#include <Arduino.h>
#include <SchedulerLP_pico.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LEAmDNS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <stdio.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/sync.h>
#include <pico/time.h>

#define MAX_REGISTRY_ITEMS 25
#define HISTORY_SIZE 180

enum ItemType { TYPE_SENSOR_GENERIC,
                TYPE_SENSOR_STATE,
                TYPE_CONTROL_SLIDER,
                TYPE_CONTROL_TOGGLE,
                TYPE_CONTROL_BUTTON };

struct RegistryItem {
  char id[20], name[32];
  ItemType type;
  volatile float value;
  float min_val, max_val, step;
  char unit[8];
  // FIELDS FOR AUTO-POLLING:
  uint32_t update_interval_ms;
  void (*read_callback)(int idx);
  unsigned long last_update_time;
};

struct HistoryPoint {
  uint32_t timestamp;
  float v1, v2, v3, v4;
};

extern volatile RegistryItem registry[MAX_REGISTRY_ITEMS];
extern volatile int registry_count;
extern volatile HistoryPoint sensor_history[HISTORY_SIZE];
extern volatile int history_write_index;
extern struct _task_entry_type* sensor_tasks[MAX_REGISTRY_ITEMS];  // Task storage
auto_init_mutex(data_mutex);

// ============================================================================
// SECTION 2: APPLICATION CALLBACKS (to be implemented in your .ino file)
// ============================================================================
void app_setup();
void app_register_items();
void app_draw_graph(String& svg_body);
void app_get_identity(String& json_payload);
void app_get_default_identity(String& project_name, String& device_id_prefix);
void app_add_api_endpoints(WebServer& server);
bool app_load_settings();
void app_save_settings();

// ============================================================================
// SECTION 3: FRAMEWORK IMPLEMENTATION (The "Black Box")
// ============================================================================

WebServer server(80);
DNSServer dnsServer;
bool in_config_mode = false;

static int findRegistryItem(const char* id) {
  for (int i = 0; i < registry_count; i++) {
    if (strcmp((const char*)registry[i].id, id) == 0) return i;
  }
  return -1;
}
float getRegistryValue(const char* id, float default_val = 0.0f) {
  float val = default_val;
  mutex_enter_blocking(&data_mutex);
  int idx = findRegistryItem(id);
  if (idx != -1) val = registry[idx].value;
  mutex_exit(&data_mutex);
  return val;
}
void setRegistryValue(const char* id, float val) {
  mutex_enter_blocking(&data_mutex);
  int idx = findRegistryItem(id);
  if (idx != -1) registry[idx].value = val;
  mutex_exit(&data_mutex);
}
void setUpdateInterval(const char* id, uint32_t new_interval_ms) {
  mutex_enter_blocking(&data_mutex);
  int idx = findRegistryItem(id);
  if (idx != -1) { registry[idx].update_interval_ms = new_interval_ms; }
  mutex_exit(&data_mutex);
}

static void handleRoot() {
  char page[] = R"=====(
<!DOCTYPE html><html><head><title>PicoW IoT Controller</title><meta name=viewport content="width=device-width, initial-scale=1"><style>body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;margin:0;padding:20px}h1,h2{color:#03dac6;border-bottom:1px solid #333;padding-bottom:10px}.grid-container{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:20px}.card{background-color:#1e1e1e;border-radius:8px;padding:20px;box-shadow:0 4px 8px #0000004d}h3{margin-top:0;color:#bb86fc}.sensor-value{font-size:2.2em;font-weight:700;color:#03dac6}.control-group,.button-group{margin-top:15px}.control-group label{display:block;margin-bottom:5px;color:#cfcfcf}input[type=range]{width:100%}.toggle-switch{display:flex;align-items:center}.toggle-switch input{opacity:0;width:0;height:0}.slider{position:relative;cursor:pointer;width:40px;height:20px;background-color:#555;border-radius:20px;transition:.4s}.slider:before{position:absolute;content:"";height:16px;width:16px;left:2px;bottom:2px;background-color:#fff;border-radius:50%;transition:.4s}input:checked+.slider{background-color:#03dac6}input:checked+.slider:before{transform:translateX(20px)}button{background-color:#bb86fc;color:#121212;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;font-weight:700;margin-right:10px}button:hover{background-color:#9e66d4}</style></head><body><h1>PicoW IoT Controller</h1><div class=grid-container><div id=status-card class=card><h3>Status</h3><div id=status-content></div></div><div id=sensor-card class=card><h3>Live Sensors</h3><div id=sensor-content></div></div><div id=control-card class=card><h3>Controls</h3><div id=control-content></div></div><div id=settings-card class=card><h3>Settings</h3><div id=settings-content></div></div></div><h2>History</h2><img id=history-graph src=/history.svg width=100%><script>const stateNames={1:"Manual",2:"Auto",3:"Set Max Speed",6:"Debug Pressure"};function sendUpdate(e,t){fetch("/api/update",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({id:e,value:parseFloat(t)})})}document.addEventListener("DOMContentLoaded",()=>{const e={status:document.getElementById("status-content"),sensor:document.getElementById("sensor-content"),control:document.getElementById("control-content"),settings:document.getElementById("settings-content")};fetch("/api/manifest").then(e=>e.json()).then(t=>{t.forEach(t=>{let n,l="";1===t.type?n=e.status:0===t.type?n=e.sensor:t.id.includes("button")||t.id.includes("knob")||t.id.includes("power")?n=e.control:n=e.settings,t.type<=1?l=`<p>${t.name}: <strong class=sensor-value id=${t.id}>--</strong> ${t.unit}</p>`:2===t.type?l=`<div class=control-group><label for=${t.id}>${t.name} (<span id=${t.id}-value>${t.value}</span>)</label><input type=range id=${t.id} min=${t.min_val} max=${t.max_val} step=${t.step} value=${t.value}></div>`:3===t.type?l=`<div class="control-group toggle-switch"><label style=margin-right:10px>${t.name}</label><label class=switch><input type=checkbox id=${t.id} ${t.value>0?"checked":""}><span class=slider></span></label></div>`:4===t.type&&(l='<div class=button-group><label>'+t.name+'</label><br><button id=btn_short>Short</button><button id=btn_long>Long</button><button id=btn_vlong>V. Long</button></div>'),n.innerHTML+=l}),t.forEach(e=>{2===e.type?(document.getElementById(e.id).addEventListener("input",t=>{document.getElementById(`${e.id}-value`).textContent=t.target.value,sendUpdate(e.id,t.target.value)}),sendUpdate(e.id,document.getElementById(e.id).value)):3===e.type&&document.getElementById(e.id).addEventListener("change",t=>sendUpdate(e.id,t.target.checked?1:0))}),document.getElementById("btn_short")&&document.getElementById("btn_short").addEventListener("click",()=>sendUpdate("button_press",1)),document.getElementById("btn_long")&&document.getElementById("btn_long").addEventListener("click",()=>sendUpdate("button_press",2)),document.getElementById("btn_vlong")&&document.getElementById("btn_vlong").addEventListener("click",()=>sendUpdate("button_press",3))}),setInterval(()=>{fetch("/api/data").then(e=>e.json()).then(e=>{for(const t in e){const n=document.getElementById(t);n&&("current_state"===t&&window.stateNames?n.textContent=stateNames[e[t]]||`State ${e[t]}`:n.textContent=parseFloat(e[t]).toFixed(t.includes("pressure")?0:2))}}),document.getElementById("history-graph").src="/history.svg?"+new Date().getTime()},1e3)})</script></body></html>
)=====";
  server.send(200, "text/html", page);
}
static void handleManifest() {
  String json = "[";
  mutex_enter_blocking(&data_mutex);
  for (int i = 0; i < registry_count; i++) {
    json += "{\"id\":\"" + String((char*)registry[i].id) + "\",\"name\":\"" + String((char*)registry[i].name) + "\",\"type\":" + String(registry[i].type) + ",\"value\":" + String(registry[i].value) + ",\"min_val\":" + String(registry[i].min_val) + ",\"max_val\":" + String(registry[i].max_val) + ",\"step\":" + String(registry[i].step) + ",\"unit\":\"" + String((char*)registry[i].unit) + "\"}";
    if (i < registry_count - 1) json += ",";
  }
  mutex_exit(&data_mutex);
  json += "]";
  server.send(200, "application/json", json);
}
static void handleData() {
  String json = "{";
  mutex_enter_blocking(&data_mutex);
  for (int i = 0; i < registry_count; i++) {
    json += "\"" + String((char*)registry[i].id) + "\":" + String(registry[i].value);
    if (i < registry_count - 1) json += ",";
  }
  mutex_exit(&data_mutex);
  json += "}";
  server.send(200, "application/json", json);
}
static void handleUpdate() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Body not received");
    return;
  }
  String body = server.arg("plain");
  int id_start = body.indexOf("\"id\":\"") + 6;
  int id_end = body.indexOf("\"", id_start);
  String id = body.substring(id_start, id_end);
  int val_start = body.indexOf("\"value\":") + 8;
  int val_end = body.indexOf("}", val_start);
  float value = body.substring(val_start, val_end).toFloat();
  setRegistryValue(id.c_str(), value);
  server.send(200, "text/plain", "OK");
}
static void drawSensorHistory() {
  String out;
  out.reserve(8000);
  int w = 1000, h = 400, m = 50;
  out += "<svg xmlns='http://www.w3.org/2000/svg' version='1.1' width='" + String(w) + "' height='" + String(h) + "'><rect width='" + String(w) + "' height='" + String(h) + "' fill='#1e1e1e'/><rect x='" + String(m) + "' y='" + String(m) + "' width='" + String(w - 2 * m) + "' height='" + String(h - 2 * m) + "' fill='#121212' stroke='#444'/>";
  app_draw_graph(out);
  out += "</svg>";
  server.send(200, "image/svg+xml", out);
}
static void handleIdentity() {
  String json_payload;
  app_get_identity(json_payload);
  server.send(200, "application/json", json_payload);
}
void handleCaptiveRoot() {
  String project_name, device_id_prefix;
  app_get_default_identity(project_name, device_id_prefix);

  String html = "";    // Start with an empty string
  html.reserve(1500);  // Reserve memory to prevent fragmentation

  html += R"(<!DOCTYPE html><html><head><title>)";
  html += project_name;
  html += R"( Setup</title><meta name=viewport content="width=device-width, initial-scale=1">)";
  html += R"(<style>body{font-family:sans-serif;background:#f0f0f0;text-align:center}div{background:white;margin:20px auto;padding:20px;border-radius:8px;box-shadow:0 3px 10px #00000026;max-width:400px}input,select{width:90%;padding:10px;margin-top:10px;border:1px solid #ccc;border-radius:4px}button{background:#007bff;color:white;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:95%}h2{color:#333}</style></head><body><div><h2>)";
  html += project_name;
  html += R"( Setup</h2><form action=/save method=POST><p>Connect to your WiFi network:</p><select id=ssid name=ssid>)";

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    html += "<option value='";
    html += WiFi.SSID(i);
    html += "'>";
    html += WiFi.SSID(i);
    html += "</option>";
  }

  html += R"(</select><input type=password name=pass placeholder="WiFi Password" required><hr><p>Set a unique name for this device:</p><input name=devicename placeholder="e.g., Living Room Device" required><button type=submit>Save and Reboot</button></form></div></body></html>)";

  server.send(200, "text/html", html);
}

void handleCaptiveSave() {
  extern String device_name_setting, ssid_setting, pass_setting;
  device_name_setting = server.arg("devicename");
  ssid_setting = server.arg("ssid");
  pass_setting = server.arg("pass");
  app_save_settings();
  String html = R"(<!DOCTYPE html><html><head><title>Setup Complete</title><meta http-equiv=refresh content="5; url=http://1.1.1.1"><style>body{font-family:sans-serif;background:#f0f0f0;text-align:center}div{background:white;margin:20px auto;padding:20px;border-radius:8px;box-shadow:0 3px 10px #00000026;max-width:400px}</style></head><body><div><h2>Settings Saved!</h2><p>The device will now reboot and connect to your WiFi network.</p><p>Please reconnect your computer to your main WiFi network.</p></div></body></html>)";
  server.send(200, "text/html", html);
  delay(1000);
  rp2040.restart();
}
void startConfigMode() {
  in_config_mode = true;
  String project_name, device_id_prefix;
  app_get_default_identity(project_name, device_id_prefix);
  String ap_name = device_id_prefix + "-" + WiFi.macAddress().substring(12);
  ap_name.replace(":", "");
  Serial.println("\nEntering Configuration Mode.");
  Serial.printf("Connect to WiFi network: '%s'\n", ap_name.c_str());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_name.c_str());
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleCaptiveRoot);
  server.on("/save", HTTP_POST, handleCaptiveSave);
  server.onNotFound(handleCaptiveRoot);
  server.begin();
}

static int sensorUpdateTask(struct _task_entry_type* task, int sensor_idx, int) {
  uint32_t next_interval = 0;
  if (sensor_idx >= 0 && sensor_idx < registry_count) {
    mutex_enter_blocking(&data_mutex);
    if (registry[sensor_idx].read_callback != NULL) {
      registry[sensor_idx].read_callback(sensor_idx);
      registry[sensor_idx].last_update_time = millis();
    }
    next_interval = registry[sensor_idx].update_interval_ms;
    mutex_exit(&data_mutex);
    if (next_interval > 0) { AddTaskMilli(task, next_interval, &sensorUpdateTask, sensor_idx, 0); }
  }
  return 0;
}

static void autoScheduleSensors() {
  for (int i = 0; i < registry_count; i++) {
    if (registry[i].type <= TYPE_SENSOR_STATE && registry[i].update_interval_ms > 0 && registry[i].read_callback != NULL) {
      sensor_tasks[i] = CreateTask();
      AddTaskMilli(sensor_tasks[i], 1000 + (i * 200), &sensorUpdateTask, i, 0);
      Serial.printf("Auto-scheduled sensor '%s' at %dms interval\n", registry[i].id, registry[i].update_interval_ms);
    }
  }
}

// ============================================================================
// SECTION 4: ARDUINO ENTRY POINTS
// ============================================================================
void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  bool loaded = app_load_settings();
  extern String ssid_setting, pass_setting;
  if (!loaded || ssid_setting.length() == 0) {
    startConfigMode();
    return;
  }
  Serial.println("\n>>> Core 0: IoT Framework Starting in Normal Mode <<<");
  app_register_items();
  autoScheduleSensors();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_setting.c_str(), pass_setting.c_str());
  int connect_timeout = 20;
  while (WiFi.status() != WL_CONNECTED && connect_timeout-- > 0) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    startConfigMode();
    return;
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  if (MDNS.begin("picow-iot-device")) {
    MDNS.addService("_iot-framework", "_tcp", 80);
    Serial.println("mDNS responder started.");
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/manifest", HTTP_GET, handleManifest);
  server.on("/api/data", HTTP_GET, handleData);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/identity", HTTP_GET, handleIdentity);
  server.on("/history.svg", HTTP_GET, drawSensorHistory);
  app_add_api_endpoints(server);
  server.begin();
}
void loop() {
  if (in_config_mode) { dnsServer.processNextRequest(); }
  server.handleClient();
}
void setup1() {
  if (!in_config_mode) {
    Serial.println("\n>>> Core 1: IoT Framework Starting <<<");
    app_setup();
  }
}
void loop1() {
  if (!in_config_mode) { DoTasks(); }
}

#endif