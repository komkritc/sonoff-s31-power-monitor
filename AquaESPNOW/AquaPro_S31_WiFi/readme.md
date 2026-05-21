# Smart Pump Controller v3.5

[![Platform](https://img.shields.io/badge/platform-ESP8266-blue.svg)](https://www.espressif.com/en/products/socs/esp8266)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-3.5-red.svg)](https://github.com)

Industrial-grade ESP8266 pump controller with real-time monitoring, multiple protection systems, and beautiful web interface.

## 📌 Quick Overview

**Smart Pump Controller** is an industrial-grade pump controller that protects your pump from dry running, overload conditions, and rapid cycling. It features real-time power monitoring via Sonoff S31, ESP-NOW water level sensor integration, dual WiFi mode, and a professional web interface.

## 🎯 Key Features

### 🛡️ Safety Protection Systems (Priority Order)

| Priority | Protection | Action |
|----------|-----------|--------|
| **1** | **Dry Run** | Stops pump INSTANTLY when low power detected (3-300s configurable) |
| **2** | **Overload** | Shuts down pump when power exceeds threshold, cancels all timers |
| **3** | **Pressure Lockout** | Prevents rapid pump cycling (5min to 3 days configurable) |

### 🎮 Operation Modes

| Mode | Description |
|------|-------------|
| **AUTO** | Pressure-based automatic control with all protection features |
| **MANUAL** | Direct button or web control with safety overrides |

### 📊 Real-Time Monitoring

| Parameter | Display |
|-----------|---------|
| Voltage | 0-250V AC |
| Current | 0-16A |
| Power | 0-3500W |
| Energy | Cumulative kWh |
| Power Factor | 0-1.00 |
| Water Level | 0-100% with animated gauge |
| Battery Voltage | Sensor battery status |

### ⏱️ Smart Timers

| Timer | Range | Purpose |
|-------|-------|---------|
| Inching | 1-60 min | Auto-stop pump after set duration |
| Soft Start | 0-10000 ms | Prevent water hammer |
| Overload Cooldown | 0-300 sec | Prevent immediate restart |
| Dry Run Cooldown | 0-300 sec | Prevent immediate restart |
| Pressure Lockout | 0-72 hours | Prevent rapid cycling |
| Auto-Return | 10 min | Return to AUTO from MANUAL |

### 🌐 Network Features

- ✅ **Dual WiFi Mode** - STA (client) + AP (access point) simultaneously
- ✅ **mDNS Support** - Access via `http://smartpump.local`
- ✅ **WiFi Scanner** - Scan and connect to available networks
- ✅ **Captive Portal** - Easy initial setup
- ✅ **OTA Updates** - Over-the-air firmware updates
- ✅ **Auto-Reconnect** - Automatic WiFi recovery

### 🖥️ Web Interface

| Page | Features |
|------|----------|
| **Main Dashboard** | Animated tank gauge, real-time power meters, mode toggle, statistics |
| **Engineering Mode** | Full configuration (8 sections), WiFi scanner, factory reset |
| **WiFi Setup** | Network scan, credential entry, reboot countdown |

## 🔧 Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **Microcontroller** | ESP8266 (NodeMCU, Wemos D1 Mini, etc.) |
| **Power Monitoring** | Sonoff S31 smart plug |
| **Pump Control** | 5V relay module |
| **Pressure Switch** | Normally open or closed (GPIO4) |
| **User Input** | Tactile button (GPIO0) |
| **Status LED** | Built-in or external (GPIO13) |
| **Water Level Sensor** | ESP-NOW capable sensor (optional) |

## 📡 Pin Connections

| Component | GPIO Pin | Function |
|-----------|----------|----------|
| Relay Control | 12 | Controls pump relay (HIGH = ON) |
| LED Indicator | 13 | Status indicator |
| Button Input | 0 | User input (LOW = pressed) |
| Pressure Switch | 4 | Pressure sensor input |

## 🎛️ Button Functions

| Action | AUTO Mode | MANUAL Mode |
|--------|-----------|-------------|
| **Short Press** (<3s) | Stop pump → Switch to MANUAL | Toggle pump ON/OFF |
| **Long Press** (5-10s) | Switch to AUTO mode | Switch to AUTO mode |

**Visual Feedback:**
- Single flash = Short press action
- Double flash = Switch to MANUAL
- Triple flash = Switch to AUTO
- Long flash (1s) = Error/cooldown

## 📱 Web Access

| Mode | Access Method |
|------|---------------|
| **AP Mode** | Connect to `SmartPump-XXXX` (password: `12345678`) → `http://192.168.4.1` |
| **STA Mode** | `http://smartpump.local` or device IP address |
| **Engineering Mode** | `/engmode` |
| **WiFi Setup** | `/wifi` |

## ⚙️ Configuration Options

### Protection Settings (Engineering Mode → Protection)

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| Dry Run Detection | 3-300 sec | 3 sec | Time before pump stops |
| Dry Run Threshold | 0-3500 W | 10 W | Below this = dry run |
| Dry Run Cooldown | 0-300 sec | 60 sec | Wait before restart |
| Overload Threshold | 10-3500 W | 1000 W | Above this = overload |
| Overload Cooldown | 0-300 sec | 60 sec | Wait before restart |

### Timer Settings (Engineering Mode → Inching/Soft Start)

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| Inching Duration | 1-60 min | 1 min | Auto-stop after duration |
| Soft Start Delay | 0-10000 ms | 2000 ms | Delay before pump start |
| Inrush Tolerance | 500-5000 ms | 2000 ms | Start-up current tolerance |

### Lockout Settings (Engineering Mode → Lockout)

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| Pressure Lockout | 0-72 hours | 2 hours | Wait after high pressure |
| Quick Presets | 5min, 30min, 1h, 2h, 4h, 8h, 12h, 1d, 2d, 3d | - | One-click presets |

### Network Settings (Engineering Mode → WiFi/System)

| Setting | Description |
|---------|-------------|
| WiFi Client | Enable/disable STA mode |
| SSID/Password | Your WiFi credentials |
| Device Name | mDNS hostname |
| ESP-NOW MAC | Peer device MAC address |
| ESP-NOW Channel | WiFi channel (1-13) |
| Auto-Return | Return to AUTO after 10 min |

## 🔐 Safety Features

### Priority-Based Protection
1. **Dry Run** - Highest priority, cancels all timers
2. **Overload** - Second priority, cancels all timers
3. **Pressure Lockout** - Third priority, cancels all timers

### Safety Mechanisms
- ✅ Timer cancellation on safety events
- ✅ Cooldown periods prevent immediate restarts
- ✅ Mode change safety (pump stops when switching to MANUAL)
- ✅ Button lockout during cooldown periods
- ✅ Watchdog timer prevents system hangs
- ✅ Mutex protection for file writes

## 📈 Statistics Tracking

| Metric | Description |
|--------|-------------|
| Total Runtime | Cumulative pump operation (minutes) |
| Pump Cycles | Number of start/stop cycles |
| Overload Events | Count of overload protections triggered |
| Dry Run Events | Count of dry run protections triggered |
| Soft Start Count | Number of soft start delays applied |
| Button Presses | Total button interactions |
| Energy Used | Cumulative power consumption (kWh) |

## 💾 Data Persistence

### Files (LittleFS)

| File | Purpose |
|------|---------|
| `config.json` | Main configuration storage |
| `config_backup.json` | Automatic backup on corruption |
| `wifi.json` | WiFi credentials storage |

### Features
- ✅ Mutex protection for concurrent writes
- ✅ Rate limiting (minimum 2 seconds between saves)
- ✅ Automatic backup on file corruption
- ✅ Factory reset option

## 🔄 ESP-NOW Integration

### Sensor Data Received

| Field | Description |
|-------|-------------|
| `d` | Distance to water (cm) |
| `l` | Water level (%) |
| `v` | Water volume (L) |
| `b` | Sensor battery voltage (V) |

### Features
- ✅ Automatic recovery on communication loss
- ✅ Virtual tank hides when ESP-NOW disabled
- ✅ Configurable peer MAC address
- ✅ Request sensor data on demand

## 🎨 UI Features

| Component | Description |
|-----------|-------------|
| **Animated Water Gauge** | Real-time level visualization |
| **Power Meter** | Voltage, Current, Power, Energy |
| **Status Badges** | Color-coded status indicators |
| **Timer Display** | Countdown for all active timers |
| **Protection Warnings** | Visual alerts for safety events |
| **WiFi Status** | Connection info with mDNS address |
| **Reboot Countdown** | 5-second cancelable countdown |
| **Mobile Responsive** | Works on all screen sizes |

## 📝 Logging System

### Features
- ✅ Ring buffer (30 entries)
- ✅ Timestamped entries (`[Xs] message`)
- ✅ Debug mode with serial output
- ✅ Critical error logging
- ✅ Automatic log rotation

## 🔋 Power Monitoring (Sonoff S31)

### Measurements

| Value | Description |
|-------|-------------|
| Real Power | Active power consumption (W) |
| Apparent Power | VA calculation |
| Power Factor | Real/Apparent ratio (0-1.00) |
| Voltage | RMS voltage (V) |
| Current | RMS current (A) |
| Energy | Cumulative consumption (kWh) |

### Update Rate
- **100ms** - Continuous monitoring
- **Real-time** - Web interface updates every second

## 🚀 Getting Started

### 1. Flash the Firmware
```bash
# Using Arduino IDE
1. Install ESP8266 board support (3.1.2)
2. Install required libraries
3. Select board: NodeMCU 1.0 (ESP-12E Module)
4. Upload the sketch
