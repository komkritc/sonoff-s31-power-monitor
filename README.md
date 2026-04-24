# ⚡ Sonoff S31 Power Monitor API

A lightweight **ESP8266-based REST API** for power monitoring and relay control using the Sonoff S31.

This firmware exposes real-time electrical measurements and device control through simple HTTP endpoints.

---

# 🚀 Quick Start

### 1. Flash firmware

Upload using PlatformIO or Arduino IDE.

### 2. Configure WiFi

```cpp
const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
```

### 3. Access device

After boot:

* IP: `http://192.168.x.x`
* mDNS: `http://s31.local`

---

# 🌐 API Overview

| Endpoint  | Method | Description              |
| --------- | ------ | ------------------------ |
| `/data`   | GET    | Get real-time power data |
| `/toggle` | GET    | Toggle relay             |
| `/info`   | GET    | Device info              |

---

# 📊 1. Get Power Data

### Request

```http
GET /data
```

### Response

```json
{
  "voltage": 220.5,
  "current": 0.52,
  "power": 110.3,
  "energy": 0.125,
  "relayState": true
}
```

### Parameters

| Field        | Unit | Description        |
| ------------ | ---- | ------------------ |
| `voltage`    | V    | Line voltage       |
| `current`    | A    | Load current       |
| `power`      | W    | Active power       |
| `energy`     | kWh  | Accumulated energy |
| `relayState` | bool | ON/OFF state       |

---

# 🔌 2. Toggle Relay

### Request

```http
GET /toggle
```

### Response

```text
OK
```

### Behavior

* If OFF → turns ON
* If ON → turns OFF

---

# ℹ️ 3. Device Info

### Request

```http
GET /info
```

### Response

```json
{
  "hostname": "s31",
  "ip": "192.168.1.100"
}
```

---

# 🔁 Example Usage

## Using curl

### Get data

```bash
curl http://192.168.1.100/data
```

### Toggle relay

```bash
curl http://192.168.1.100/toggle
```

---

## Using Python

```python
import requests

url = "http://192.168.1.100/data"
data = requests.get(url).json()

print("Power:", data["power"], "W")
print("Energy:", data["energy"], "kWh")
```

---

## Using JavaScript (Frontend)

```javascript
async function getData() {
  const res = await fetch('/data');
  const data = await res.json();
  console.log(data);
}
```

---

# ⏱️ Update Rate

* Sensor updates: ~10–100 ms
* API recommended polling: **1–2 seconds**

---

# ⚠️ Notes

* No authentication (local network use recommended)
* Energy resets on reboot (no persistent storage)
* Accuracy depends on calibration values
* Designed for **real-time monitoring**, not billing-grade metering

---

# 🧠 Architecture

* ESP8266 Web Server
* UART-based CSE7766 parsing
* Real-time energy integration
* REST API interface

---

# 🔧 Future API Extensions

Planned endpoints:

| Endpoint  | Description          |
| --------- | -------------------- |
| `/on`     | Force relay ON       |
| `/off`    | Force relay OFF      |
| `/reset`  | Reset energy         |
| `/config` | Calibration settings |
| `/mqtt`   | MQTT integration     |

---

# 👨‍💻 Author

Komkrit Chooraung

---

# 📄 License

MIT License
