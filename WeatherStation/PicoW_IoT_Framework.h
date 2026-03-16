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
#include <DNSServer.h>
#include <stdio.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/sync.h>
#include <pico/time.h>
#include <hardware/flash.h>
#include <hardware/sync.h>


// Macros to make app_register_items clean (copied from NonEvent example)
#define SENSOR_AUTO(ID, NAME, INTERVAL_MS, UNIT, CALLBACK) \
  strncpy(def.items[i].id, ID, sizeof(def.items[i].id) - 1); \
  strncpy(def.items[i].name, NAME, sizeof(def.items[i].name) - 1); \
  def.items[i].type = TYPE_SENSOR_GENERIC; \
  def.items[i].value = 0; \
  def.items[i].min_val = 0; \
  def.items[i].max_val = 0; \
  def.items[i].step = 0; \
  strncpy(def.items[i].unit, UNIT, sizeof(def.items[i].unit) - 1); \
  def.items[i].update_interval_ms = INTERVAL_MS; \
  def.items[i].read_callback = CALLBACK; \
  i++;

#define SENSOR_MANUAL(ID, NAME, UNIT) \
  strncpy(def.items[i].id, ID, sizeof(def.items[i].id) - 1); \
  strncpy(def.items[i].name, NAME, sizeof(def.items[i].name) - 1); \
  def.items[i].type = TYPE_SENSOR_GENERIC; \
  def.items[i].value = 0; \
  def.items[i].min_val = 0; \
  def.items[i].max_val = 0; \
  def.items[i].step = 0; \
  strncpy(def.items[i].unit, UNIT, sizeof(def.items[i].unit) - 1); \
  def.items[i].update_interval_ms = 0; \
  def.items[i].read_callback = NULL; \
  i++;

#define CONTROL_SLIDER(ID, NAME, DEFAULT, MIN, MAX, STEP, UNIT) \
  strncpy(def.items[i].id, ID, sizeof(def.items[i].id) - 1); \
  strncpy(def.items[i].name, NAME, sizeof(def.items[i].name) - 1); \
  def.items[i].type = TYPE_CONTROL_SLIDER; \
  def.items[i].value = DEFAULT; \
  def.items[i].min_val = MIN; \
  def.items[i].max_val = MAX; \
  def.items[i].step = STEP; \
  strncpy(def.items[i].unit, UNIT, sizeof(def.items[i].unit) - 1); \
  def.items[i].update_interval_ms = 0; \
  def.items[i].read_callback = NULL; \
  i++;

#define CONTROL_BUTTON(ID, NAME) \
  strncpy(def.items[i].id, ID, sizeof(def.items[i].id) - 1); \
  strncpy(def.items[i].name, NAME, sizeof(def.items[i].name) - 1); \
  def.items[i].type = TYPE_CONTROL_BUTTON; \
  def.items[i].value = 0; \
  def.items[i].min_val = 0; \
  def.items[i].max_val = 1; \
  def.items[i].step = 1; \
  def.items[i].unit[0] = '\0'; \
  def.items[i].update_interval_ms = 0; \
  def.items[i].read_callback = NULL; \
  i++;

#define MAX_REGISTRY_ITEMS 64
#define HISTORY_SIZE 180

enum ItemType { TYPE_SENSOR_GENERIC,
                TYPE_SENSOR_STATE,
                TYPE_CONTROL_SLIDER,
                TYPE_CONTROL_TOGGLE,
                TYPE_CONTROL_BUTTON };

struct RegistryItem {
  char id[20];
  char name[32];
  ItemType type;
  float value;
  float min_val;
  float max_val;
  float step;
  char unit[8];
  uint32_t update_interval_ms;
  int (*read_callback)(struct _task_entry_type*, int, int);
  unsigned long last_update_time;
};

struct RegistryDef {
  RegistryItem items[MAX_REGISTRY_ITEMS];
  int count;
};

RegistryDef app_register_items();  // forward declaration — implemented in WeatherStation.ino

class Registry {
private:
  RegistryItem items0[MAX_REGISTRY_ITEMS];
  RegistryItem items1[MAX_REGISTRY_ITEMS];
  uint32_t dirty0[(MAX_REGISTRY_ITEMS + 31) / 32] = { 0 };
  uint32_t dirty1[(MAX_REGISTRY_ITEMS + 31) / 32] = { 0 };
  int send_cursor0 = 0;
  int send_cursor1 = 0;
  int count = 0;

  RegistryItem* items() {
    return get_core_num() == 0 ? items0 : items1;
  }
  uint32_t* dirty() {
    return get_core_num() == 0 ? dirty0 : dirty1;
  }
  int& sendCursor() {
    return get_core_num() == 0 ? send_cursor0 : send_cursor1;
  }
  void setDirty(uint8_t id) {
    dirty()[id / 32] |= (1u << (id % 32));
  }
  void clearDirty(uint8_t id) {
    dirty()[id / 32] &= ~(1u << (id % 32));
  }
  bool isDirty(uint8_t id) {
    return (dirty()[id / 32] >> (id % 32)) & 1u;
  }

public:

  void begin() {
    RegistryDef def = app_register_items();
    memcpy(items0, def.items, sizeof(RegistryItem) * def.count);
    count = def.count;
    Serial.printf(">> Registry Begin: Item Count: %d\n", count);
    deepCopy();
  }

  int getCount() {
    return count;
  }

  // -----------------------------------------------------------------------
  // NAME LOOKUP
  // -----------------------------------------------------------------------

  uint8_t nameToIdx(const char* id_str) {
    for (int i = 0; i < count; i++)
      if (strcmp(items()[i].id, id_str) == 0) return (uint8_t)i;
    Serial.printf(">> Registry ERROR: nameToIdx() could not find id '%s'\n", id_str);
    return 255;
  }

  const char* idxToName(uint8_t id) {
    if (id < count) return items()[id].id;
    Serial.printf(">> Registry ERROR: idxToName() index %d out of range\n", id);
    return nullptr;
  }

  // -----------------------------------------------------------------------
  // CORE IMPLEMENTATION — keyed by numeric index
  // -----------------------------------------------------------------------

  RegistryItem* getItem_id(uint8_t id) {
    if (id < count) return &items()[id];
    Serial.printf(">> Registry ERROR: getItem_id() index %d out of range\n", id);
    return nullptr;
  }

  void set_id(uint8_t id, float val) {
    if (id >= count) {
      Serial.printf(">> Registry ERROR: set_id() index %d out of range\n", id);
      return;
    }
    setDirty(id);
    items()[id].value = val;
  }

  float get_id(uint8_t id, float default_val = 0.0f) {
    if (id < count) return items()[id].value;
    Serial.printf(">> Registry ERROR: get_id() index %d out of range\n", id);
    return default_val;
  }

  void update_id(uint8_t id, float val) {
    if (id < count) items()[id].value = val;
  }

  // -----------------------------------------------------------------------
  // NAME-BASED WRAPPERS
  // -----------------------------------------------------------------------

  RegistryItem* getItem(const char* id_str) {
    uint8_t i = nameToIdx(id_str);
    if (i == 255) return nullptr;
    return getItem_id(i);
  }

  // legacy numeric overload — kept for autoScheduleSensors() loop
  RegistryItem* getItem(int i) {
    return getItem_id((uint8_t)i);
  }

  void set(const char* id_str, float val) {
    uint8_t i = nameToIdx(id_str);
    if (i == 255) return;
    set_id(i, val);
  }

  float get(const char* id_str, float default_val = 0.0f) {
    uint8_t i = nameToIdx(id_str);
    if (i == 255) return default_val;
    return get_id(i, default_val);
  }

  // -----------------------------------------------------------------------
  // INTER-CORE SYNC
  // -----------------------------------------------------------------------

  void deepCopy() {
    memcpy(items1, items0, sizeof(RegistryItem) * count);
    memset(dirty1, 0, sizeof(dirty1));
    send_cursor1 = 0;
  }

  void sendDirty() {
    int checked = 0;
    while (checked < count) {
      int i = sendCursor();
      sendCursor() = (sendCursor() + 1) % count;
      checked++;
      if (!isDirty(i)) continue;
      uint8_t msg[MSG_TOTAL_BYTES];
      msg_set_type(msg, MSG_VALUE_SYNC);
      msg_set_id(msg, (uint8_t)i);
      float_to_msg(items()[i].value, msg);
      if (!fifo_send(msg)) return;
      Serial.printf(">> FIFO PUSH [Core %d]: Item %d (%s) = %.2f\n", get_core_num(), i, items()[i].id, items()[i].value);
      clearDirty(i);
    }
  }

  void recvUpdates() {
    for (int i = 0; i < 4; i++) {
      uint8_t msg[MSG_TOTAL_BYTES];
      if (!fifo_recv(msg)) return;
      if (msg_get_type(msg) == MSG_NONE) return;
      Serial.printf("<< FIFO POP  [Core %d]: Item %d = %.2f\n", get_core_num(), msg_get_id(msg), msg_to_float(msg));
      update_id(msg_get_id(msg), msg_to_float(msg));
    }
  }
};

Registry registry;

// drop-in replacements for old getRegistryValue/setRegistryValue
//float getRegistryValue(const char* id, float default_val = 0.0f) {
//  return activeRegistry().get(id, default_val);
//}
//void setRegistryValue(const char* id, float val) {
//  activeRegistry().set(id, val);
//}



//auto _ = (mutex_init(&data_mutex), 0);
volatile bool framework_ready = false;

// ============================================================================
// SECTION 2: APPLICATION CALLBACKS (to be implemented in your .ino file)
// ============================================================================
void app_setup();
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

static void handleRoot() {
  Serial.println(">> Starting handleRoot (Streaming)");

  // 1. Tell the server we are sending an unknown length in chunks
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", "");

  // CHUNK 1: Header and CSS
  server.sendContent(R"=====(
<!DOCTYPE html><html><head><title>PicoW IoT Controller</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;margin:0;padding:20px}
  h1{color:#03dac6;border-bottom:1px solid #333;padding-bottom:10px}
  .grid-container{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:20px}
  .card{background-color:#1e1e1e;border-radius:8px;padding:20px;box-shadow:0 4px 8px #0000004d}
  h3{margin-top:0;color:#bb86fc}
  .sensor-value{font-size:2.2em;font-weight:700;color:#03dac6}
  .control-group,.button-group{margin-top:15px}
  .control-group label{display:block;margin-bottom:5px;color:#cfcfcf}
  input[type=range]{width:100%}.toggle-switch{display:flex;align-items:center}
  .toggle-switch input{opacity:0;width:0;height:0}
  .slider{position:relative;cursor:pointer;width:40px;height:20px;background-color:#555;border-radius:20px;transition:.4s}
  .slider:before{position:absolute;content:"";height:16px;width:16px;left:2px;bottom:2px;background-color:#fff;border-radius:50%;transition:.4s}
  input:checked+.slider{background-color:#03dac6}input:checked+.slider:before{transform:translateX(20px)}
  button{background-color:#bb86fc;color:#121212;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;font-weight:700;margin-right:10px}
  button:hover{background-color:#9e66d4}
</style></head>
)=====");

  // CHUNK 2: HTML Body structure
  server.sendContent(R"=====(
<body><h1>PicoW IoT Controller</h1>
<div class="grid-container">
  <div id="status-card" class="card"><h3>Status</h3><div id="status-content"></div></div>
  <div id="sensor-card" class="card"><h3>Live Sensors</h3><div id="sensor-content"></div></div>
  <div id="control-card" class="card"><h3>Controls</h3><div id="control-content"></div></div>
  <div id="settings-card" class="card"><h3>Settings</h3><div id="settings-content"></div></div>
</div>
)=====");

  // CHUNK 3: Javascript Logic
  server.sendContent(R"=====(
<script>
function sendUpdate(id, val) {
  fetch("/api/update", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({id:id, value:parseFloat(val)})});
}
function refreshData() {
  fetch("/api/data?t=" + new Date().getTime()).then(r => r.json()).then(data => {
    for (const t in data) {
      const n = document.getElementById(t);
      if (n) n.textContent = parseFloat(data[t]).toFixed(t.includes("pressure") ? 0 : 2);
    }
  });
}
document.addEventListener("DOMContentLoaded", () => {
  const cards = { status: document.getElementById("status-content"), sensor: document.getElementById("sensor-content"), control: document.getElementById("control-content"), settings: document.getElementById("settings-content") };
  fetch("/api/manifest?t=" + new Date().getTime()).then(r => r.json()).then(items => {
    items.forEach(item => {
      let card, html = "";
      if (item.type === 1) card = cards.status;
      else if (item.type === 0) card = cards.sensor;
      else card = (item.type >= 2) ? cards.control : cards.settings;
      if (item.type <= 1) html = `<p>${item.name}: <strong class="sensor-value" id="${item.id}">--</strong> ${item.unit}</p>`;
      else if (item.type === 2) html = `<div class="control-group"><label>${item.name} (<span id="${item.id}-value">${item.value}</span>)</label><input type="range" id="${item.id}" min="${item.min_val}" max="${item.max_val}" step="${item.step}" value="${item.value}"></div>`;
      else if (item.type === 3) html = `<div class="control-group toggle-switch"><label style="margin-right:10px">${item.name}</label><label class="switch"><input type="checkbox" id="${item.id}" ${item.value > 0 ? "checked" : ""}><span class="slider"></span></label></div>`;
      else if (item.type === 4) html = `<div class="button-group"><label>${item.name}</label><br><button id="btn_${item.id}">Trigger</button></div>`;
      if (card && html) card.innerHTML += html;
    });
    items.forEach(item => {
      const el = document.getElementById(item.id);
      if (item.type === 2 && el) el.addEventListener("input", e => { document.getElementById(item.id + "-value").textContent = e.target.value; sendUpdate(item.id, e.target.value); });
      else if (item.type === 3 && el) el.addEventListener("change", e => sendUpdate(item.id, e.target.checked ? 1 : 0));
      else if (item.type === 4) { const btn = document.getElementById("btn_" + item.id); if (btn) btn.addEventListener("click", () => sendUpdate(item.id, 1)); }
    });
    refreshData();
    setInterval(refreshData, 5000);
  });
});
</script></body></html>
)=====");

  // 4. Send final empty chunk to finish the request
  server.sendContent("");
}

static void handleManifest() {     //debug verrsion
  Serial.printf(">> Manifest Request. Items: %d\n", registry.getCount());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", ""); 

  server.sendContent("[");
  for (int i = 0; i < registry.getCount(); i++) {
    RegistryItem* r = registry.getItem(i);
    
    String item = "{\"id\":\"" + String(r->id) + 
                  "\",\"name\":\"" + String(r->name) + 
                  "\",\"type\":" + String(r->type) + 
                  ",\"value\":" + String(r->value) + 
                  ",\"min_val\":" + String(r->min_val) + 
                  ",\"max_val\":" + String(r->max_val) + 
                  ",\"step\":" + String(r->step) + 
                  ",\"unit\":\"" + String(r->unit) + "\"}";
    
    Serial.println("CHUNK: " + item); // DEBUG PRINT
    server.sendContent(item);
    if (i < registry.getCount() - 1) server.sendContent(",");
  }
  server.sendContent("]");
  server.sendContent(""); 
}

static void handleManifest_orig() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  server.sendContent("[");
  for (int i = 0; i < registry.getCount(); i++) {
    RegistryItem* r = registry.getItem(i);

    String item = "{\"id\":\"" + String(r->id) + "\",\"name\":\"" + String(r->name) + "\",\"type\":" + String(r->type) + ",\"value\":" + String(r->value) + ",\"min_val\":" + String(r->min_val) + ",\"max_val\":" + String(r->max_val) + ",\"step\":" + String(r->step) + ",\"unit\":\"" + String(r->unit) + "\"}";

    server.sendContent(item);
    if (i < registry.getCount() - 1) server.sendContent(",");
  }
  server.sendContent("]");
  server.sendContent("");  // Final empty chunk to signal end of stream
}

static void handleUpdate() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No body");
    return;
  }

  String body = server.arg("plain");

  // 1. Extract the ID string
  int id_start = body.indexOf("\"id\":\"") + 6;
  int id_end = body.indexOf("\"", id_start);
  if (id_start < 6 || id_end == -1) {
    server.send(400, "text/plain", "Bad ID");
    return;
  }
  String id = body.substring(id_start, id_end);

  // 2. Extract the numeric value
  int val_start = body.indexOf("\"value\":") + 8;
  int val_end = body.indexOf("}", val_start);
  if (val_start < 8) {
    server.send(400, "text/plain", "Bad Val");
    return;
  }
  float value = body.substring(val_start, val_end).toFloat();

  // 3. Update the registry (this automatically sets the 'dirty' flag for Core 1)
  registry.set(id.c_str(), value);

  // 4. Send a tiny response
  server.send(200, "text/plain", "OK");
}

static void handleData() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  server.sendContent("{");
  for (int i = 0; i < registry.getCount(); i++) {
    RegistryItem* r = registry.getItem(i);
    String entry = "\"" + String(r->id) + "\":" + String(r->value);
    server.sendContent(entry);
    if (i < registry.getCount() - 1) server.sendContent(",");
  }
  server.sendContent("}");
  server.sendContent("");
}

static void handleIdentity() {
  Serial.println(">> Starting handleIdentity");
  String json_payload;
  app_get_identity(json_payload);
  server.send(200, "application/json", json_payload);
}

void handleCaptiveRoot() {
  Serial.println(">> Starting handleCaptiveRoot");
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
  Serial.println(">> Starting handleCaptiveSave");
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
  Serial.println(">> Starting startConfigMode");
  in_config_mode = true;
  String project_name, device_id_prefix;
  app_get_default_identity(project_name, device_id_prefix);
  String ap_name = device_id_prefix + "-" + WiFi.macAddress().substring(12);
  ap_name.replace(":", "");
  Serial.println("\nEntering Configuration Mode.");
  Serial.printf("Connect to WiFi network: '%s'\n", ap_name.c_str());
  WiFi.mode(WIFI_AP);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  bool ap_result = WiFi.softAP(ap_name.c_str());
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("softAP result: %d\n", ap_result);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  server.on("/", HTTP_GET, handleCaptiveRoot);
  server.on("/save", HTTP_POST, handleCaptiveSave);
  server.onNotFound(handleCaptiveRoot);
  server.begin();
}

static void autoScheduleSensors() {
    Serial.printf(">> autoScheduleSensors() count=%d\n", registry.getCount());
    for (int i = 0; i < registry.getCount(); i++) {
        RegistryItem* item = registry.getItem_id(i);
        if (!item) continue;
        Serial.printf(">> item[%d] id='%s' type=%d interval=%lu callback=%s\n", 
            i, item->id, item->type, item->update_interval_ms, 
            item->read_callback ? "SET" : "NULL");
        if (item->type <= TYPE_SENSOR_STATE && item->update_interval_ms > 0 && item->read_callback != NULL) {
            uint32_t delay = item->update_interval_ms + 10000 + i * 1000;
            Serial.printf(">> scheduling item[%d] '%s' with delay=%lu mesgid=%d\n", i, item->id, delay, i);
            AddTaskMilli(CreateTask(), delay, item->read_callback, i, 0);
        }
    }
}

// ============================================================================
// SECTION 4: ARDUINO ENTRY POINTS
// ============================================================================
void setup() {
  //mutex_init(&data_mutex);
  //Serial.begin(115200);
  //while (!Serial) delay(10);
  Serial.begin(115200);
  while (!Serial) {
    yield();
  }
  //delay(5000);

  Serial.println(">> Starting Setup 0");
  bool loaded = app_load_settings();
  extern String ssid_setting, pass_setting;
  if (!loaded || ssid_setting.length() == 0) {
    startConfigMode();
    rp2040.restart();
  }
  Serial.println("\n>>> Core 0: IoT Framework Attempting to Start in Normal Mode <<<");
  registry.begin(); 
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("PicoW2");
  WiFi.begin(ssid_setting.c_str(), pass_setting.c_str());
  int connect_timeout = 60;
  while (WiFi.status() != WL_CONNECTED && connect_timeout-- > 0) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    startConfigMode();
    rp2040.restart();
  }
  framework_ready = true;

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
  //server.on("/history.svg", HTTP_GET, drawSensorHistory);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  //app_add_api_endpoints(server);
  server.begin();
}
void loop() {
  if (in_config_mode) { dnsServer.processNextRequest(); }
  server.handleClient();
  registry.recvUpdates();
  registry.sendDirty();
  best_effort_wfe_or_timeout(make_timeout_time_ms(101));
}
void setup1() {
  randomSeed(1000);
  while (!framework_ready) delay(10);
  app_setup();
  autoScheduleSensors();
}

void loop1() {
  DoTasks();
  registry.recvUpdates();
  registry.sendDirty();
  delay(10);
}

#endif