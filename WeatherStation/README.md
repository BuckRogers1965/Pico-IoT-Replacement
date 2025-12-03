# 🌦️ PicoW Weather Station & Smart Irrigation Controller

> **⚠️ WORK IN PROGRESS**
>
> This project is currently in active development. Features, pinouts, and API endpoints are subject to change. This firmware relies on the **PicoW IoT Framework** which must be present in your project directory.

## 📖 Overview

This is a comprehensive weather monitoring and automated irrigation system running on the **Raspberry Pi Pico W**. Unlike standard weather stations that sleep to save power, this system utilizes the RP2040's dual-core architecture to run a 24/7 web server and real-time control logic simultaneously.

It combines atmospheric sensing (BME280), light monitoring (BH1750), and physical weather hardware (Wind/Rain) with a logic-based soil moisture irrigation controller.

## ⚙️ Features

*   **Real-time Sensing:**
    *   Temperature, Humidity, Pressure (BME280).
    *   Derived Metrics: Heat Index, Dew Point.
    *   Light Levels in Lux (BH1750).
    *   Wind Speed (Anemometer) & Direction (16-point Vane).
    *   Rainfall (Tipping bucket).
*   **Smart Irrigation:**
    *   Monitors Capacitive Soil Moisture sensors.
    *   Triggers a Water Valve Relay based on moisture thresholds.
    *   **Logic:** Includes "Cooldown" timers to prevent over-watering and "Water Duration" safety stops.
*   **Dual-Core Architecture:**
    *   **Core 0:** Handles WiFi, Web Dashboard, and API (Home Assistant).
    *   **Core 1:** Polling sensors and running the Irrigation State Machine.
*   **Web Dashboard:**
    *   Built-in historical graphing (Temp, Humidity, Soil, Rain).
    *   Manual override controls for irrigation.

## 🔌 Hardware Pinout

The firmware is configured for the following GP pins on the Raspberry Pi Pico W:

| Component | Pin (GP) | Type | Notes |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | `4` | I2C | BME280 & BH1750 |
| **I2C SCL** | `5` | I2C | BME280 & BH1750 |
| **Rain Gauge** | `14` | Input (Pullup) | Tipping bucket (Interrupt) |
| **Anemometer** | `15` | Input (Pullup) | Wind Speed (Interrupt) |
| **Water Relay** | `16` | Output | Active High Relay |
| **Wind Vane** | `26` | Analog (ADC0) | Voltage Divider array |
| **Soil Sensor** | `27` | Analog (ADC1) | Capacitive Sensor |

**Note on Wind Vane:** The code assumes a standard 8-resistor array wind vane (common in Sparkfun/Adafruit weather kits). Calibration values are hardcoded in `VANE_VOLTAGES`.

## 📦 Dependencies

To compile this project in the Arduino IDE, you need:

1.  **Board Support:** Raspberry Pi Pico (Earle Philhower core recommended).
2.  **Libraries:**
    *   `Adafruit BME280 Library`
    *   `BH1750` (by Christopher Laws)
    *   `ArduinoJson`
3.  **Local Framework:**
    *   `PicoW_IoT_Framework.h`
    *   `SchedulerLP_pico.h`
    *   *(These files must be in the sketch folder)*

## 🎛️ Logic & Control

### Irrigation State Machine
The device runs a logic loop once per second to manage watering:
1.  **IDLE:** Checks if Soil Moisture < Target %.
2.  **WATERING:** Turns relay ON for `Water Duration` seconds.
3.  **COOLDOWN:** Prevents watering again for `Min Time Between` hours, allowing water to soak in.

### Web Controls
The Web UI (hosted on the Pico) allows you to configure:
*   **Target Moisture:** The % at which watering begins.
*   **Water Duration:** How long the valve stays open.
*   **Manual Water:** Button to trigger an immediate cycle.

## 🔗 Home Assistant Integration

This device is designed to work with the **Pico Discovery Bridge**.
It exposes the following entities via the API:

*   `sensor.weather_station_temp_f`
*   `sensor.weather_station_humidity`
*   `sensor.weather_station_soil_moisture`
*   `sensor.weather_station_wind_speed`
*   `sensor.weather_station_wind_direction`
*   `sensor.weather_station_rainfall`
*   `number.weather_station_moisture_target` (Slider)
*   `button.weather_station_manual_water`

## 🚀 Installation

1.  Copy `PicoW_IoT_Framework.h`, `SchedulerLP_pico.h`, and `WeatherStation.ino` into a folder named `WeatherStation`.
2.  Open `WeatherStation.ino` in Arduino IDE.
3.  Select Board: **Raspberry Pi Pico W**.
4.  Upload.
5.  On first boot, connect to WiFi AP `Weather-Setup` to configure your credentials.
