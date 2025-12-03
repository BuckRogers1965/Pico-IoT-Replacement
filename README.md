# PicoW IoT Framework (The "Hacker's OS")
BETA RELEASE! UNDER HEAVY DEVELOPMENT!  This is an OS for the Raspberry Pico board to make a full featured  IoT replacement for people building hardware for houses.  It aims to prove a web server to access nd control the hardware and make it simple to register hardware to the system and run it with a central state machine


> **⚠️ WORK IN PROGRESS**
>
> This framework is currently in active development. APIs may change. This is a Proof of Concept operating system designed to turn a Raspberry Pi Pico W into a professional IoT device with zero networking code.

## 💀 The Problem
When hacking hardware on a microcontroller, you usually spend 20% of your time on the hardware logic (reading sensors, moving motors) and 80% of your time fighting with WiFi connection strings, HTML, CSS, parsing JSON, and trying to get Home Assistant to see your device.

## ⚡ The Solution
This Framework acts as a lightweight **IoT Operating System** for the Pico W.

It utilizes the RP2040's **Dual-Core** architecture to separate concerns:
1.  **Core 0 (The OS):** Handles WiFi, Web Server, File System, API, mDNS, and configuration. **You don't touch this.**
2.  **Core 1 (Your Code):** You write your hardware logic here. It runs uninterrupted by network lag.

**You write the hardware code. The OS gives you a professional Web Interface and API for free.**

## ✨ Features

*   **Instant Web UI:** simply "Register" a variable (e.g., `CONTROL_SLIDER("motor_speed")`), and the OS automatically generates a responsive Web UI with that control. No HTML/JS required.
*   **Zero-Touch Home Assistant:** Paired with the Python Bridge, your device is auto-discovered. Add a sensor in C++, and it appears in Home Assistant automatically.
*   **Jitter-Free Hardware:** Because the WiFi stack runs on a separate core, your LED strips, stepper motors, or high-speed sensors won't glitch when you load a webpage.
*   **Captive Portal:** If WiFi fails, the OS spawns an Access Point (`Device-Setup`) to let you configure credentials via your phone.
*   **Built-in Scheduler:** A cooperative multitasking scheduler allows you to run different hardware tasks at precise frequencies (e.g., control loop at 60Hz, telemetry at 1Hz).

## 🛠️ How it Works

The Framework relies on a **Central Registry**. You define what your data is, and the OS handles how to display it.

### 1. The Setup
Include the framework in your sketch. You do not write `setup()` or `loop()`. The OS owns those.

```cpp
#include "PicoW_IoT_Framework.h"
```

### 2. The Registration
Tell the OS what hardware you have hooked up.

```cpp
void app_register_items() {
    // I have a temp sensor, read it every 1 second
    SENSOR_AUTO("temp", "Temperature", 1000, "C", readTempCallback);
    
    // I have a motor, give me a slider from 0 to 100
    CONTROL_SLIDER("speed", "Motor Speed", 0, 0, 100, 1, "%");
    
    // I have a light, give me a toggle switch
    CONTROL_TOGGLE("light", "Status LED");
}
```

### 3. The Logic (Core 1)
Write your logic using simple Getters and Setters. The OS handles the thread safety between cores.

```cpp
// This runs on Core 1, completely separate from WiFi
void my_motor_logic() {
    float speed = getRegistryValue("speed"); // Get value from Web UI
    analogWrite(MOTOR_PIN, speed);
    
    setRegistryValue("temp", 24.5); // Send value to Web UI
}
```

## 📂 Included Demos

This repository includes several "Apps" that run on top of this OS:

1.  **`MyNonEventProject`**: A reactive motor controller. Demonstrates high-speed control loops (60Hz) running alongside the web server.
2.  **`WeatherStation`**: A complex sensor array (BME280, Wind, Rain, Soil). Demonstrates reading multiple I2C and Analog sensors and state-machine automation.

## 🔌 The Python Bridge

Included is `pico_discovery_bridge.py`. Run this script on your network (e.g., on a Raspberry Pi).
1.  It listens for Pico devices via mDNS.
2.  It downloads their "Manifest" (the list of sensors/controls).
3.  It automatically creates entities in **Home Assistant** via MQTT.

## 📦 Requirements

*   **Hardware:** Raspberry Pi Pico W
*   **Software:** Arduino IDE
*   **Core:** Earle Philhower RP2040 Core
*   **Libraries:** `ArduinoJson`, `LittleFS`

## 🚧 Status
*   [x] Dual Core implementation
*   [x] Registry / Manifest System
*   [x] Auto-generated Web UI
*   [x] Scheduler
*   [ ] Secure Authentication
*   [ ] OTA Updates
*   [ ] Deep Sleep support
