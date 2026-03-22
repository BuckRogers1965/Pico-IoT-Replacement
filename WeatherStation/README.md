# 🌦️ A Declarative IoT Platform for the Raspberry Pi Pico W

> **A complete, production-grade IoT framework that reduces building a complex hardware device with a professional multi-page web interface to defining three tables in a single C++ file.**

---

## What This Is

BluePrint is a local-first, cloud-free IoT application framework for the Raspberry Pi Pico W. It is built on a custom dual-core architecture that keeps real-time hardware logic completely isolated from network activity, and a declarative three-table UI engine that generates dynamic multi-page web interfaces automatically.

The developer's entire job is:

1. Write sensor callbacks
2. Define what sensors and controls exist (the **Registry**)
3. Define what the website looks like (the **Layout Table**)
4. Optionally write documentation for sensors and cards (the **Help Table**)

The framework handles everything else — dual-core synchronization, web server, HTML generation, CSS, JavaScript, live data binding, multi-page routing, WiFi provisioning, mDNS discovery, and Home Assistant integration.

This repository includes the framework itself and a Weather Station & Smart Irrigation Controller as the reference implementation.

---

## Why This Is Different

Most Pico W IoT projects fall into one of two categories. Either the developer writes every byte of HTML themselves, or they reach for a platform like ESPHome — which requires cloud compilation, a proprietary app, an account, and a YAML configuration model that fights you the moment your project doesn't fit a pre-built component.

BluePrint is a third thing. It runs entirely locally. It compiles in the Arduino IDE. It requires no cloud, no app, no account. And it leaves **62% of RAM free** for the actual application after the complete framework is running.

That 62% figure is not an accident. Every design decision — from the lock-free inter-core communication to the zero-allocation chunked streaming to the Flash-resident layout tables — was made to keep RAM free for the developer's code.

---

## Core Architecture

### Asymmetric Multiprocessing (AMP)

The RP2040's two cores are used correctly from the ground up:

- **Core 0 — The Network Core:** Runs the web server, handles all WiFi and mDNS activity, serves pages, processes API requests
- **Core 1 — The Hardware Core:** Runs the real-time application state machine, polls sensors on a low-power scheduler, executes control logic

Neither core ever blocks waiting for the other. The web server can stream a full page to a browser while sensors are being read and relays are being switched simultaneously.

### Lock-Free Inter-Core Synchronization

Cores communicate through the RP2040's hardware FIFO using a mirrored registry. Each core has its own copy of the state. Dirty flags track which values have changed. The FIFO carries only delta updates — small fixed-size messages packed into 32-bit words. No mutexes. No spinlocks. No core ever waits.

**State coalescing** is built in. If a slider is moved 50 times in a second, the framework absorbs the intermediate values and only pushes the final state across the FIFO. This prevents congestion and makes the system naturally debounced.

### Zero-Allocation Chunked Streaming

Web pages, JSON data, and documentation strings are all streamed in small chunks directly from Flash to the network buffer using `sendContent()`. No large HTML strings are ever constructed in heap memory. No `String` concatenation builds multi-kilobyte buffers. This prevents heap fragmentation and stack overflows, allowing the device to run for months without a reboot regardless of page complexity.

---

## The Three-Table UI Engine

The UI system is built on complete separation of the data model from the view definition.

### Table 1 — The Registry (the Model)

`app_register_items()` defines what the device knows: sensors, their polling intervals, their hardware callbacks, controls and their ranges. This table has no knowledge of HTML, pages, or layout. It never will.

```cpp
RegistryDef app_register_items() {
    RegistryDef def;
    int i = 0;

    SENSOR_AUTO("temp_a",     "Temperature A", 6004, "°F", readAM2302a_temp);
    SENSOR_AUTO("humidity_a", "Humidity A",    7003,  "%", readAM2302a_humidity);
    SENSOR_MANUAL("heat_index", "Heat Index",  "°F");
    CONTROL_SLIDER("moisture_target", "Target Moisture", 40, 20, 80, 5, "%");
    CONTROL_BUTTON("water_now", "Manual Water");

    def.count = i;
    return def;
}
```

### Table 2 — The Layout Table (the View)

`layout_table[]` defines what the website looks like: pages, layout containers, cards, and which registry items map to which visual widgets. It is a flat array of parent-child relationships. The framework resolves it into a tree at boot and generates the complete website from it.

```cpp
const LayoutNode layout_table[] = {
    {"root",         "",          "Root",           "", W_ROOT,   ""},

    {"main_page",    "root",      "Weather Station","", W_PAGE,   ""},
    {"main_grid",    "main_page", "",               "", W_GRID,   ""},
    {"sensor_card",  "main_grid", "Live Sensors",   "", W_CARD,   "width:400"},
    {"w_temp",       "sensor_card","",    "temp_a",    W_DIAL,   "min:0,max:120"},
    {"help_temp",    "sensor_card","",    "",          W_HELP,   ""},
    {"w_humidity",   "sensor_card","", "humidity_a",  W_BAR,    "min:0,max:100"},
    {"w_heat",       "sensor_card","", "heat_index",  W_TEXT,   ""},
    {"card_info",    "sensor_card","",    "",          W_HTML,   ""},

    {"control_card", "main_grid", "Controls",       "", W_CARD,   ""},
    {"w_target",     "control_card","","moisture_target",W_SLIDER,""},
    {"w_water",      "control_card","","water_now",   W_BUTTON, ""},

    {"system_page",  "root",      "System",         "", W_PAGE,   ""},
    {"sys_col",      "system_page","",               "", W_COLUMN, ""},
    {"status_card",  "sys_col",   "Status",         "", W_CARD,   ""},
    {"w_cpu",        "status_card","","cpu_temp_f",  W_TEXT,   ""},
    {"w_ram",        "status_card","","free_ram",    W_BAR,    "min:0,max:100"},
};
const int layout_count = sizeof(layout_table) / sizeof(LayoutNode);
```

Moving a sensor to a different page means changing one `parent_id` string. Changing a text readout to a dial means changing one `widget` field. Adding a page means adding one `W_PAGE` node — the framework registers the URL and adds the nav tab automatically.

### Table 3 — The Help Table (the Documentation)

`help_table[]` holds static HTML strings stored in Flash. They are streamed directly to the browser on demand — never loaded into RAM. A `W_HELP` node renders an ⓘ icon; hovering shows the tooltip. A `W_HTML` node streams the content inline.

```cpp
const HelpNode help_table[] = {
    {"help_temp",
     "<b>Temperature A</b><br>AM2302 sensor on GPIO 2.<br>"
     "Range: -40 to 80&deg;C. Updates every ~6 seconds."},

    {"card_info",
     "<p><strong>Outdoor Conditions</strong></p>"
     "<p>Live readings from the AM2302 sensor on GPIO 2.</p>"},
};
const int help_count = sizeof(help_table) / sizeof(HelpNode);
```

---

## Widget Types

| Widget | Description | Props |
|---|---|---|
| `W_TEXT` | Plain numeric readout with label and unit | — |
| `W_BAR` | Horizontal progress bar | `min:N,max:N` |
| `W_DIAL` | SVG arc gauge | `min:N,max:N` |
| `W_SLIDER` | Range slider control | — (uses registry min/max/step) |
| `W_BUTTON` | Momentary trigger button | — |
| `W_HELP` | ⓘ hover tooltip from help table | — |
| `W_HTML` | Inline static HTML from help table | — |

## Container Types

| Container | Description | Props |
|---|---|---|
| `W_ROOT` | Invisible tree root — one per layout | — |
| `W_PAGE` | Top-level page with its own URL and nav tab | — |
| `W_GRID` | Responsive CSS grid | `cols:N,gap:N` |
| `W_COLUMN` | Vertical flex column | `gap:N` |
| `W_ROW` | Horizontal flex row, wrapping | `gap:N` |
| `W_CARD` | Bordered content card | `width:N` |
| `W_COLLAPSIBLE` | Card with show/hide toggle | — |
| `W_TABBED` | Child containers as tabs | — |
| `W_RADIO` | Mutually exclusive button group | — |

---

## Boot-Time Resolution

At startup, after the registry is built, the framework runs a resolution pass over the layout table. Every `registry_id` string is resolved to a numeric array index. This is the only moment string comparisons happen. All runtime rendering, data serving, and JavaScript generation uses only O(1) numeric index operations.

If a `registry_id` doesn't match any registry entry, the error is logged immediately to the serial port and the device boots anyway. Misconfiguration is caught at startup, not during a user's browser session.

---

## Additional Platform Features

### Smart Captive Portal — Zero-Config Provisioning

On first boot, if no WiFi credentials are stored, the device automatically creates a WiFi access point named `DeviceName-XXXX`. Connecting with a phone or computer auto-opens a configuration page (captive portal). The user selects their network, enters the password, names the device, and hits save. The device reboots into normal mode. No hardcoded credentials. No serial port interaction. No computer required.

### mDNS Discovery

The device advertises itself on the local network as `picow-iot-device.local` and registers a `_iot-framework._tcp` mDNS service record. It is reachable by name from any browser on the same network without knowing the IP address.

### Home Assistant MQTT Auto-Discovery

The included Python bridge (`pico_discovery_bridge.py`) runs on any machine on the same network. It listens for `_iot-framework._tcp` mDNS announcements, queries each device's API, and automatically creates Home Assistant entities for every sensor and control — with correct device classes, units, and MQTT topics. No Home Assistant configuration required.

### Low-Power Scheduler

Core 1 runs on a cooperative task scheduler (`SchedulerLP_pico`) that sleeps between task executions rather than spinning. Sensor callbacks self-reschedule at their declared interval. The scheduler wakes only when a task is due, minimizing power consumption without requiring complex power management code.

---

## Memory Footprint

The complete platform — dual-core AMP, lock-free FIFO sync, dynamic multi-page UI engine, captive portal, mDNS, flash config storage — leaves **62% of SRAM free** on the Pico W (264KB total).

The layout table lives in Flash. The help strings live in Flash. The resolved node table is approximately 5KB of fixed RAM. The renderer never heap-allocates. The JavaScript is generated once per page request from Flash-resident string literals and streamed directly to the network.

---

## Project Structure

```
WeatherStation_des.ino    — Reference implementation: sensors, layout, help tables
PicoW_IoT_Framework.h    — The complete framework: registry, renderer, web server
PicoCoreFifo.h           — Hardware FIFO inter-core messaging
SchedulerLP_pico.h/.cpp  — Low-power cooperative task scheduler
pico_discovery_bridge.py — Home Assistant MQTT auto-discovery bridge
```

The framework is a single header file. An application requires only `WeatherStation_des.ino` (renamed for the project), `PicoW_IoT_Framework.h`, `PicoCoreFifo.h`, and the scheduler library.

---

## Getting Started

### Requirements

- Arduino IDE with the [Earle Philhower RP2040 core](https://github.com/earlephilhower/arduino-pico)
- Raspberry Pi Pico W

### Libraries Required

- AM2302-Sensor
- Adafruit BME280
- BH1750
- ArduinoJson
- Raspberry Pi Pico CPU Temperature

### First Boot

1. Open `WeatherStation_des.ino` in the Arduino IDE
2. Upload to the Pico W
3. On first boot the device creates a WiFi access point — connect to `Weather-Setup-XXXX`
4. The configuration page opens automatically — select your WiFi network and name the device
5. The device reboots and connects to your network
6. Open `http://picow-iot-device.local` in a browser

### Building Your Own Device

1. Copy `WeatherStation_des.ino` and rename it for your project
2. Write your sensor callbacks
3. Populate `app_register_items()` with your sensors and controls
4. Declare your pages, cards, and widgets in `layout_table[]`
5. Optionally add `help_table[]` entries for documentation
6. Upload — the framework generates the complete website automatically

See the [Configuration Manual](docs/blueprint_config_manual.md) and [Layout Table Reference](docs/blueprint_layout_reference.md) for complete documentation.

---

## Hardware Pinout — Weather Station Reference Implementation

| Component | Pin (GP) | Type | Notes |
|---|---|---|---|
| **AM2302 Sensor A** | `2` | Digital | Temperature & Humidity |
| **AM2302 Sensor B** | `3` | Digital | Temperature & Humidity |
| **I2C SDA/SCL** | `4/5` | I2C | BME280 & BH1750 |
| **Water Relay** | `16` | Output | Active High |
| **Soil Sensor** | `27` | Analog | Capacitive |
| **Rain/Wind** | `14/15` | Input | PIO Driven (planned) |

---

## Roadmap

- `W_GRID`, `W_COLUMN`, `W_ROW` layout containers (replaces hardcoded page flex layout)
- `W_LED` binary indicator widget
- Per-page custom CSS injection via help table
- SVG graph history widget
- Multi-device dashboard aggregation via the Python bridge

---

*SE Ohio — https://github.com/BuckRogers1965/Pico-IoT-Replacement*