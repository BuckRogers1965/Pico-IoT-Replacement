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

// Forward declaration — WebServer is defined in Section 3 but the renderer
// functions below need to call server.sendContent() before that point.
extern WebServer server;

// ============================================================================
// LAYOUT TABLE  (the View Definition — completely separate from the Registry)
//
// layout_table[] and layout_count are defined in the .ino file.
// Widgets reference registry items by string id.
// The framework resolves those strings to numeric indices once at boot
// in setupLayoutResolution(), then the renderer uses only numeric indices.
// ============================================================================

enum WidgetType {
  W_ROOT,        // invisible tree root
  W_PAGE,        // top-level page — becomes a nav tab and a URL
  W_CARD,        // plain card, children stacked vertically
  W_COLLAPSIBLE, // card with a show/hide toggle header
  W_TABBED,      // child cards rendered as tabs
  W_RADIO,       // child buttons are mutually exclusive
  W_TEXT,        // plain numeric text readout
  W_BAR,         // horizontal progress bar
  W_DIAL,        // SVG arc gauge
  W_LED,         // binary on/off indicator
  W_SLIDER,      // range slider control
  W_BUTTON,      // momentary trigger button
  W_HELP,        // ⓘ icon with hover tooltip containing static HTML
  W_HTML         // inline static HTML streamed directly into the container
};

// Static node — declared by the developer in the .ino, lives in Flash
struct LayoutNode {
  const char* id;           // unique id for this UI node
  const char* parent_id;    // id of parent node ("" for children of root)
  const char* name;         // display name shown in the UI
  const char* registry_id;  // registry item this widget shows ("" for containers)
  WidgetType  widget;
  const char* props;        // key:value pairs e.g. "min:0,max:100" ("" for none)
};

// Runtime resolved node — parallel array, built once at boot, lives in RAM
struct ResolvedNode {
  char       id[20];
  char       parent_id[20];
  char       name[32];
  uint8_t    registry_idx;   // 255 = not mapped (containers have no registry item)
  WidgetType widget;
  bool       is_container;
  float      prop_min;
  float      prop_max;
  bool       has_min;
  bool       has_max;
  bool       momentary;
  int        prop_width;   // card width in px, 0 = auto
};

#define MAX_LAYOUT_NODES 64
ResolvedNode resolved_table[MAX_LAYOUT_NODES];
int resolved_count = 0;

extern const LayoutNode layout_table[];
extern const int layout_count;

// Help table — static HTML strings stored in Flash, streamed on demand
// Declared in the .ino file alongside the layout table
struct HelpNode {
  const char* id;        // unique id, referenced by node id in layout_table
  const char* html;      // HTML content shown in the hover tooltip
};

extern const HelpNode help_table[];
extern const int help_count;

// Parse a single "key:value" token into the resolved node
static void parsePropToken(ResolvedNode& node, const char* key, const char* val) {
  if (strcmp(key, "min")       == 0) { node.prop_min  = atof(val); node.has_min  = true; }
  if (strcmp(key, "max")       == 0) { node.prop_max  = atof(val); node.has_max  = true; }
  if (strcmp(key, "momentary") == 0 && strcmp(val, "true") == 0) node.momentary = true;
  if (strcmp(key, "width")     == 0) { node.prop_width = atoi(val); }
}

// Parse the full props string "min:0,max:100" into a resolved node
static void parseProps(ResolvedNode& node, const char* props_str) {
  node.prop_min  = 0.0f;  node.prop_max  = 100.0f;
  node.has_min   = false; node.has_max   = false;
  node.momentary = false; node.prop_width = 0;
  if (!props_str || !props_str[0]) return;
  char buf[64];
  strncpy(buf, props_str, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
  char* tok = strtok(buf, ",");
  while (tok) {
    char* colon = strchr(tok, ':');
    if (colon) { *colon = '\0'; parsePropToken(node, tok, colon + 1); }
    tok = strtok(nullptr, ",");
  }
}

// Called once from setup() after registry.begin().
// Resolves every registry_id string to a numeric registry index.
// All string work happens here — renderer uses only numeric indices at runtime.
static void setupLayoutResolution() {
  Serial.println(">> [Layout] Boot resolution starting...");
  Serial.printf(">> [Layout] Total nodes to resolve: %d\n", layout_count);

  resolved_count = layout_count;

  for (int i = 0; i < layout_count; i++) {
    strncpy(resolved_table[i].id,        layout_table[i].id,        20);
    strncpy(resolved_table[i].parent_id, layout_table[i].parent_id, 20);
    strncpy(resolved_table[i].name,      layout_table[i].name,      32);
    resolved_table[i].widget       = layout_table[i].widget;
    resolved_table[i].is_container = (layout_table[i].widget <= W_RADIO);
    resolved_table[i].registry_idx = 255;

    parseProps(resolved_table[i], layout_table[i].props);

    Serial.printf(">> [Layout] node[%d] id='%s' parent='%s' name='%s' widget=%d container=%s\n",
      i,
      layout_table[i].id,
      layout_table[i].parent_id[0] ? layout_table[i].parent_id : "(root)",
      layout_table[i].name,
      (int)layout_table[i].widget,
      resolved_table[i].is_container ? "YES" : "NO");

    if (layout_table[i].props[0]) {
      Serial.printf(">> [Layout]   props='%s' -> min=%.2f has_min=%d max=%.2f has_max=%d momentary=%d\n",
        layout_table[i].props,
        resolved_table[i].prop_min, resolved_table[i].has_min,
        resolved_table[i].prop_max, resolved_table[i].has_max,
        resolved_table[i].momentary);
    }

    if (layout_table[i].registry_id[0] != '\0') {
      uint8_t idx = registry.nameToIdx(layout_table[i].registry_id);
      if (idx == 255) {
        Serial.printf(">> [Layout ERROR] node '%s' -> registry id '%s' NOT FOUND\n",
          layout_table[i].id, layout_table[i].registry_id);
      } else {
        resolved_table[i].registry_idx = idx;
        RegistryItem* r = registry.getItem_id(idx);
        Serial.printf(">> [Layout OK]    node '%s' -> registry[%d] id='%s' name='%s'\n",
          layout_table[i].id, idx, r->id, r->name);
      }
    } else {
      Serial.printf(">> [Layout]   node '%s' is a container — no registry mapping\n",
        layout_table[i].id);
    }
  }

  Serial.printf(">> [Layout] Boot resolution complete. %d nodes resolved.\n", resolved_count);
}

// Find a resolved node index by id string
static int findResolvedNode(const char* id) {
  for (int i = 0; i < resolved_count; i++)
    if (strcmp(resolved_table[i].id, id) == 0) return i;
  return -1;
}

// Walk parent chain to check if node_i is a descendant of page_id
static bool nodeIsDescendantOf(int node_i, const char* ancestor_id) {
  char cur[20];
  strncpy(cur, resolved_table[node_i].parent_id, 20);
  int safety = 0;
  while (cur[0] != '\0' && safety++ < MAX_LAYOUT_NODES) {
    if (strcmp(cur, ancestor_id) == 0) return true;
    int p = findResolvedNode(cur);
    if (p < 0) return false;
    strncpy(cur, resolved_table[p].parent_id, 20);
  }
  return false;
}

// ---- Leaf widget renderers ----

static void renderWidget_Text(const ResolvedNode& node, int node_idx) {
  uint8_t idx = node.registry_idx;
  if (idx == 255) return;
  RegistryItem* r = registry.getItem_id(idx);
  if (!r) return;
  Serial.printf(">> [Render] W_TEXT   node='%s' registry='%s'\n", node.id, r->id);

  // Check if the immediately following sibling in the resolved table is a W_HELP node
  // with the same parent — if so, render the ⓘ inline on the same line
  String help_html = "";
  if (node_idx + 1 < resolved_count) {
    ResolvedNode& next = resolved_table[node_idx + 1];
    if (next.widget == W_HELP && strcmp(next.parent_id, node.parent_id) == 0) {
      for (int i = 0; i < help_count; i++) {
        if (strcmp(help_table[i].id, next.id) == 0) {
          Serial.printf(">> [Render] W_TEXT   found inline help for '%s'\n", next.id);
          help_html = "<span class=\"help-wrap\"><span class=\"help-icon\">&#9432;</span>"
                      "<span class=\"help-tooltip\">" + String(help_table[i].html) + "</span></span>";
          break;
        }
      }
    }
  }

  server.sendContent(
    "<p>" + String(r->name) + ": "
    "<strong class=\"sensor-value\" id=\"" + String(r->id) + "\">--</strong>"
    " " + String(r->unit) + help_html + "</p>"
  );
}

static void renderWidget_Bar(const ResolvedNode& node) {
  uint8_t idx = node.registry_idx;
  if (idx == 255) return;
  RegistryItem* r = registry.getItem_id(idx);
  if (!r) return;
  float mn = node.has_min ? node.prop_min : 0.0f;
  float mx = node.has_max ? node.prop_max : 100.0f;
  Serial.printf(">> [Render] W_BAR    node='%s' registry='%s' min=%.1f max=%.1f\n", node.id, r->id, mn, mx);
  String rid = String(r->id);
  server.sendContent(
    "<div class=\"bar-row\">"
    "<span class=\"bar-label\">" + String(r->name) + "</span>"
    "<div class=\"bar-track\"><div class=\"bar-fill\" id=\"bar_" + rid + "\" style=\"width:0%\"></div></div>"
    "<span class=\"sensor-value\" id=\"" + rid + "\">--</span>"
    "<span class=\"bar-unit\"> " + String(r->unit) + "</span>"
    "<span id=\"barmeta_" + rid + "\" data-min=\"" + String(mn,2) + "\" data-max=\"" + String(mx,2) + "\" style=\"display:none\"></span>"
    "</div>"
  );
}

static void renderWidget_Dial(const ResolvedNode& node) {
  uint8_t idx = node.registry_idx;
  if (idx == 255) return;
  RegistryItem* r = registry.getItem_id(idx);
  if (!r) return;
  float mn = node.has_min ? node.prop_min : 0.0f;
  float mx = node.has_max ? node.prop_max : 100.0f;
  Serial.printf(">> [Render] W_DIAL   node='%s' registry='%s' min=%.1f max=%.1f\n", node.id, r->id, mn, mx);
  String rid = String(r->id);
  server.sendContent(
    "<div class=\"dial-row\">"
    "<span class=\"bar-label\">" + String(r->name) + "</span>"
    "<div class=\"dial-wrap\">"
    "<svg viewBox=\"0 0 100 60\" class=\"dial-svg\">"
    "<path d=\"M10,55 A40,40 0 1,1 90,55\" fill=\"none\" stroke=\"#333\" stroke-width=\"8\" stroke-linecap=\"round\"/>"
    "<path d=\"M10,55 A40,40 0 1,1 90,55\" fill=\"none\" stroke=\"#03dac6\" stroke-width=\"8\" stroke-linecap=\"round\""
    " pathLength=\"1\" stroke-dasharray=\"1\" stroke-dashoffset=\"1\" id=\"dial_" + rid + "\"/>"
    "<text x=\"50\" y=\"52\" text-anchor=\"middle\" class=\"dial-text\" id=\"" + rid + "\">--</text>"
    "</svg>"
    "<span class=\"bar-unit\"> " + String(r->unit) + "</span>"
    "</div>"
    "<span id=\"dialmeta_" + rid + "\" data-min=\"" + String(mn,2) + "\" data-max=\"" + String(mx,2) + "\" style=\"display:none\"></span>"
    "</div>"
  );
}

static void renderWidget_Slider(const ResolvedNode& node) {
  uint8_t idx = node.registry_idx;
  if (idx == 255) return;
  RegistryItem* r = registry.getItem_id(idx);
  if (!r) return;
  Serial.printf(">> [Render] W_SLIDER node='%s' registry='%s'\n", node.id, r->id);
  server.sendContent(
    "<div class=\"control-group\">"
    "<label>" + String(r->name) + " (<span id=\"" + String(r->id) + "-value\">" + String(r->value, 1) + "</span> " + String(r->unit) + ")</label>"
    "<input type=\"range\" id=\"" + String(r->id) + "\""
    " min=\"" + String(r->min_val, 1) + "\" max=\"" + String(r->max_val, 1) + "\""
    " step=\"" + String(r->step, 1) + "\" value=\"" + String(r->value, 1) + "\">"
    "</div>"
  );
}

static void renderWidget_Button(const ResolvedNode& node) {
  uint8_t idx = node.registry_idx;
  if (idx == 255) return;
  RegistryItem* r = registry.getItem_id(idx);
  if (!r) return;
  Serial.printf(">> [Render] W_BUTTON node='%s' registry='%s'\n", node.id, r->id);
  server.sendContent(
    "<div class=\"button-group\">"
    "<button id=\"btn_" + String(r->id) + "\">" + String(r->name) + "</button>"
    "</div>"
  );
}

static void renderWidget_Help(const ResolvedNode& node, int node_idx) {
  // If the previous sibling was W_TEXT with the same parent, the icon was already
  // rendered inline by renderWidget_Text — nothing to do here
  if (node_idx > 0) {
    ResolvedNode& prev = resolved_table[node_idx - 1];
    if (prev.widget == W_TEXT && strcmp(prev.parent_id, node.parent_id) == 0) {
      Serial.printf(">> [Render] W_HELP   node='%s' already rendered inline by W_TEXT sibling\n", node.id);
      return;
    }
  }
  // Standalone W_HELP — render the icon as its own block
  Serial.printf(">> [Render] W_HELP   node='%s' standalone\n", node.id);
  for (int i = 0; i < help_count; i++) {
    if (strcmp(help_table[i].id, node.id) == 0) {
      server.sendContent(
        "<div class=\"help-wrap\"><span class=\"help-icon\">&#9432;</span>"
        "<span class=\"help-tooltip\">" + String(help_table[i].html) + "</span></div>"
      );
      return;
    }
  }
  Serial.printf(">> [Render] W_HELP   WARNING: no help content found for id '%s'\n", node.id);
}

static void renderWidget_Html(const ResolvedNode& node) {
  // node.id matches a help_table entry — streams its html directly inline
  Serial.printf(">> [Render] W_HTML   node='%s'\n", node.id);
  for (int i = 0; i < help_count; i++) {
    if (strcmp(help_table[i].id, node.id) == 0) {
      Serial.printf(">> [Render] W_HTML   found content for '%s'\n", node.id);
      server.sendContent("<span class=\"inline-html\">" + String(help_table[i].html) + "</span>");
      return;
    }
  }
  Serial.printf(">> [Render] W_HTML   WARNING: no content found for id '%s'\n", node.id);
}

// Recursive container renderer — walks resolved_table depth-first
static void renderContainer(const char* parent_id);

static void renderContainer(const char* parent_id) {
  Serial.printf(">> [Render] renderContainer(parent='%s')\n", parent_id);
  int children_found = 0;

  for (int i = 0; i < resolved_count; i++) {
    ResolvedNode& node = resolved_table[i];
    if (strcmp(node.parent_id, parent_id) != 0) continue;
    children_found++;

    if (node.is_container) {
      Serial.printf(">> [Render]   container '%s' widget=%d\n", node.id, (int)node.widget);
      switch (node.widget) {
        case W_PAGE:
          // Pages are top-level — they are not rendered inside another container
          Serial.printf(">> [Render]   skipping W_PAGE '%s' inside renderContainer\n", node.id);
          break;
        case W_CARD: {
          Serial.printf(">> [Render]   W_CARD '%s' name='%s' width=%d\n", node.id, node.name, node.prop_width);
          String style = node.prop_width > 0 ? " style=\"width:" + String(node.prop_width) + "px\"" : "";
          server.sendContent("<div class=\"card\"" + style + "><h3>" + String(node.name) + "</h3>");
          renderContainer(node.id);
          server.sendContent("</div>");
          break;
        }
        case W_COLLAPSIBLE: {
          Serial.printf(">> [Render]   W_COLLAPSIBLE '%s' name='%s'\n", node.id, node.name);
          String uid = "col_" + String(node.id);
          server.sendContent(
            "<div class=\"card\">"
            "<div class=\"col-header\" onclick=\"toggleCol('" + uid + "')\">"
            "<h3>" + String(node.name) + "</h3><span>&#9660;</span></div>"
            "<div class=\"col-body\" id=\"" + uid + "\">"
          );
          renderContainer(node.id);
          server.sendContent("</div></div>");
          break;
        }
        case W_RADIO:
          Serial.printf(">> [Render]   W_RADIO '%s' name='%s'\n", node.id, node.name);
          server.sendContent(
            "<div class=\"card\"><h3>" + String(node.name) + "</h3>"
            "<div class=\"radio-group\" id=\"rg_" + String(node.id) + "\">"
          );
          renderContainer(node.id);
          server.sendContent("</div></div>");
          break;
        default:
          renderContainer(node.id);
          break;
      }
    } else {
      switch (node.widget) {
        case W_TEXT:   renderWidget_Text(node, i);   break;
        case W_BAR:    renderWidget_Bar(node);        break;
        case W_DIAL:   renderWidget_Dial(node);       break;
        case W_SLIDER: renderWidget_Slider(node);     break;
        case W_BUTTON: renderWidget_Button(node);     break;
        case W_HELP:   renderWidget_Help(node, i);    break;
        case W_HTML:   renderWidget_Html(node);       break;
        default:
          Serial.printf(">> [Render] WARNING unknown widget type %d for node '%s'\n",
            (int)node.widget, node.id);
          break;
      }
    }
  }

  if (children_found == 0) {
    Serial.printf(">> [Render] WARNING: no children found for parent='%s'\n", parent_id);
  }
}

// Build the comma-separated registry index list for a page — baked into the JS at serve time
static String buildPageIndices(const char* page_id) {
  String indices = "";
  bool first = true;
  for (int i = 0; i < resolved_count; i++) {
    if (resolved_table[i].is_container) continue;
    if (resolved_table[i].registry_idx == 255) continue;
    if (!nodeIsDescendantOf(i, page_id)) continue;
    if (!first) indices += ",";
    indices += String(resolved_table[i].registry_idx);
    first = false;
  }
  return indices;
}

// Serve a complete HTML page for the given page node id
static void handlePage(const char* page_id) {
  Serial.printf(">> handlePage('%s')\n", page_id);

  // Scan what widget types this page contains — drives conditional CSS chunks
  bool needs_bar    = false;
  bool needs_dial   = false;
  bool needs_collapsible = false;
  bool needs_radio  = false;

  for (int i = 0; i < resolved_count; i++) {
    if (!nodeIsDescendantOf(i, page_id)) continue;
    switch (resolved_table[i].widget) {
      case W_BAR:         needs_bar         = true; break;
      case W_DIAL:        needs_dial        = true; break;
      case W_COLLAPSIBLE: needs_collapsible = true; break;
      case W_RADIO:       needs_radio       = true; break;
      default: break;
    }
  }

  Serial.printf(">> handlePage('%s') needs: bar=%d dial=%d collapsible=%d radio=%d\n",
    page_id, needs_bar, needs_dial, needs_collapsible, needs_radio);

  // Find page display name
  String page_name = String(page_id);
  int pn = findResolvedNode(page_id);
  if (pn >= 0 && resolved_table[pn].name[0]) page_name = String(resolved_table[pn].name);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", "");

  // CHUNK 1: Header and base CSS (mirrors original styling exactly)
  server.sendContent(
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">\n"
    "<title>" + page_name + "</title>\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<style>\n"
    "  body{font-family:Arial,sans-serif;background-color:#121212;color:#e0e0e0;margin:0;padding:20px}\n"
    "  h1{color:#03dac6;border-bottom:1px solid #333;padding-bottom:10px}\n"
    "  .nav-bar{display:flex;gap:10px;margin-bottom:20px;flex-wrap:wrap}\n"
    "  .nav-link{color:#bb86fc;text-decoration:none;padding:6px 14px;border-radius:4px;font-weight:600;background:#1e1e1e}\n"
    "  .nav-link.active{background:#bb86fc;color:#121212}\n"
    "  .grid-container{display:flex;flex-wrap:wrap;gap:20px;align-items:flex-start}\n"
    "  .card{background-color:#1e1e1e;border-radius:8px;padding:20px;box-shadow:0 4px 8px #0000004d;min-width:280px}\n"
    "  h3{margin-top:0;color:#bb86fc}\n"
    "  .sensor-value{font-size:2.2em;font-weight:700;color:#03dac6}\n"
    "  .control-group,.button-group{margin-top:15px}\n"
    "  .control-group label{display:block;margin-bottom:5px;color:#cfcfcf}\n"
    "  input[type=range]{width:100%}.toggle-switch{display:flex;align-items:center}\n"
    "  .toggle-switch input{opacity:0;width:0;height:0}\n"
    "  .slider{position:relative;cursor:pointer;width:40px;height:20px;background-color:#555;border-radius:20px;transition:.4s}\n"
    "  .slider:before{position:absolute;content:\"\";height:16px;width:16px;left:2px;bottom:2px;background-color:#fff;border-radius:50%;transition:.4s}\n"
    "  input:checked+.slider{background-color:#03dac6}input:checked+.slider:before{transform:translateX(20px)}\n"
    "  button{background-color:#bb86fc;color:#121212;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;font-weight:700;margin-right:10px}\n"
    "  button:hover{background-color:#9e66d4}\n"
    "  .help-wrap{display:inline-block;position:relative;margin-left:6px;vertical-align:middle}\n"
    "  .help-icon{color:#bb86fc;cursor:pointer;font-size:1.1em}\n"
    "  .help-tooltip{display:none;position:absolute;left:1.5em;top:0;background:#2a2a2a;color:#e0e0e0;border:1px solid #444;border-radius:6px;padding:10px 14px;min-width:200px;max-width:320px;z-index:100;font-size:.85em;line-height:1.5}\n"
    "  .help-wrap:hover .help-tooltip{display:block}\n"
    "</style>"
  );

  // Conditional CSS — only sent if the page actually needs it
  if (needs_bar) {
    server.sendContent(
      "<style>\n"
      "  .bar-label{display:block;margin-bottom:4px;color:#cfcfcf;font-size:.9em}\n"
      "  .bar-unit{color:#888;font-size:.8em;margin-left:4px}\n"
      "  .bar-row{margin-top:12px}\n"
      "  .bar-track{width:100%;height:12px;background:#333;border-radius:6px;overflow:hidden;margin:4px 0}\n"
      "  .bar-fill{height:100%;background:#03dac6;border-radius:6px;transition:width .4s ease}\n"
      "</style>"
    );
  }
  if (needs_dial) {
    server.sendContent(
      "<style>\n"
      "  .dial-row{margin-top:12px}\n"
      "  .dial-wrap{display:flex;align-items:center;gap:8px;margin-top:4px}\n"
      "  .dial-svg{width:110px;height:68px}\n"
      "  .dial-text{fill:#03dac6;font-size:14px;font-weight:700}\n"
      "</style>"
    );
  }
  if (needs_collapsible) {
    server.sendContent(
      "<style>\n"
      "  .col-header{display:flex;justify-content:space-between;align-items:center;cursor:pointer}\n"
      "  .col-body{margin-top:10px}\n"
      "</style>"
    );
  }
  if (needs_radio) {
    server.sendContent(
      "<style>\n"
      "  .radio-group{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}\n"
      "  .radio-group button{background:#333;color:#e0e0e0}\n"
      "  .radio-group button.active{background:#03dac6;color:#121212}\n"
      "</style>"
    );
  }

  server.sendContent("</head>\n");

  // CHUNK 2: Nav bar + page body
  String nav = "<body><h1>" + page_name + "</h1>\n<div class=\"nav-bar\">\n";
  for (int i = 0; i < resolved_count; i++) {
    if (resolved_table[i].widget != W_PAGE) continue;
    String active = (strcmp(resolved_table[i].id, page_id) == 0) ? " active" : "";
    nav += "  <a href=\"/" + String(resolved_table[i].id) + "\" class=\"nav-link" + active + "\">"
        + String(resolved_table[i].name) + "</a>\n";
  }
  nav += "</div>\n<div class=\"grid-container\">\n";
  server.sendContent(nav);

  // CHUNK 3: Recursive page content
  Serial.printf(">> handlePage('%s') starting recursive render\n", page_id);
  renderContainer(page_id);
  server.sendContent("</div>\n");

  // CHUNK 4: JavaScript
  String indices = buildPageIndices(page_id);
  Serial.printf(">> handlePage('%s') JS index list: [%s]\n", page_id, indices.c_str());

  server.sendContent("<script>\nconst PAGE_INDICES=[" + indices + "];\n");

  server.sendContent(R"=====(
const IDX_TO_ID = {};

function sendUpdate(id, val) {
  fetch("/api/update", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({id:id, value:parseFloat(val)})});
}
function refreshData() {
  if (!PAGE_INDICES.length) return;
  fetch("/api/data?idx=" + PAGE_INDICES.join(",") + "&t=" + new Date().getTime()).then(r => r.json()).then(data => {
    for (const idx in data) {
      const val = parseFloat(data[idx]);
      const rid = IDX_TO_ID[idx];
      if (!rid) continue;
      const el = document.getElementById(rid);
      if (el) el.textContent = val.toFixed(2);
      const bm = document.getElementById("barmeta_" + rid);
      if (bm) {
        const mn = parseFloat(bm.dataset.min), mx = parseFloat(bm.dataset.max);
        const pct = Math.max(0, Math.min(100, (val - mn) / (mx - mn) * 100));
        const bar = document.getElementById("bar_" + rid);
        if (bar) bar.style.width = pct.toFixed(1) + "%";
      }
      const dm = document.getElementById("dialmeta_" + rid);
      if (dm) {
        const mn = parseFloat(dm.dataset.min), mx = parseFloat(dm.dataset.max);
        const pct = Math.max(0, Math.min(1, (val - mn) / (mx - mn)));
        const arc = document.getElementById("dial_" + rid);
        if (arc) arc.setAttribute("stroke-dashoffset", (1 - pct).toFixed(3));
      }
    }
  });
}
document.addEventListener("DOMContentLoaded", () => {
  // Fetch manifest once to build the index-to-id map, then start polling
  fetch("/api/manifest").then(r => r.json()).then(items => {
    items.forEach((item, idx) => { IDX_TO_ID[idx] = item.id; });
    refreshData();
    setInterval(refreshData, 5000);
  });
  document.querySelectorAll("input[type=range]").forEach(el => {
    el.addEventListener("input", e => {
      const vspan = document.getElementById(e.target.id + "-value");
      if (vspan) vspan.textContent = parseFloat(e.target.value).toFixed(2);
      sendUpdate(e.target.id, e.target.value);
    });
  });
  document.querySelectorAll("button[id^=btn_]").forEach(el => {
    el.addEventListener("click", () => sendUpdate(el.id.replace("btn_", ""), 1));
  });
  document.querySelectorAll(".radio-group button").forEach(btn => {
    btn.addEventListener("click", () => {
      const grp = btn.closest(".radio-group");
      if (grp) grp.querySelectorAll("button").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      sendUpdate(btn.id.replace("btn_", ""), 1);
    });
  });
});
function toggleCol(id) {
  const el = document.getElementById(id);
  if (el) el.style.display = (el.style.display === "none") ? "block" : "none";
}
)=====");

  server.sendContent("</script></body></html>\n");
  server.sendContent("");
  Serial.printf(">> handlePage('%s') done streaming.\n", page_id);
}

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
  Serial.println(">> handleRoot: redirecting to first layout page");
  for (int i = 0; i < resolved_count; i++) {
    if (resolved_table[i].widget == W_PAGE) {
      Serial.printf(">> handleRoot: found first page '%s'\n", resolved_table[i].id);
      server.sendHeader("Location", String("/") + resolved_table[i].id);
      server.send(302, "text/plain", "");
      return;
    }
  }
  Serial.println(">> handleRoot ERROR: no W_PAGE node found in layout_table");
  server.send(404, "text/plain", "No pages defined in layout_table");
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
  // Now serves by index list: /api/data?idx=0,3,5
  // Returns {"idx": value, ...} keyed by numeric index
  String idx_param = server.arg("idx");
  Serial.printf(">> handleData idx_param='%s'\n", idx_param.c_str());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{");
  bool first = true;
  int start = 0;
  while (start < (int)idx_param.length()) {
    int comma = idx_param.indexOf(',', start);
    if (comma == -1) comma = idx_param.length();
    uint8_t idx = (uint8_t)idx_param.substring(start, comma).toInt();
    RegistryItem* r = registry.getItem_id(idx);
    if (r) {
      if (!first) server.sendContent(",");
      server.sendContent("\"" + String(idx) + "\":" + String(r->value));
      first = false;
    }
    start = comma + 1;
  }
  server.sendContent("}");
  server.sendContent("");
}

// /api/idxname?idx=3 — returns the registry string id for a numeric index
// The JS uses this to find the correct DOM element after getting data by index
static void handleIdxName() {
  uint8_t idx = (uint8_t)server.arg("idx").toInt();
  RegistryItem* r = registry.getItem_id(idx);
  if (r) {
    Serial.printf(">> handleIdxName idx=%d -> '%s'\n", idx, r->id);
    server.send(200, "text/plain", String(r->id));
  } else {
    Serial.printf(">> handleIdxName ERROR: idx=%d not found\n", idx);
    server.send(404, "text/plain", "");
  }
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
  setupLayoutResolution();
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
  server.on("/api/idxname", HTTP_GET, handleIdxName);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/identity", HTTP_GET, handleIdentity);
  //server.on("/history.svg", HTTP_GET, drawSensorHistory);
  // Register one URL endpoint per PAGE node in the layout table
  for (int i = 0; i < resolved_count; i++) {
    if (resolved_table[i].widget != W_PAGE) continue;
    String path = String("/") + resolved_table[i].id;
    server.on(path.c_str(), HTTP_GET, [i]() { handlePage(resolved_table[i].id); });
    Serial.printf(">> Registered page endpoint: %s\n", path.c_str());
  }
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