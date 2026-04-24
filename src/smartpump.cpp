/*
 * Sonoff S31 Smart Pump Controller
 * Controls water pump based on tank water level via MQTT
 * 
 * Features:
 * - Auto pump control based on water level thresholds
 * - Manual pump control via web interface
 * - WiFi configuration portal (AP mode if no WiFi)
 * - MQTT integration with tank monitor
 * - Real-time power monitoring
 * - Configurable high/low water level thresholds
 * - OTA updates
 * 
 * Author: Komkrit Chooraung
 * Version: 2.0.0
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SonoffS31.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// ========== Configuration Structure ==========
struct Config {
  char ssid[32] = "";
  char password[32] = "";
  char mqtt_server[40] = "broker.hivemq.com";
  int mqtt_port = 1883;
  char mqtt_topic[64] = "TankMonitor888/status";
  float low_threshold = 30.0;    // Pump ON when level below this (%)
  float high_threshold = 80.0;   // Pump OFF when level above this (%)
  bool auto_mode = true;          // Auto/Manual mode
  float min_power_threshold = 5.0; // Minimum power to detect pump running (Watts)
  unsigned long pump_protection_time = 300; // Dry run protection time (seconds)
} config;

// ========== Pin Definitions ==========
#define RELAY_PIN 12
#define CSE7766_RX_PIN 3

// ========== Global Objects ==========
SonoffS31 s31(CSE7766_RX_PIN, RELAY_PIN);
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ========== Global Variables ==========
String deviceName = "s31-pump";
String apSSID = "";
bool apMode = false;
bool needsSave = false;
unsigned long lastMqttReconnect = 0;
unsigned long lastDataUpdate = 0;
unsigned long pumpStartTime = 0;
bool pumpRunning = false;
float currentWaterLevel = 0;
float currentDistance = 0;
float currentVolume = 0;
String waterSource = "No Data";
float batteryLevel = 0;
unsigned long lastMqttData = 0;
bool mqttDataValid = false;

// ========== HTML Pages ==========
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Pump Controller</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        
        .card {
            background: white;
            border-radius: 20px;
            padding: 25px;
            margin-bottom: 20px;
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
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 1px solid #eee;
        }
        
        .water-level {
            background: linear-gradient(135deg, #3b82f6 0%, #1e3a8a 100%);
            border-radius: 15px;
            padding: 20px;
            text-align: center;
            margin-bottom: 20px;
            color: white;
        }
        
        .level-value {
            font-size: 56px;
            font-weight: bold;
        }
        
        .level-unit {
            font-size: 20px;
        }
        
        .level-bar-container {
            background: rgba(255,255,255,0.2);
            border-radius: 10px;
            margin-top: 15px;
            height: 30px;
            overflow: hidden;
        }
        
        .level-bar {
            background: #10B981;
            height: 100%;
            transition: width 0.5s;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-size: 12px;
            font-weight: bold;
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
        
        .pump-status {
            text-align: center;
            margin-bottom: 20px;
        }
        
        .pump-btn {
            width: 140px;
            height: 140px;
            border-radius: 70px;
            border: none;
            font-size: 24px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s;
            margin: 10px auto;
            display: block;
            box-shadow: 0 5px 20px rgba(0,0,0,0.2);
        }
        
        .pump-btn.running {
            background: #10B981;
            color: white;
            animation: pulse-green 2s infinite;
        }
        
        .pump-btn.stopped {
            background: #EF4444;
            color: white;
        }
        
        .pump-btn.manual {
            background: #F59E0B;
            color: white;
        }
        
        @keyframes pulse-green {
            0% { box-shadow: 0 0 0 0 rgba(16,185,129,0.7); }
            70% { box-shadow: 0 0 0 15px rgba(16,185,129,0); }
            100% { box-shadow: 0 0 0 0 rgba(16,185,129,0); }
        }
        
        .mode-switch {
            display: flex;
            gap: 10px;
            justify-content: center;
            margin-bottom: 20px;
        }
        
        .mode-btn {
            padding: 10px 20px;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            font-weight: bold;
            transition: all 0.3s;
        }
        
        .mode-btn.active {
            background: #667eea;
            color: white;
        }
        
        .mode-btn.inactive {
            background: #e5e7eb;
            color: #666;
        }
        
        .settings {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 12px;
            margin-top: 15px;
        }
        
        .setting-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
            padding: 8px;
            background: white;
            border-radius: 8px;
        }
        
        .setting-label {
            font-weight: bold;
            color: #333;
        }
        
        .setting-value {
            color: #667eea;
            font-weight: bold;
        }
        
        input, select {
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 5px;
            margin: 5px 0;
            width: 100%;
        }
        
        button {
            background: #667eea;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 10px;
            cursor: pointer;
            margin-top: 10px;
            font-weight: bold;
        }
        
        button:hover {
            background: #5a67d8;
        }
        
        .info {
            font-size: 11px;
            color: #666;
            text-align: center;
            margin-top: 15px;
            padding-top: 10px;
            border-top: 1px solid #eee;
        }
        
        .status-led {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 4px;
            margin-right: 5px;
        }
        
        .status-online { background: #10B981; animation: pulse 2s infinite; }
        .status-offline { background: #EF4444; }
        
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.5; }
            100% { opacity: 1; }
        }
        
        .nav-buttons {
            display: flex;
            gap: 10px;
            margin-bottom: 15px;
        }
        
        .nav-btn {
            flex: 1;
            background: #e5e7eb;
            color: #333;
            padding: 10px;
        }
        
        .nav-btn.active {
            background: #667eea;
            color: white;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>💧 Smart Pump Controller</h1>
            <div class="subtitle" id="deviceId">Loading...</div>
            
            <div class="nav-buttons">
                <button class="nav-btn active" onclick="showDashboard()">Dashboard</button>
                <button class="nav-btn" onclick="showSettings()">Settings</button>
                <button class="nav-btn" onclick="showStats()">Statistics</button>
            </div>
            
            <!-- Dashboard View -->
            <div id="dashboardView">
                <div class="water-level">
                    <div>Current Water Level</div>
                    <div class="level-value">
                        <span id="waterLevel">0</span><span class="level-unit">%</span>
                    </div>
                    <div class="level-bar-container">
                        <div class="level-bar" id="levelBar" style="width: 0%">0%</div>
                    </div>
                    <div style="font-size: 12px; margin-top: 10px;">
                        Source: <span id="waterSource">Unknown</span>
                    </div>
                </div>
                
                <div class="stats">
                    <div class="stat">
                        <div class="stat-label">Power</div>
                        <div class="stat-value"><span id="power">0</span> <span class="stat-unit">W</span></div>
                    </div>
                    <div class="stat">
                        <div class="stat-label">Voltage</div>
                        <div class="stat-value"><span id="voltage">0</span> <span class="stat-unit">V</span></div>
                    </div>
                    <div class="stat">
                        <div class="stat-label">Current</div>
                        <div class="stat-value"><span id="current">0</span> <span class="stat-unit">A</span></div>
                    </div>
                    <div class="stat">
                        <div class="stat-label">Energy Today</div>
                        <div class="stat-value"><span id="energy">0</span> <span class="stat-unit">kWh</span></div>
                    </div>
                </div>
                
                <div class="mode-switch">
                    <button id="autoBtn" class="mode-btn" onclick="setMode('auto')">🤖 Auto Mode</button>
                    <button id="manualBtn" class="mode-btn" onclick="setMode('manual')">👆 Manual Mode</button>
                </div>
                
                <div class="pump-status">
                    <button id="pumpBtn" class="pump-btn stopped" onclick="togglePump()">
                        <span id="pumpText">PUMP OFF</span>
                    </button>
                    <div id="pumpReason" style="font-size: 11px; margin-top: 10px;"></div>
                </div>
                
                <div class="settings">
                    <div class="setting-row">
                        <span class="setting-label">Low Threshold (ON)</span>
                        <span class="setting-value"><span id="lowThresh">0</span>%</span>
                    </div>
                    <div class="setting-row">
                        <span class="setting-label">High Threshold (OFF)</span>
                        <span class="setting-value"><span id="highThresh">0</span>%</span>
                    </div>
                    <div class="setting-row">
                        <span class="setting-label">MQTT Status</span>
                        <span class="setting-value"><span id="mqttStatus">Disconnected</span></span>
                    </div>
                </div>
                
                <div class="info">
                    <div><span class="status-led" id="statusLed"></span> <span id="statusText">Updating...</span></div>
                    <div id="updateTime" style="margin-top: 5px;"></div>
                </div>
            </div>
            
            <!-- Settings View (hidden by default) -->
            <div id="settingsView" style="display: none;">
                <h3>WiFi Settings</h3>
                <input type="text" id="wifiSsid" placeholder="WiFi SSID">
                <input type="password" id="wifiPass" placeholder="WiFi Password">
                
                <h3>MQTT Settings</h3>
                <input type="text" id="mqttServer" placeholder="MQTT Server" value="broker.hivemq.com">
                <input type="number" id="mqttPort" placeholder="MQTT Port" value="1883">
                <input type="text" id="mqttTopic" placeholder="MQTT Topic" value="TankMonitor888/status">
                
                <h3>Pump Control</h3>
                <label>Low Threshold (Pump ON below %):</label>
                <input type="number" id="lowThreshold" step="5" value="30">
                <label>High Threshold (Pump OFF above %):</label>
                <input type="number" id="highThreshold" step="5" value="80">
                <label>Min Power to Detect Pump (Watts):</label>
                <input type="number" id="minPower" step="1" value="5">
                
                <button onclick="saveSettings()">Save Settings</button>
                <button onclick="resetWifi()">Reset WiFi Settings</button>
            </div>
            
            <!-- Statistics View (hidden by default) -->
            <div id="statsView" style="display: none;">
                <h3>Pump Statistics</h3>
                <div class="stat">
                    <div class="stat-label">Total Runtime Today</div>
                    <div class="stat-value" id="totalRuntime">0 min</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Energy Today</div>
                    <div class="stat-value" id="totalEnergy">0 kWh</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Pump Cycles Today</div>
                    <div class="stat-value" id="pumpCycles">0</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Last Pump Start</div>
                    <div class="stat-value" id="lastStart">Never</div>
                </div>
                <button onclick="resetStats()">Reset Statistics</button>
            </div>
        </div>
    </div>
    
    <script>
        let currentView = 'dashboard';
        
        function showDashboard() {
            currentView = 'dashboard';
            document.getElementById('dashboardView').style.display = 'block';
            document.getElementById('settingsView').style.display = 'none';
            document.getElementById('statsView').style.display = 'none';
            updateNavButtons();
        }
        
        function showSettings() {
            currentView = 'settings';
            document.getElementById('dashboardView').style.display = 'none';
            document.getElementById('settingsView').style.display = 'block';
            document.getElementById('statsView').style.display = 'none';
            updateNavButtons();
            loadSettings();
        }
        
        function showStats() {
            currentView = 'stats';
            document.getElementById('dashboardView').style.display = 'none';
            document.getElementById('settingsView').style.display = 'none';
            document.getElementById('statsView').style.display = 'block';
            updateNavButtons();
            loadStats();
        }
        
        function updateNavButtons() {
            const btns = document.querySelectorAll('.nav-btn');
            btns.forEach(btn => btn.classList.remove('active'));
            if (currentView === 'dashboard') document.querySelector('.nav-btn:nth-child(1)').classList.add('active');
            if (currentView === 'settings') document.querySelector('.nav-btn:nth-child(2)').classList.add('active');
            if (currentView === 'stats') document.querySelector('.nav-btn:nth-child(3)').classList.add('active');
        }
        
        async function fetchData() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                document.getElementById('waterLevel').innerText = data.waterLevel.toFixed(1);
                document.getElementById('waterSource').innerText = data.waterSource || 'Unknown';
                document.getElementById('levelBar').style.width = data.waterLevel + '%';
                document.getElementById('levelBar').innerHTML = data.waterLevel.toFixed(0) + '%';
                document.getElementById('power').innerText = data.power.toFixed(1);
                document.getElementById('voltage').innerText = data.voltage.toFixed(1);
                document.getElementById('current').innerText = data.current.toFixed(2);
                document.getElementById('energy').innerText = data.energy.toFixed(2);
                document.getElementById('lowThresh').innerText = data.lowThreshold;
                document.getElementById('highThresh').innerText = data.highThreshold;
                
                const pumpBtn = document.getElementById('pumpBtn');
                const pumpText = document.getElementById('pumpText');
                const pumpReason = document.getElementById('pumpReason');
                
                if (data.autoMode) {
                    document.getElementById('autoBtn').className = 'mode-btn active';
                    document.getElementById('manualBtn').className = 'mode-btn inactive';
                } else {
                    document.getElementById('autoBtn').className = 'mode-btn inactive';
                    document.getElementById('manualBtn').className = 'mode-btn active';
                }
                
                if (data.pumpState) {
                    pumpBtn.className = 'pump-btn running';
                    pumpText.innerText = 'PUMP ON';
                    pumpReason.innerText = data.autoMode ? '🤖 Auto: ' + data.pumpReason : '👆 Manual: Pump ON';
                } else {
                    pumpBtn.className = 'pump-btn stopped';
                    pumpText.innerText = 'PUMP OFF';
                    pumpReason.innerText = data.autoMode ? '🤖 Auto: ' + data.pumpReason : '👆 Manual: Pump OFF';
                }
                
                if (data.autoMode && !data.pumpState && data.waterLevel <= data.lowThreshold) {
                    pumpReason.innerText = '⚠️ ' + pumpReason.innerText;
                }
                
                document.getElementById('mqttStatus').innerHTML = data.mqttConnected ? '✅ Connected' : '❌ Disconnected';
                document.getElementById('statusLed').className = 'status-led ' + (data.mqttConnected ? 'status-online' : 'status-offline');
                document.getElementById('statusText').innerHTML = data.mqttConnected ? 'MQTT Connected' : 'MQTT Disconnected';
                document.getElementById('updateTime').innerHTML = 'Updated: ' + new Date().toLocaleTimeString();
                
            } catch(e) {
                console.error('Fetch error:', e);
            }
        }
        
        async function loadSettings() {
            try {
                const res = await fetch('/config');
                const config = await res.json();
                document.getElementById('wifiSsid').value = config.wifi_ssid;
                document.getElementById('mqttServer').value = config.mqtt_server;
                document.getElementById('mqttPort').value = config.mqtt_port;
                document.getElementById('mqttTopic').value = config.mqtt_topic;
                document.getElementById('lowThreshold').value = config.low_threshold;
                document.getElementById('highThreshold').value = config.high_threshold;
                document.getElementById('minPower').value = config.min_power;
            } catch(e) {}
        }
        
        async function loadStats() {
            try {
                const res = await fetch('/stats');
                const stats = await res.json();
                document.getElementById('totalRuntime').innerText = stats.total_runtime + ' min';
                document.getElementById('totalEnergy').innerText = stats.total_energy + ' kWh';
                document.getElementById('pumpCycles').innerText = stats.pump_cycles;
                document.getElementById('lastStart').innerText = stats.last_start || 'Never';
            } catch(e) {}
        }
        
        async function saveSettings() {
            const settings = {
                wifi_ssid: document.getElementById('wifiSsid').value,
                wifi_password: document.getElementById('wifiPass').value,
                mqtt_server: document.getElementById('mqttServer').value,
                mqtt_port: parseInt(document.getElementById('mqttPort').value),
                mqtt_topic: document.getElementById('mqttTopic').value,
                low_threshold: parseFloat(document.getElementById('lowThreshold').value),
                high_threshold: parseFloat(document.getElementById('highThreshold').value),
                min_power: parseFloat(document.getElementById('minPower').value)
            };
            
            const response = await fetch('/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(settings)
            });
            
            if (response.ok) {
                alert('Settings saved! Device will restart in 5 seconds...');
                setTimeout(() => location.reload(), 5000);
            }
        }
        
        async function resetWifi() {
            if (confirm('Reset WiFi settings and enter AP mode?')) {
                await fetch('/resetwifi');
                alert('Resetting... Device will restart in AP mode');
                setTimeout(() => location.reload(), 3000);
            }
        }
        
        async function setMode(mode) {
            await fetch('/mode?mode=' + mode);
            setTimeout(fetchData, 200);
        }
        
        async function togglePump() {
            const res = await fetch('/data');
            const data = await res.json();
            if (data.autoMode) {
                alert('Cannot manually control in Auto Mode. Please switch to Manual Mode first.');
                return;
            }
            await fetch('/toggle');
            setTimeout(fetchData, 200);
        }
        
        async function resetStats() {
            if (confirm('Reset pump statistics?')) {
                await fetch('/resetstats');
                setTimeout(loadStats, 500);
            }
        }
        
        setInterval(fetchData, 2000);
        fetchData();
        if (currentView === 'settings') loadSettings();
        if (currentView === 'stats') loadStats();
    </script>
</body>
</html>
)rawliteral";

const char ap_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Smart Pump - WiFi Setup</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            padding: 30px;
            max-width: 400px;
            width: 100%;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        h1 { text-align: center; color: #333; margin-bottom: 10px; }
        .subtitle { text-align: center; color: #666; margin-bottom: 30px; }
        input {
            width: 100%;
            padding: 12px;
            margin: 10px 0;
            border: 1px solid #ddd;
            border-radius: 8px;
            font-size: 14px;
        }
        button {
            width: 100%;
            padding: 12px;
            background: #667eea;
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            cursor: pointer;
            margin-top: 10px;
        }
        button:hover { background: #5a67d8; }
        .info {
            margin-top: 20px;
            padding: 10px;
            background: #f0f0f0;
            border-radius: 8px;
            font-size: 12px;
            color: #666;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>💧 Smart Pump Controller</h1>
        <div class="subtitle">WiFi Configuration</div>
        <form action="/connect" method="POST">
            <input type="text" name="ssid" placeholder="WiFi SSID" required>
            <input type="password" name="password" placeholder="WiFi Password">
            <button type="submit">Connect</button>
        </form>
        <div class="info">
            <strong>📡 Available Networks:</strong><br>
            <span id="networks">Scanning...</span>
        </div>
    </div>
    <script>
        async function scanNetworks() {
            const response = await fetch('/scan');
            const networks = await response.json();
            const netList = document.getElementById('networks');
            netList.innerHTML = networks.map(n => n.ssid).join('<br>');
        }
        scanNetworks();
        setInterval(scanNetworks, 10000);
    </script>
</body>
</html>
)rawliteral";

// ========== Pump Statistics ==========
struct PumpStats {
  unsigned long totalRuntimeSeconds = 0;
  float totalEnergyKwh = 0;
  int pumpCycles = 0;
  unsigned long lastStartTime = 0;
  char lastStartStr[32] = "Never";
} pumpStats;

// ========== Function Prototypes ==========
void saveConfig();
void loadConfig();
void setupAPMode();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();
void controlPump();
void updateStats();

// ========== Setup ==========
void setup() {
  // Initialize EEPROM
  EEPROM.begin(512);
  loadConfig();
  
  // Initialize Sonoff S31
  pinMode(CSE7766_RX_PIN, INPUT);
  delay(500);
  s31.begin(4800);
  
  // Generate device name
  uint32_t chipId = ESP.getChipId();
  deviceName = "s31-pump-" + String(chipId & 0xFFFF, HEX);
  deviceName.toLowerCase();
  apSSID = "SmartPump-" + String(chipId & 0xFFFF, HEX);
  
  s31.update();
  
  // Try to connect to WiFi
  if (strlen(config.ssid) > 0) {
    WiFi.hostname(deviceName);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
  }
  
  // If no WiFi, start AP mode
  if (WiFi.status() != WL_CONNECTED) {
    setupAPMode();
  } else {
    // Setup MQTT
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setCallback(mqttCallback);
  }
  
  // Setup Web Server
  setupWebServer();
  
  // Setup OTA
  ArduinoOTA.setHostname(deviceName.c_str());
  ArduinoOTA.setPassword("admin123");
  ArduinoOTA.begin();
  
  // Setup mDNS
  MDNS.begin(deviceName.c_str());
  MDNS.addService("http", "tcp", 80);
  
  server.begin();
}

void setupAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), "12345678");
  
  Serial.begin(115200);
  Serial.println("AP Mode Active");
  Serial.print("Connect to: ");
  Serial.println(apSSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    if (apMode) {
      server.send_P(200, "text/html", ap_html);
    } else {
      server.send_P(200, "text/html", index_html);
    }
  });
  
  server.on("/data", HTTP_GET, []() {
    s31.update();
    PowerData powerData = s31.getPowerData();
    
    StaticJsonDocument<512> doc;
    doc["waterLevel"] = currentWaterLevel;
    doc["waterSource"] = waterSource;
    doc["power"] = powerData.power;
    doc["voltage"] = powerData.voltage;
    doc["current"] = powerData.current;
    doc["energy"] = powerData.energy;
    doc["pumpState"] = powerData.relayState;
    doc["autoMode"] = config.auto_mode;
    doc["lowThreshold"] = config.low_threshold;
    doc["highThreshold"] = config.high_threshold;
    doc["mqttConnected"] = mqtt.connected();
    
    // Determine pump reason
    String reason = "";
    if (!config.auto_mode) {
      reason = "Manual Control";
    } else if (powerData.relayState) {
      reason = "Water level low (" + String(currentWaterLevel) + "% < " + String(config.low_threshold) + "%)";
    } else {
      if (currentWaterLevel >= config.high_threshold) {
        reason = "Tank full (" + String(currentWaterLevel) + "% ≥ " + String(config.high_threshold) + "%)";
      } else if (currentWaterLevel <= config.low_threshold && !powerData.relayState) {
        reason = "⚠️ DRY RUN PROTECTION - Waiting " + String(config.pump_protection_time) + "s";
      } else {
        reason = "Level OK (" + String(currentWaterLevel) + "%)";
      }
    }
    doc["pumpReason"] = reason;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/config", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["wifi_ssid"] = config.ssid;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_topic"] = config.mqtt_topic;
    doc["low_threshold"] = config.low_threshold;
    doc["high_threshold"] = config.high_threshold;
    doc["min_power"] = config.min_power_threshold;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/config", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, server.arg("plain"));
      if (!error) {
        if (doc.containsKey("wifi_ssid")) strcpy(config.ssid, doc["wifi_ssid"]);
        if (doc.containsKey("wifi_password")) strcpy(config.password, doc["wifi_password"]);
        if (doc.containsKey("mqtt_server")) strcpy(config.mqtt_server, doc["mqtt_server"]);
        if (doc.containsKey("mqtt_port")) config.mqtt_port = doc["mqtt_port"];
        if (doc.containsKey("mqtt_topic")) strcpy(config.mqtt_topic, doc["mqtt_topic"]);
        if (doc.containsKey("low_threshold")) config.low_threshold = doc["low_threshold"];
        if (doc.containsKey("high_threshold")) config.high_threshold = doc["high_threshold"];
        if (doc.containsKey("min_power")) config.min_power_threshold = doc["min_power"];
        
        saveConfig();
        server.send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
      }
    }
    server.send(400, "text/plain", "Bad Request");
  });
  
  server.on("/mode", HTTP_GET, []() {
    if (server.hasArg("mode")) {
      String mode = server.arg("mode");
      config.auto_mode = (mode == "auto");
      saveConfig();
      if (!config.auto_mode) {
        // Stop pump when switching to manual
        s31.relayOff();
      }
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/toggle", HTTP_GET, []() {
    if (!config.auto_mode) {
      s31.relayToggle();
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/stats", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["total_runtime"] = pumpStats.totalRuntimeSeconds / 60;
    doc["total_energy"] = pumpStats.totalEnergyKwh;
    doc["pump_cycles"] = pumpStats.pumpCycles;
    doc["last_start"] = pumpStats.lastStartStr;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/resetstats", HTTP_GET, []() {
    pumpStats.totalRuntimeSeconds = 0;
    pumpStats.totalEnergyKwh = 0;
    pumpStats.pumpCycles = 0;
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/resetwifi", HTTP_GET, []() {
    memset(config.ssid, 0, sizeof(config.ssid));
    memset(config.password, 0, sizeof(config.password));
    saveConfig();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  });
  
  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanComplete();
    if (n == -2) {
      WiFi.scanNetworks(true);
      server.send(200, "application/json", "[]");
    } else if (n >= 0) {
      String json = "[";
      for (int i = 0; i < n; ++i) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\"}";
      }
      json += "]";
      server.send(200, "application/json", json);
      WiFi.scanDelete();
    } else {
      server.send(200, "application/json", "[]");
    }
  });
  
  server.on("/connect", HTTP_POST, []() {
    if (server.hasArg("ssid")) {
      strcpy(config.ssid, server.arg("ssid").c_str());
      if (server.hasArg("password")) {
        strcpy(config.password, server.arg("password").c_str());
      }
      saveConfig();
      server.send(200, "text/html", "<html><body><h1>Saved! Restarting...</h1></body></html>");
      delay(1000);
      ESP.restart();
    }
  });
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Parse MQTT message
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (!error) {
    if (doc.containsKey("level_percent")) {
      currentWaterLevel = doc["level_percent"];
    }
    if (doc.containsKey("distance_cm")) {
      currentDistance = doc["distance_cm"];
    }
    if (doc.containsKey("volume_liters")) {
      currentVolume = doc["volume_liters"];
    }
    if (doc.containsKey("water_source")) {
      waterSource = doc["water_source"].as<String>();
    }
    if (doc.containsKey("battery")) {
      batteryLevel = doc["battery"];
    }
    
    mqttDataValid = true;
    lastMqttData = millis();
  }
}

void reconnectMQTT() {
  if (!mqtt.connected() && !apMode && WiFi.status() == WL_CONNECTED) {
    if (mqtt.connect(deviceName.c_str())) {
      mqtt.subscribe(config.mqtt_topic);
    }
  }
}

void controlPump() {
  if (!config.auto_mode) return;
  
  PowerData powerData = s31.getPowerData();
  bool currentState = powerData.relayState;
  bool shouldPumpOn = false;
  String reason = "";
  
  // Check if MQTT data is stale (older than 5 minutes)
  bool dataStale = (millis() - lastMqttData > 300000);
  
  if (dataStale || !mqttDataValid) {
    // Stop pump if no data for safety
    shouldPumpOn = false;
    reason = "No water level data";
  } else if (currentWaterLevel <= config.low_threshold) {
    // Check dry run protection
    if (currentState) {
      // Pump is running, check if it's been running too long without filling
      unsigned long runtime = (millis() - pumpStartTime) / 1000;
      if (runtime > config.pump_protection_time && powerData.power < config.min_power_threshold) {
        // Pump is running but no power draw - likely dry run
        shouldPumpOn = false;
        reason = "DRY RUN PROTECTION - No water";
      } else {
        shouldPumpOn = true;
        reason = "Water level low";
      }
    } else {
      shouldPumpOn = true;
      reason = "Water level low";
    }
  } else if (currentWaterLevel >= config.high_threshold) {
    shouldPumpOn = false;
    reason = "Tank full";
  } else {
    // Maintain current state if between thresholds
    shouldPumpOn = currentState;
    reason = currentState ? "Filling tank" : "Level OK";
  }
  
  // Apply pump control
  if (shouldPumpOn && !currentState) {
    s31.relayOn();
    pumpStartTime = millis();
    pumpStats.pumpCycles++;
    // Update last start time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    strftime(pumpStats.lastStartStr, sizeof(pumpStats.lastStartStr), "%H:%M:%S", timeinfo);
  } else if (!shouldPumpOn && currentState) {
    s31.relayOff();
  }
}

void updateStats() {
  static unsigned long lastStatsUpdate = 0;
  static bool lastPumpState = false;
  
  if (millis() - lastStatsUpdate >= 60000) { // Update every minute
    PowerData powerData = s31.getPowerData();
    bool currentPumpState = powerData.relayState;
    
    if (currentPumpState) {
      // Pump is running
      pumpStats.totalRuntimeSeconds += 60;
      pumpStats.totalEnergyKwh += (powerData.power / 1000.0) * (1.0 / 60.0);
    }
    
    lastStatsUpdate = millis();
  }
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, config);
  // Check if config is valid (simple validation)
  if (config.low_threshold == 0 && config.high_threshold == 0) {
    // Set defaults
    strcpy(config.mqtt_server, "broker.hivemq.com");
    config.mqtt_port = 1883;
    strcpy(config.mqtt_topic, "TankMonitor888/status");
    config.low_threshold = 30.0;
    config.high_threshold = 80.0;
    config.auto_mode = true;
    config.min_power_threshold = 5.0;
    config.pump_protection_time = 300;
  }
}

// ========== Main Loop ==========
void loop() {
  s31.update();
  if (!apMode) {
    if (WiFi.status() != WL_CONNECTED) {
      // Try to reconnect
      WiFi.begin(config.ssid, config.password);
    } else {
      // Handle MQTT
      if (!mqtt.connected()) {
        reconnectMQTT();
      } else {
        mqtt.loop();
      }
      
      // Update sensor and control pump
      //s31.update();
      controlPump();
      updateStats();
    }
  }
  
  // Handle web server and OTA
  server.handleClient();
  ArduinoOTA.handle();
  MDNS.update();
  
  delay(10);
}