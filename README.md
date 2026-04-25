# SonoffS31 Arduino Library

A lightweight and reliable Arduino library for the **Sonoff S31 smart plug** using the **CSE7766 energy monitoring chip**.

Designed for **ESP8266**, this library provides real-time power monitoring, energy tracking, and relay control with a clean and simple API.

---

## ✨ Features

* ⚡ Voltage measurement (V)
* 🔌 Current measurement (A)
* 🔥 Active power measurement (W)
* 🔋 Energy tracking (kWh)
* 🎛 Relay control (ON/OFF/Toggle)
* 🔄 Real-time update loop
* 🧠 Auto calibration (from CSE7766)
* 🛠 Manual calibration support
* 📡 Callback system for:

  * Power updates
  * Relay state changes
* 🧩 Clean and minimal implementation (no external driver required)

---

## 📦 Installation

### Method 1: Manual Installation

1. Download this repository as ZIP
2. Extract to:

```
Documents/Arduino/libraries/SonoffS31
```

3. Restart Arduino IDE

---

### Method 2: Git Clone

```bash
git clone https://github.com/komkritc/sonoff-s31-power-monitor.git
```

---

## 🚀 Quick Start

### Basic Example

```cpp
#include <SonoffS31.h>

SonoffS31 s31;

void setup() {
    Serial.begin(115200);
    s31.begin();
}

void loop() {
    s31.update();

    Serial.print("Voltage: ");
    Serial.println(s31.getVoltage());

    Serial.print("Current: ");
    Serial.println(s31.getCurrent());

    Serial.print("Power: ");
    Serial.println(s31.getPower());

    Serial.print("Energy (kWh): ");
    Serial.println(s31.getEnergy());

    Serial.println("----------------------");

    delay(1000);
}
```

---

## 🔌 Relay Control

```cpp
s31.setRelay(true);   // Turn ON
s31.setRelay(false);  // Turn OFF

s31.toggleRelay();    // Toggle state

bool state = s31.getRelayState();
```

---

## 📊 Available Functions

| Function                  | Description                     |
| ------------------------- | ------------------------------- |
| `begin()`                 | Initialize device               |
| `update()`                | Read and process CSE7766 data   |
| `setRelay(bool)`          | Control relay                   |
| `getRelayState()`         | Get relay state                 |
| `toggleRelay()`           | Toggle relay                    |
| `getVoltage()`            | Voltage (V)                     |
| `getCurrent()`            | Current (A)                     |
| `getPower()`              | Power (W)                       |
| `getEnergy()`             | Energy (kWh)                    |
| `resetEnergy()`           | Reset energy counter            |
| `setVoltageCalibration()` | Manual voltage calibration      |
| `setCurrentCalibration()` | Manual current calibration      |
| `setPowerCalibration()`   | Manual power calibration        |
| `enableAutoCalibration()` | Enable/disable auto calibration |

---

## 📡 Callbacks

### Power Update Callback

```cpp
s31.onPowerUpdate([](float power, float voltage, float current) {
    Serial.println("Power updated!");
});
```

---

### Relay Change Callback

```cpp
s31.onRelayChange([](bool state) {
    Serial.println(state ? "Relay ON" : "Relay OFF");
});
```

---

## ⚙️ How It Works

* The **CSE7766 chip** sends measurement data via UART (4800 baud, 8E1)
* The library:

  * Parses raw packets
  * Validates checksum
  * Applies calibration
  * Computes:

    * Voltage
    * Current
    * Power
    * Energy (integrated over time)

---

## ⚠️ Important Notes

* Call `update()` **as frequently as possible** in `loop()`
* Energy calculation depends on continuous updates
* When relay is OFF:

  * Power, current, and voltage reset to zero
* Uses **hardware Serial** (default)

---

## 🧪 Calibration

By default, calibration is handled automatically.

You can override:

```cpp
s31.setVoltageCalibration(1912);
s31.setCurrentCalibration(16140);
s31.setPowerCalibration(5364);
```

Disable auto calibration:

```cpp
s31.enableAutoCalibration(false);
```

---

## 📁 Examples

* `examples/basic` → simple monitoring
* `examples/dashboard` → web-based interface (extendable)

---

## 🛠 Requirements

* ESP8266 board (Sonoff S31)
* Arduino IDE or PlatformIO

---

## 📜 License

MIT License

---

## 👨‍💻 Author

**Komkrit Chooruang**

---

## 🚀 Future Improvements (Planned)

* Power Factor (PF)
* Apparent Power (VA)
* JSON API output
* MQTT integration
* Async Web Dashboard
* Machine Learning anomaly detection

---

## ⭐ Support

If you find this library useful:

* ⭐ Star the repository
* 🍴 Fork it
* 🛠 Contribute improvements

---
