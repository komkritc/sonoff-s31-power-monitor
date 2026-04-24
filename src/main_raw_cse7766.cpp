#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

// WiFi Configuration
const char* ssid = "redmi4xx";
const char* password = "komkritc";

// Relay Pin
const int relayPin = 12;

// CSE7766 Configuration
#define CSE_HEADER1 0x55
#define CSE_HEADER2 0x5A
#define CSE_PACKET_LEN 24

uint8_t rx_buffer[25];
int byte_counter = 0;

// Power measurements
float voltage = 0.0;
float current = 0.0;
float power = 0.0;
float energy = 0.0;
float accumulated_energy = 0.0;
float last_power = 0.0;
unsigned long last_energy_calc = 0;

// Calibration values
long voltage_cal = 1912;
long current_cal = 16140;
long power_cal = 5364;

// Relay state
bool relayState = false;

// Web Server
ESP8266WebServer server(80);

// Device name
String deviceName = "s31";

// HTML Dashboard
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
    <title>Sonoff S31</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 500px;
            margin: 0 auto;
        }
        
        .card {
            background: white;
            border-radius: 20px;
            padding: 25px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        
        h1 {
            text-align: center;
            color: #333;
            margin-bottom: 5px;
            font-size: 24px;
        }
        
        .subtitle {
            text-align: center;
            color: #666;
            font-size: 11px;
            margin-bottom: 25px;
            padding-bottom: 10px;
            border-bottom: 1px solid #eee;
        }
        
        .power-display {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border-radius: 15px;
            padding: 20px;
            text-align: center;
            margin-bottom: 20px;
            color: white;
        }
        
        .power-label {
            font-size: 14px;
            opacity: 0.9;
            margin-bottom: 5px;
        }
        
        .power-value {
            font-size: 48px;
            font-weight: bold;
        }
        
        .power-unit {
            font-size: 20px;
        }
        
        .stats {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 15px;
            margin-bottom: 20px;
        }
        
        .stat {
            background: #f5f5f5;
            padding: 15px;
            border-radius: 12px;
            text-align: center;
        }
        
        .stat-label {
            font-size: 11px;
            color: #666;
            margin-bottom: 5px;
            text-transform: uppercase;
        }
        
        .stat-value {
            font-size: 22px;
            font-weight: bold;
            color: #333;
        }
        
        .stat-unit {
            font-size: 11px;
            color: #666;
        }
        
        .relay-btn {
            width: 120px;
            height: 120px;
            border-radius: 60px;
            border: none;
            font-size: 22px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s;
            margin: 10px auto 20px;
            display: block;
            box-shadow: 0 5px 20px rgba(0,0,0,0.2);
        }
        
        .relay-btn.on {
            background: #10B981;
            color: white;
            box-shadow: 0 5px 20px rgba(16,185,129,0.4);
        }
        
        .relay-btn.off {
            background: #EF4444;
            color: white;
            box-shadow: 0 5px 20px rgba(239,68,68,0.4);
        }
        
        .relay-btn:active {
            transform: scale(0.95);
        }
        
        .info {
            background: #f8f9fa;
            padding: 12px;
            border-radius: 10px;
            font-size: 11px;
            color: #666;
            text-align: center;
            margin-top: 20px;
        }
        
        .status {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 4px;
            margin-right: 5px;
        }
        
        .status.online { background: #10B981; animation: pulse 2s infinite; }
        .status.offline { background: #EF4444; }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.5; }
            100% { opacity: 1; }
        }
        
        .refresh {
            font-size: 10px;
            color: #999;
            margin-top: 10px;
            text-align: center;
        }
        
        @media (max-width: 480px) {
            .power-value { font-size: 36px; }
            .stat-value { font-size: 18px; }
            .relay-btn { width: 100px; height: 100px; font-size: 18px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>Sonoff S31</h1>
            <div class="subtitle" id="deviceId">Loading...</div>
            
            <div class="power-display">
                <div class="power-label">Current Power</div>
                <div class="power-value">
                    <span id="power">0</span><span class="power-unit">W</span>
                </div>
            </div>
            
            <div class="stats">
                <div class="stat">
                    <div class="stat-label">Voltage</div>
                    <div class="stat-value">
                        <span id="voltage">0</span><span class="stat-unit">V</span>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">Current</div>
                    <div class="stat-value">
                        <span id="current">0</span><span class="stat-unit">A</span>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">Energy</div>
                    <div class="stat-value">
                        <span id="energy">0</span><span class="stat-unit">kWh</span>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">Relay</div>
                    <div class="stat-value">
                        <span id="relayStatus">OFF</span>
                    </div>
                </div>
            </div>
            
            <button class="relay-btn off" id="relayBtn" onclick="toggleRelay()">
                <span id="relayText">OFF</span>
            </button>
            
            <div class="info">
                <div><span class="status" id="statusLed"></span> <span id="statusText">Updating...</span></div>
                <div id="updateTime" class="refresh"></div>
            </div>
        </div>
    </div>
    
    <script>
        async function fetchData() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                document.getElementById('power').innerText = data.power.toFixed(1);
                document.getElementById('voltage').innerText = data.voltage.toFixed(1);
                document.getElementById('current').innerText = data.current.toFixed(2);
                document.getElementById('energy').innerText = data.energy.toFixed(2);
                
                const relayStatus = document.getElementById('relayStatus');
                const relayBtn = document.getElementById('relayBtn');
                const relayText = document.getElementById('relayText');
                
                if (data.relayState) {
                    relayStatus.innerText = 'ON';
                    relayBtn.className = 'relay-btn on';
                    relayText.innerText = 'ON';
                } else {
                    relayStatus.innerText = 'OFF';
                    relayBtn.className = 'relay-btn off';
                    relayText.innerText = 'OFF';
                }
                
                document.getElementById('statusLed').className = 'status online';
                document.getElementById('statusText').innerHTML = 'Connected';
                document.getElementById('updateTime').innerHTML = 'Updated: ' + new Date().toLocaleTimeString();
                
            } catch(e) {
                document.getElementById('statusLed').className = 'status offline';
                document.getElementById('statusText').innerHTML = 'Reconnecting...';
            }
        }
        
        async function toggleRelay() {
            try {
                const response = await fetch('/toggle');
                const result = await response.text();
                setTimeout(fetchData, 100);
            } catch(e) {
                console.error('Toggle error:', e);
            }
        }
        
        async function getDeviceInfo() {
            try {
                const res = await fetch('/info');
                const info = await res.json();
                document.getElementById('deviceId').innerHTML = info.hostname + '.local';
            } catch(e) {}
        }
        
        setInterval(fetchData, 1500);
        setInterval(getDeviceInfo, 30000);
        
        fetchData();
        getDeviceInfo();
    </script>
</body>
</html>
)rawliteral";

// CSE7766 Functions
void cseProcessByte(uint8_t b) {
  rx_buffer[byte_counter++] = b;

  if (byte_counter == CSE_PACKET_LEN) {
    uint8_t checksum = 0;
    for (int i = 2; i < (CSE_PACKET_LEN - 1); i++) checksum += rx_buffer[i];

    if (checksum == rx_buffer[CSE_PACKET_LEN - 1]) {
      // Parse packet
      uint8_t header = rx_buffer[0];
      uint8_t adj = rx_buffer[20];

      // Update calibration
      if (header != 0xAA) {
        long new_voltage_cal = ((rx_buffer[2] << 16) | (rx_buffer[3] << 8) | rx_buffer[4]) / 100;
        long new_current_cal = (rx_buffer[8] << 16) | (rx_buffer[9] << 8) | rx_buffer[10];
        long new_power_cal = ((rx_buffer[14] << 16) | (rx_buffer[15] << 8) | rx_buffer[16]) / 1000;

        if (new_voltage_cal > 0) voltage_cal = new_voltage_cal;
        if (new_current_cal > 0) current_cal = new_current_cal;
        if (new_power_cal > 0) power_cal = new_power_cal;
      }

      // Calculate values
      long voltage_cycle = (rx_buffer[5] << 16) | (rx_buffer[6] << 8) | rx_buffer[7];
      long current_cycle = (rx_buffer[11] << 16) | (rx_buffer[12] << 8) | rx_buffer[13];
      long power_cycle = (rx_buffer[17] << 16) | (rx_buffer[18] << 8) | rx_buffer[19];

      if ((adj & 0x40) && voltage_cycle > 0) {
        float newVoltage = (voltage_cal * 100.0) / voltage_cycle;
        if (newVoltage > 50 && newVoltage < 300) voltage = newVoltage;
      }

      if ((adj & 0x10) && power_cycle > 0 && (header & 0xF2) != 0xF2) {
        float newPower = (power_cal * 1000.0) / power_cycle;
        if (newPower >= 0 && newPower < 5000) power = newPower;
      }

      if ((adj & 0x20) && current_cycle > 0 && power > 0.1) {
        float newCurrent = current_cal / (float)current_cycle;
        if (newCurrent >= 0 && newCurrent < 20) current = newCurrent;
      }

      // Calculate energy
      unsigned long now = millis();
      if (last_energy_calc > 0 && last_power > 0) {
        float hours_elapsed = (now - last_energy_calc) / 3600000.0;
        float avg_power = (power + last_power) / 2.0;
        accumulated_energy += (avg_power / 1000.0) * hours_elapsed;
        energy = accumulated_energy;
      }
      last_power = power;
      last_energy_calc = now;
    }
    byte_counter = 0;
  }

  // Byte alignment
  if (byte_counter == 1 && rx_buffer[0] != CSE_HEADER1) byte_counter = 0;
  if (byte_counter == 2 && rx_buffer[1] != CSE_HEADER2) {
    rx_buffer[0] = rx_buffer[1];
    byte_counter = (rx_buffer[0] == CSE_HEADER1) ? 1 : 0;
  }
}

void setup() {
  // Initialize Serial for CSE7766
  Serial.begin(4800, SERIAL_8E1);

  // Setup relay
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);

  // Generate unique hostname
  //   uint32_t chipId = ESP.getChipId();
  //   deviceName = "s31-" + String(chipId & 0xFFFF, HEX);
  //   deviceName.toLowerCase();

  deviceName = "s31";

  // Connect to WiFi
  WiFi.hostname(deviceName);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Setup mDNS
  MDNS.begin(deviceName.c_str());
  MDNS.addService("http", "tcp", 80);

  // Setup OTA
  ArduinoOTA.setHostname(deviceName.c_str());
  ArduinoOTA.setPassword("admin123");
  ArduinoOTA.begin();

  // Web server routes
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", index_html);
  });

  server.on("/data", HTTP_GET, []() {
    String json = "{";
    json += "\"voltage\":" + String(voltage, 1) + ",";
    json += "\"current\":" + String(current, 3) + ",";
    json += "\"power\":" + String(power, 1) + ",";
    json += "\"energy\":" + String(energy, 3) + ",";
    json += "\"relayState\":" + String(relayState ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/toggle", HTTP_GET, []() {
    relayState = !relayState;
    digitalWrite(relayPin, relayState ? HIGH : LOW);
    server.send(200, "text/plain", "OK");
  });

  server.on("/info", HTTP_GET, []() {
    String json = "{";
    json += "\"hostname\":\"" + deviceName + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();

  last_energy_calc = millis();
}

void loop() {
  // Read CSE7766 data
  while (Serial.available()) {
    int rb = Serial.read();
    if (rb != -1) {
      cseProcessByte((uint8_t)rb);
    }
  }

  // Handle OTA and web server
  ArduinoOTA.handle();
  MDNS.update();
  server.handleClient();

  delay(10);
}