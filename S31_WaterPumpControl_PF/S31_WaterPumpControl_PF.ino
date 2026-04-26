/*
 * Sonoff S31 Smart Pump Controller - Complete Configuration UI
 * Version: 2.9.1 - Power Factor Correction (PF=0 when no load)
 * Features:
 * - Power Factor (PF) and Apparent Power (VA) monitoring
 * - PF = 0 when pump OFF or no load (corrected)
 * - MQTT Subscribe only with test message publisher
 * - Display full incoming MQTT payload in web UI
 * - Parse TankMonitor888/status payload format
 * - AP mode with 10-minute timeout and auto-reboot
 * - Real-time pump control with water level monitoring
 */

// ========== MQTT Buffer Configuration ==========
#define MQTT_MAX_PACKET_SIZE 2048
#define MQTT_KEEPALIVE 30

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SonoffS31.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <espnow.h>

// ========== Debug Configuration ==========
#define DEBUG false

// ========== Timing Constants ==========
#define S31_UPDATE_INTERVAL     100
#define MQTT_RECONNECT_INTERVAL 5000
#define CONTROL_INTERVAL        500
#define WEB_SERVER_INTERVAL     10
#define OTA_INTERVAL            50
#define MDNS_INTERVAL           1000
#define AQUIRE_TIMEOUT          100
#define AP_MODE_TIMEOUT         600000
#define MAX_WIFI_ATTEMPTS       20

// ========== Configuration Structure ==========
struct Config {
  uint32_t magic = 0xDEADBEEF;
  char ssid[32] = "";
  char password[32] = "";
  char mqtt_server[40] = "broker.hivemq.com";
  int mqtt_port = 1883;
  char mqtt_topic[64] = "TankMonitor888/status";
  char mqtt_publish_topic[64] = "TankMonitor888/command";
  float low_threshold = 30.0;
  float high_threshold = 80.0;
  bool auto_mode = true;
  float min_power_threshold = 5.0;
  unsigned long pump_protection_time = 300;
  
  uint8_t peer_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  char peer_mac_str[18] = "FF:FF:FF:FF:FF:FF";
  bool use_espnow = false;
  int espnow_channel = 1;
} config;

// ========== Pin Definitions ==========
#define RELAY_PIN 12

// ========== Global Objects ==========
SonoffS31 s31(RELAY_PIN);
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ========== Global Variables ==========
String deviceName = "s31-pump";
String apSSID = "";
bool apMode = false;
unsigned long apModeStartTime = 0;

unsigned long lastS31Update = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastControlCheck = 0;
unsigned long lastWebServer = 0;
unsigned long lastOTA = 0;
unsigned long lastMDNS = 0;

// Sensor data from MQTT
float currentWaterLevel = 0;
float currentDistance = 0;
float currentVolume = 0;
float batteryVoltage = 0;
float batteryPercentage = 0;
String batteryStatus = "Unknown";
String sensorDeviceId = "";
String sensorIpAddress = "";
String sensorTimestamp = "";
String sensorNextSleep = "";
String sensorNextOnline = "";
String sensorUptime = "";
unsigned long lastMqttData = 0;
bool mqttDataValid = false;

// Store last received MQTT payload
String lastMqttPayload = "";
String lastMqttTopic = "";

// ESP-NOW Variables
bool espnow_initialized = false;
unsigned long lastEspNowData = 0;
struct SensorData {
  float level_percent;
  float distance_cm;
  float volume_liters;
  char device_id[32];
  unsigned long timestamp;
};

// Pump Statistics
struct PumpStats {
  unsigned long totalRuntimeSeconds = 0;
  float totalEnergyKwh = 0;
  int pumpCycles = 0;
  char lastStartStr[32] = "Never";
  unsigned long lastPumpOnTime = 0;
  bool wasRunning = false;
} pumpStats;

// Power Quality Variables
float powerFactor = 0.0;        // Start at 0 (no load)
float apparentPower = 0.0;
float reactivePower = 0.0;
bool lastRelayState = false;

Config config_backup;

// ========== Debug Macros ==========
#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// ========== HTML UI Pages ==========
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
            max-width: 900px;
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
        .warning-banner {
            background: #FEF3C7;
            color: #92400E;
            padding: 10px;
            border-radius: 10px;
            margin-bottom: 15px;
            text-align: center;
            font-size: 12px;
            border-left: 4px solid #F59E0B;
        }
        .nav-buttons {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            flex-wrap: wrap;
        }
        .nav-btn {
            flex: 1;
            background: #e5e7eb;
            color: #333;
            padding: 10px;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            font-weight: bold;
            transition: all 0.3s;
        }
        .nav-btn.active {
            background: #667eea;
            color: white;
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
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
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
            font-size: 20px;
            font-weight: bold;
            color: #333;
        }
        .pf-card {
            background: #f5f5f5;
            padding: 15px;
            border-radius: 12px;
            text-align: center;
            margin-bottom: 20px;
        }
        .pf-value {
            font-size: 28px;
            font-weight: bold;
        }
        .pf-good { color: #10B981; }
        .pf-warning { color: #F59E0B; }
        .pf-bad { color: #EF4444; }
        .pf-idle { color: #9CA3AF; }
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
        }
        .pump-btn.running { background: #10B981; color: white; animation: pulse 2s infinite; }
        .pump-btn.stopped { background: #EF4444; color: white; }
        @keyframes pulse {
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
        }
        .mode-btn.active { background: #667eea; color: white; }
        .mode-btn.inactive { background: #e5e7eb; color: #666; }
        .config-section {
            margin-top: 20px;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 12px;
        }
        .config-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            font-weight: bold;
            margin-bottom: 5px;
            color: #333;
        }
        input, select, textarea {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 8px;
            font-size: 14px;
        }
        textarea {
            font-family: monospace;
            resize: vertical;
        }
        button {
            background: #667eea;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 10px;
            cursor: pointer;
            font-weight: bold;
            margin-top: 10px;
            margin-right: 10px;
        }
        button:hover { background: #5a67d8; }
        button.danger { background: #EF4444; }
        button.danger:hover { background: #dc2626; }
        button.success { background: #10B981; }
        button.warning { background: #F59E0B; }
        .info-text {
            font-size: 11px;
            color: #666;
            margin-top: 5px;
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
        .mac-address {
            font-family: monospace;
            font-size: 14px;
            background: #f0f0f0;
            padding: 8px;
            border-radius: 5px;
            text-align: center;
        }
        .sensor-info {
            background: #f0fdf4;
            border-left: 4px solid #10B981;
            padding: 10px;
            border-radius: 8px;
            margin-top: 10px;
            font-size: 11px;
        }
        .mqtt-payload {
            background: #1e1e1e;
            color: #d4d4d4;
            padding: 10px;
            border-radius: 8px;
            font-family: monospace;
            font-size: 11px;
            overflow-x: auto;
            margin-top: 10px;
            white-space: pre-wrap;
            word-break: break-all;
        }
        @media (max-width: 600px) {
            .stats-grid { grid-template-columns: repeat(2, 1fr); }
            .nav-buttons { flex-direction: column; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>💧 Smart Pump Controller v2.9</h1>
            <div class="subtitle" id="deviceInfo">Loading...</div>
            
            <div id="apWarning" class="warning-banner" style="display:none;">
                ⚠️ AP MODE ACTIVE - Device will reboot in <span id="apCountdown">10:00</span>
            </div>
            
            <div class="nav-buttons">
                <button class="nav-btn active" onclick="showSection('dashboard')">Dashboard</button>
                <button class="nav-btn" onclick="showSection('wifi')">WiFi</button>
                <button class="nav-btn" onclick="showSection('mqtt')">MQTT</button>
                <button class="nav-btn" onclick="showSection('pump')">Pump</button>
                <button class="nav-btn" onclick="showSection('stats')">Statistics</button>
            </div>
            
            <!-- Dashboard Section -->
            <div id="dashboardSection">
                <div class="water-level">
                    <div>Current Water Level</div>
                    <div class="level-value"><span id="waterLevel">0</span><span>%</span></div>
                    <div class="level-bar-container">
                        <div class="level-bar" id="levelBar" style="width: 0%">0%</div>
                    </div>
                    <div style="font-size: 12px; margin-top: 5px;">Distance: <span id="distance">0</span> cm | Volume: <span id="volume">0</span> L</div>
                </div>
                
                <div class="stats-grid">
                    <div class="stat"><div class="stat-label">Real Power</div><div class="stat-value"><span id="power">0</span> W</div></div>
                    <div class="stat"><div class="stat-label">Apparent Power</div><div class="stat-value"><span id="apparentPower">0</span> VA</div></div>
                    <div class="stat"><div class="stat-label">Reactive Power</div><div class="stat-value"><span id="reactivePower">0</span> VAR</div></div>
                    <div class="stat"><div class="stat-label">Voltage</div><div class="stat-value"><span id="voltage">0</span> V</div></div>
                    <div class="stat"><div class="stat-label">Current</div><div class="stat-value"><span id="current">0</span> A</div></div>
                    <div class="stat"><div class="stat-label">Energy</div><div class="stat-value"><span id="energy">0</span> kWh</div></div>
                </div>
                
                <div class="pf-card" id="pfCard">
                    <div class="stat-label">Power Factor</div>
                    <div class="pf-value" id="pfValue">
                        <span id="powerFactor">0.00</span>
                    </div>
                    <div id="pfQuality" style="font-size: 11px; margin-top: 5px;"></div>
                </div>
                
                <div class="mode-switch">
                    <button id="autoBtn" class="mode-btn active" onclick="setMode('auto')">🤖 Auto Mode</button>
                    <button id="manualBtn" class="mode-btn inactive" onclick="setMode('manual')">👆 Manual Mode</button>
                </div>
                
                <button id="pumpBtn" class="pump-btn stopped" onclick="togglePump()">
                    <span id="pumpText">PUMP OFF</span>
                </button>
                <div id="pumpReason" style="text-align: center; font-size: 12px; margin-top: 10px;"></div>
                
                <div class="config-section">
                    <div><span class="status-led" id="wifiLed"></span> WiFi: <span id="wifiStatus">-</span></div>
                    <div><span class="status-led" id="mqttLed"></span> MQTT: <span id="mqttStatus">-</span></div>
                    <div>Data Source: <span id="dataSource">-</span></div>
                    <div id="lastUpdate" style="margin-top: 10px; font-size: 11px; color: #666;"></div>
                </div>
            </div>
            
            <!-- WiFi Section -->
            <div id="wifiSection" style="display:none;">
                <h3>WiFi Configuration</h3>
                <div class="config-group">
                    <label>WiFi SSID</label>
                    <input type="text" id="wifiSsid" placeholder="Enter WiFi SSID">
                </div>
                <div class="config-group">
                    <label>WiFi Password</label>
                    <input type="password" id="wifiPass" placeholder="Enter WiFi Password">
                </div>
                <div class="config-group">
                    <label>Device Hostname</label>
                    <input type="text" id="hostname" placeholder="Device Name">
                </div>
                <button onclick="saveWiFi()">Save & Restart</button>
                <button class="danger" onclick="factoryReset()">Factory Reset</button>
                <button class="warning" onclick="rebootNow()">Reboot</button>
            </div>
            
            <!-- MQTT Section -->
            <div id="mqttSection" style="display:none;">
                <h3>MQTT Configuration</h3>
                <div class="config-group">
                    <label>MQTT Broker</label>
                    <input type="text" id="mqttServer" placeholder="broker.hivemq.com">
                </div>
                <div class="config-group">
                    <label>MQTT Port</label>
                    <input type="number" id="mqttPort" placeholder="1883">
                </div>
                <div class="config-group">
                    <label>MQTT Topic</label>
                    <input type="text" id="mqttTopic" placeholder="TankMonitor888/status">
                </div>
                <button onclick="saveMQTT()">Save MQTT Settings</button>
                
                <h4 style="margin-top: 20px;">📥 Last Received Message</h4>
                <div class="mqtt-payload" id="lastMqttPayload">No messages</div>
            </div>
            
            <!-- Pump Settings Section -->
            <div id="pumpSection" style="display:none;">
                <h3>Pump Control Settings</h3>
                <div class="config-group">
                    <label>Low Threshold (Pump ON below %)</label>
                    <input type="number" id="lowThreshold" step="5" placeholder="30">
                </div>
                <div class="config-group">
                    <label>High Threshold (Pump OFF above %)</label>
                    <input type="number" id="highThreshold" step="5" placeholder="80">
                </div>
                <div class="config-group">
                    <label>Minimum Power Detection (Watts)</label>
                    <input type="number" id="minPower" step="1" placeholder="5">
                </div>
                <button onclick="savePumpSettings()">Save Pump Settings</button>
            </div>
            
            <!-- Statistics Section -->
            <div id="statsSection" style="display:none;">
                <h3>Pump Statistics</h3>
                <div class="stats-grid">
                    <div class="stat"><div class="stat-label">Total Runtime</div><div class="stat-value" id="totalRuntime">0 min</div></div>
                    <div class="stat"><div class="stat-label">Total Energy</div><div class="stat-value" id="totalEnergy">0 kWh</div></div>
                    <div class="stat"><div class="stat-label">Pump Cycles</div><div class="stat-value" id="pumpCycles">0</div></div>
                </div>
                <button onclick="resetStats()">Reset Statistics</button>
            </div>
        </div>
    </div>
    
    <script>
        let currentSection = 'dashboard';
        
        function showSection(section) {
            currentSection = section;
            document.getElementById('dashboardSection').style.display = section === 'dashboard' ? 'block' : 'none';
            document.getElementById('wifiSection').style.display = section === 'wifi' ? 'block' : 'none';
            document.getElementById('mqttSection').style.display = section === 'mqtt' ? 'block' : 'none';
            document.getElementById('pumpSection').style.display = section === 'pump' ? 'block' : 'none';
            document.getElementById('statsSection').style.display = section === 'stats' ? 'block' : 'none';
            
            if (section === 'wifi') loadWiFiSettings();
            if (section === 'mqtt') loadMQTTSettings();
            if (section === 'pump') loadPumpSettings();
            if (section === 'stats') loadStats();
        }
        
        function getPFClass(pf, isRunning) {
            if (!isRunning) return 'pf-idle';
            if (pf >= 0.95) return 'pf-good';
            if (pf >= 0.8) return 'pf-warning';
            return 'pf-bad';
        }
        
        function getPFQuality(pf, isRunning) {
            if (!isRunning) return '⏸ Idle - No load (PF = 0)';
            if (pf >= 0.95) return '✓ Excellent - Near unity';
            if (pf >= 0.9) return '✓ Good - Efficient';
            if (pf >= 0.8) return '⚠ Fair - Some reactive power';
            return '✗ Poor - High reactive power';
        }
        
        async function loadWiFiSettings() {
            const config = await fetchConfig();
            document.getElementById('wifiSsid').value = config.wifi_ssid || '';
            document.getElementById('hostname').value = config.hostname || 's31-pump';
        }
        
        async function loadMQTTSettings() {
            const config = await fetchConfig();
            document.getElementById('mqttServer').value = config.mqtt_server || 'broker.hivemq.com';
            document.getElementById('mqttPort').value = config.mqtt_port || 1883;
            document.getElementById('mqttTopic').value = config.mqtt_topic || 'TankMonitor888/status';
        }
        
        async function loadPumpSettings() {
            const config = await fetchConfig();
            document.getElementById('lowThreshold').value = config.low_threshold || 30;
            document.getElementById('highThreshold').value = config.high_threshold || 80;
            document.getElementById('minPower').value = config.min_power || 5;
        }
        
        async function fetchConfig() {
            const response = await fetch('/config');
            return await response.json();
        }
        
        async function fetchData() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                document.getElementById('waterLevel').innerText = data.waterLevel.toFixed(1);
                document.getElementById('distance').innerText = data.distance.toFixed(1);
                document.getElementById('volume').innerText = data.volume.toFixed(0);
                document.getElementById('levelBar').style.width = data.waterLevel + '%';
                document.getElementById('levelBar').innerHTML = data.waterLevel.toFixed(0) + '%';
                
                document.getElementById('power').innerText = data.power.toFixed(1);
                document.getElementById('apparentPower').innerText = data.apparentPower.toFixed(1);
                document.getElementById('reactivePower').innerText = data.reactivePower.toFixed(1);
                document.getElementById('voltage').innerText = data.voltage.toFixed(1);
                document.getElementById('current').innerText = data.current.toFixed(3);
                document.getElementById('energy').innerText = data.energy.toFixed(2);
                
                const isRunning = data.pumpState;
                const pf = isRunning ? (data.powerFactor || 0) : 0;
                
                document.getElementById('powerFactor').innerText = pf.toFixed(3);
                document.getElementById('powerFactor').className = getPFClass(pf, isRunning);
                document.getElementById('pfQuality').innerHTML = getPFQuality(pf, isRunning);
                
                const pumpBtn = document.getElementById('pumpBtn');
                const pumpText = document.getElementById('pumpText');
                if (isRunning) {
                    pumpBtn.className = 'pump-btn running';
                    pumpText.innerText = 'PUMP ON';
                } else {
                    pumpBtn.className = 'pump-btn stopped';
                    pumpText.innerText = 'PUMP OFF';
                }
                document.getElementById('pumpReason').innerText = data.pumpReason;
                
                if (data.autoMode) {
                    document.getElementById('autoBtn').className = 'mode-btn active';
                    document.getElementById('manualBtn').className = 'mode-btn inactive';
                } else {
                    document.getElementById('autoBtn').className = 'mode-btn inactive';
                    document.getElementById('manualBtn').className = 'mode-btn active';
                }
                
                document.getElementById('wifiStatus').innerHTML = data.wifiConnected ? 'Connected' : 'Disconnected';
                document.getElementById('wifiLed').className = 'status-led ' + (data.wifiConnected ? 'status-online' : 'status-offline');
                document.getElementById('mqttStatus').innerHTML = data.mqttConnected ? 'Connected' : 'Disconnected';
                document.getElementById('mqttLed').className = 'status-led ' + (data.mqttConnected ? 'status-online' : 'status-offline');
                document.getElementById('dataSource').innerHTML = data.dataSource || 'MQTT';
                document.getElementById('lastUpdate').innerHTML = 'Updated: ' + new Date().toLocaleTimeString();
                document.getElementById('deviceInfo').innerHTML = data.hostname + ' | ' + data.ip;
                
                if (data.lastMqttPayload) {
                    document.getElementById('lastMqttPayload').innerHTML = data.lastMqttPayload;
                }
                
                if (data.apModeActive && data.apTimeRemaining) {
                    document.getElementById('apWarning').style.display = 'block';
                    const minutes = Math.floor(data.apTimeRemaining / 60);
                    const seconds = data.apTimeRemaining % 60;
                    document.getElementById('apCountdown').innerText = `${minutes.toString().padStart(2,'0')}:${seconds.toString().padStart(2,'0')}`;
                } else {
                    document.getElementById('apWarning').style.display = 'none';
                }
            } catch(e) { console.error('Fetch error:', e); }
        }
        
        async function loadStats() {
            try {
                const response = await fetch('/stats');
                const stats = await response.json();
                document.getElementById('totalRuntime').innerText = stats.total_runtime + ' min';
                document.getElementById('totalEnergy').innerText = stats.total_energy + ' kWh';
                document.getElementById('pumpCycles').innerText = stats.pump_cycles;
            } catch(e) {}
        }
        
        async function saveWiFi() {
            const settings = {
                wifi_ssid: document.getElementById('wifiSsid').value,
                wifi_password: document.getElementById('wifiPass').value,
                hostname: document.getElementById('hostname').value
            };
            await fetch('/config/wifi', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(settings)});
            alert('WiFi settings saved! Device will restart...');
            setTimeout(() => location.reload(), 3000);
        }
        
        async function saveMQTT() {
            const settings = {
                mqtt_server: document.getElementById('mqttServer').value,
                mqtt_port: parseInt(document.getElementById('mqttPort').value),
                mqtt_topic: document.getElementById('mqttTopic').value
            };
            await fetch('/config/mqtt', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(settings)});
            alert('MQTT settings saved!');
            location.reload();
        }
        
        async function savePumpSettings() {
            const settings = {
                low_threshold: parseFloat(document.getElementById('lowThreshold').value),
                high_threshold: parseFloat(document.getElementById('highThreshold').value),
                min_power: parseFloat(document.getElementById('minPower').value)
            };
            await fetch('/config/pump', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(settings)});
            alert('Pump settings saved!');
        }
        
        async function setMode(mode) {
            await fetch('/mode?mode=' + mode);
            setTimeout(fetchData, 200);
        }
        
        async function togglePump() {
            const data = await fetchConfig();
            if (data.auto_mode) {
                alert('Switch to Manual Mode first');
                return;
            }
            await fetch('/toggle');
            setTimeout(fetchData, 200);
        }
        
        async function factoryReset() {
            if (confirm('FACTORY RESET: This will erase ALL settings. Continue?')) {
                await fetch('/factoryreset');
                alert('Factory reset in progress...');
            }
        }
        
        async function rebootNow() {
            if (confirm('Reboot device?')) {
                await fetch('/reboot');
                alert('Rebooting...');
            }
        }
        
        async function resetStats() {
            if (confirm('Reset pump statistics?')) {
                await fetch('/resetstats');
                loadStats();
            }
        }
        
        setInterval(fetchData, 2000);
        setInterval(loadStats, 10000);
        fetchData();
        loadStats();
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
        }
        h1 { text-align: center; color: #333; }
        .warning {
            background: #FEF3C7;
            color: #92400E;
            padding: 10px;
            border-radius: 10px;
            margin: 10px 0;
            text-align: center;
        }
        .countdown {
            font-size: 24px;
            font-weight: bold;
            text-align: center;
            color: #F59E0B;
            margin: 10px 0;
        }
        input {
            width: 100%;
            padding: 12px;
            margin: 10px 0;
            border: 1px solid #ddd;
            border-radius: 8px;
        }
        button {
            width: 100%;
            padding: 12px;
            background: #667eea;
            color: white;
            border: none;
            border-radius: 8px;
            cursor: pointer;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>💧 Smart Pump Controller</h1>
        <div class="warning">⚠️ NO WiFi CONFIGURED<br>Device will reboot in <span id="countdown" class="countdown">10:00</span></div>
        <form action="/connect" method="POST">
            <input type="text" name="ssid" placeholder="WiFi SSID" required>
            <input type="password" name="password" placeholder="WiFi Password">
            <button type="submit">Connect</button>
        </form>
    </div>
    <script>
        let remainingSeconds = 600;
        function updateCountdown() {
            const minutes = Math.floor(remainingSeconds / 60);
            const seconds = remainingSeconds % 60;
            document.getElementById('countdown').innerText = `${minutes.toString().padStart(2,'0')}:${seconds.toString().padStart(2,'0')}`;
            remainingSeconds--;
        }
        setInterval(updateCountdown, 1000);
    </script>
</body>
</html>
)rawliteral";

// ========== Function Prototypes ==========
void saveConfig();
void loadConfig();
bool isConfigValid();
void factoryReset();
String macToString(const uint8_t* mac);
bool stringToMac(const String& macStr, uint8_t* mac);
void initEspNow();
void setupWebServer();
void reconnectMQTT();
void controlPump();
bool connectToWiFi();
void setupAPMode();
void checkAPModeTimeout();
void rebootDevice();
void updatePowerQuality();

// ========== Power Quality Calculation ==========
void updatePowerQuality() {
  float voltage = s31.getVoltage();
  float current = s31.getCurrent();
  float realPower = s31.getPower();
  bool relayState = s31.getRelayState();
  
  // Calculate Apparent Power (VA)
  apparentPower = voltage * current;
  
  // Power Factor Logic:
  // - If pump is OFF or real power is near zero: PF = 0 (no useful work)
  // - If pump is ON: PF = Real Power / Apparent Power
  if (!relayState || realPower < 0.5) {
    // Pump OFF or no significant load
    powerFactor = 0.0;
    apparentPower = 0.0;
    reactivePower = 0.0;
  } else {
    // Pump is running, calculate PF normally
    if (apparentPower > 0.01) {
      powerFactor = realPower / apparentPower;
      if (powerFactor < 0) powerFactor = 0;
      if (powerFactor > 1) powerFactor = 1;
    } else {
      powerFactor = 0.0;
    }
    
    // Calculate Reactive Power: VAR = sqrt(VA² - W²)
    float vaSquared = apparentPower * apparentPower;
    float wSquared = realPower * realPower;
    if (vaSquared > wSquared) {
      reactivePower = sqrt(vaSquared - wSquared);
    } else {
      reactivePower = 0;
    }
  }
  
  // Detect relay state change for logging
  if (relayState != lastRelayState) {
    lastRelayState = relayState;
    if (!relayState) {
      DEBUG_PRINTLN("Pump OFF - Power Factor set to 0");
    }
  }
  
  DEBUG_PRINT("Power Quality - State:");
  DEBUG_PRINT(relayState ? "ON" : "OFF");
  DEBUG_PRINT(" W:");
  DEBUG_PRINT(realPower);
  DEBUG_PRINT(" VA:");
  DEBUG_PRINT(apparentPower);
  DEBUG_PRINT(" PF:");
  DEBUG_PRINTLN(powerFactor);
}

// ========== EEPROM Functions ==========
bool isConfigValid() {
  return (config.magic == 0xDEADBEEF && 
          config.mqtt_port > 0 && 
          config.mqtt_port < 65535 &&
          config.low_threshold >= 0 &&
          config.high_threshold <= 100 &&
          config.low_threshold < config.high_threshold);
}

void saveConfig() { 
  EEPROM.begin(512);
  config.magic = 0xDEADBEEF;
  EEPROM.put(0, config);
  bool committed = EEPROM.commit();
  EEPROM.end();
  
  if (committed) {
    DEBUG_PRINTLN("✓ Config saved");
  } else {
    DEBUG_PRINTLN("✗ Config save failed");
  }
}

void loadConfig() { 
  EEPROM.begin(512);
  EEPROM.get(0, config);
  EEPROM.end();
  
  if (!isConfigValid()) {
    DEBUG_PRINTLN("Invalid config, using defaults");
    Config defaultConfig;
    config = defaultConfig;
    strcpy(config.mqtt_publish_topic, "TankMonitor888/command");
    saveConfig();
  }
  
  if (config.mqtt_port <= 0 || config.mqtt_port > 65535) config.mqtt_port = 1883;
  if (config.low_threshold >= config.high_threshold) {
    config.low_threshold = 30.0;
    config.high_threshold = 80.0;
  }
}

void factoryReset() {
  DEBUG_PRINTLN("Factory resetting...");
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
  EEPROM.end();
  
  Config defaultConfig;
  config = defaultConfig;
  saveConfig();
  
  delay(1000);
  ESP.restart();
}

void rebootDevice() {
  DEBUG_PRINTLN("Rebooting...");
  delay(500);
  ESP.restart();
}

// ========== Helper Functions ==========
String macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool stringToMac(const String& macStr, uint8_t* mac) {
  int values[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) == 6) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)values[i];
    return true;
  }
  return false;
}

// ========== WiFi Connection ==========
bool connectToWiFi() {
  if (strlen(config.ssid) == 0) return false;
  
  DEBUG_PRINT("Connecting to: ");
  DEBUG_PRINTLN(config.ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_ATTEMPTS) {
    delay(500);
    attempts++;
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN();
  
  return WiFi.status() == WL_CONNECTED;
}

void checkAPModeTimeout() {
  if (apMode && (millis() - apModeStartTime) >= AP_MODE_TIMEOUT) {
    DEBUG_PRINTLN("AP Mode timeout, rebooting...");
    rebootDevice();
  }
}

void setupAPMode() {
  apMode = true;
  apModeStartTime = millis();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), "12345678");
  DEBUG_PRINTLN("AP Mode: " + apSSID);
  DEBUG_PRINTLN("AP IP: 192.168.4.1");
}

// ========== ESP-NOW Functions ==========
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(SensorData)) {
    SensorData data;
    memcpy(&data, incomingData, sizeof(data));
    currentWaterLevel = data.level_percent;
    currentDistance = data.distance_cm;
    currentVolume = data.volume_liters;
    sensorDeviceId = String(data.device_id);
    lastEspNowData = millis();
    mqttDataValid = true;
  }
}

void initEspNow() {
  if (!config.use_espnow) return;
  
  if (esp_now_init() != 0) {
    DEBUG_PRINTLN("ESP-NOW init failed");
    return;
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(OnDataRecv);
  
  if (config.peer_mac[0] != 0xFF) {
    esp_now_add_peer(config.peer_mac, ESP_NOW_ROLE_SLAVE, config.espnow_channel, NULL, 0);
  }
  
  espnow_initialized = true;
}

// ========== MQTT Functions ==========
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  lastMqttTopic = String(topic);
  
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  lastMqttPayload = String(message);
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) return;
  
  if (doc.containsKey("level_percent")) currentWaterLevel = doc["level_percent"];
  if (doc.containsKey("distance_cm")) currentDistance = doc["distance_cm"];
  if (doc.containsKey("volume_liters")) currentVolume = doc["volume_liters"];
  if (doc.containsKey("battery_percentage")) batteryPercentage = doc["battery_percentage"];
  if (doc.containsKey("battery_status")) batteryStatus = doc["battery_status"].as<String>();
  
  lastMqttData = millis();
  mqttDataValid = true;
}

void reconnectMQTT() {
  if (mqtt.connected() || apMode || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttAttempt < MQTT_RECONNECT_INTERVAL) return;
  lastMqttAttempt = millis();
  
  DEBUG_PRINT("MQTT connecting...");
  
  if (mqtt.connect(deviceName.c_str())) {
    DEBUG_PRINTLN("connected");
    mqtt.subscribe(config.mqtt_topic);
  } else {
    DEBUG_PRINTLN("failed");
  }
}

// ========== Pump Control ==========
void controlPump() {
  if (!config.auto_mode) return;
  if (millis() - lastControlCheck < CONTROL_INTERVAL) return;
  lastControlCheck = millis();
  
  bool currentState = s31.getRelayState();
  bool shouldPumpOn = false;
  
  bool dataValid = (espnow_initialized && millis() - lastEspNowData < 30000) ||
                   (mqttDataValid && millis() - lastMqttData < 300000);
  
  if (!dataValid) {
    shouldPumpOn = false;
  } else if (currentWaterLevel <= config.low_threshold) {
    shouldPumpOn = true;
  } else if (currentWaterLevel >= config.high_threshold) {
    shouldPumpOn = false;
  } else {
    shouldPumpOn = currentState;
  }
  
  if (currentState && !pumpStats.wasRunning) {
    pumpStats.pumpCycles++;
    pumpStats.lastPumpOnTime = millis();
    pumpStats.wasRunning = true;
  } else if (!currentState && pumpStats.wasRunning) {
    unsigned long runtime = (millis() - pumpStats.lastPumpOnTime) / 1000;
    pumpStats.totalRuntimeSeconds += runtime;
    pumpStats.wasRunning = false;
  }
  
  if (shouldPumpOn != currentState) {
    s31.setRelay(shouldPumpOn);
  }
}

// ========== Web Server Setup ==========
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    if (apMode) server.send_P(200, "text/html", ap_html);
    else server.send_P(200, "text/html", index_html);
  });
  
  server.on("/data", HTTP_GET, []() {
    updatePowerQuality();
    
    StaticJsonDocument<2048> doc;
    doc["waterLevel"] = currentWaterLevel;
    doc["distance"] = currentDistance;
    doc["volume"] = currentVolume;
    doc["power"] = s31.getPower();
    doc["voltage"] = s31.getVoltage();
    doc["current"] = s31.getCurrent();
    doc["energy"] = s31.getEnergy();
    doc["powerFactor"] = powerFactor;
    doc["apparentPower"] = apparentPower;
    doc["reactivePower"] = reactivePower;
    doc["pumpState"] = s31.getRelayState();
    doc["autoMode"] = config.auto_mode;
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["mqttConnected"] = mqtt.connected();
    doc["apModeActive"] = apMode;
    doc["hostname"] = deviceName;
    doc["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["lastMqttPayload"] = lastMqttPayload;
    
    if (apMode) {
      unsigned long elapsed = millis() - apModeStartTime;
      unsigned long remaining = (AP_MODE_TIMEOUT > elapsed) ? (AP_MODE_TIMEOUT - elapsed) / 1000 : 0;
      doc["apTimeRemaining"] = remaining;
    }
    
    doc["dataSource"] = (espnow_initialized && millis() - lastEspNowData < 30000) ? "ESP-NOW" : 
                        (mqttDataValid && millis() - lastMqttData < 300000) ? "MQTT" : "No Data";
    
    String reason = "";
    if (!config.auto_mode) reason = "Manual Control";
    else if (s31.getRelayState()) reason = "Water level low (" + String(currentWaterLevel, 1) + "%)";
    else if (currentWaterLevel >= config.high_threshold) reason = "Tank full";
    else reason = "Level OK";
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
    doc["auto_mode"] = config.auto_mode;
    doc["hostname"] = deviceName;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/config/wifi", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("wifi_ssid")) strcpy(config.ssid, doc["wifi_ssid"]);
      if (doc.containsKey("wifi_password")) strcpy(config.password, doc["wifi_password"]);
      if (doc.containsKey("hostname")) deviceName = doc["hostname"].as<String>();
      saveConfig();
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    }
  });
  
  server.on("/config/mqtt", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("mqtt_server")) strcpy(config.mqtt_server, doc["mqtt_server"]);
      if (doc.containsKey("mqtt_port")) config.mqtt_port = doc["mqtt_port"];
      if (doc.containsKey("mqtt_topic")) strcpy(config.mqtt_topic, doc["mqtt_topic"]);
      saveConfig();
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    }
  });
  
  server.on("/config/pump", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("low_threshold")) config.low_threshold = doc["low_threshold"];
      if (doc.containsKey("high_threshold")) config.high_threshold = doc["high_threshold"];
      if (doc.containsKey("min_power")) config.min_power_threshold = doc["min_power"];
      saveConfig();
      server.send(200, "text/plain", "OK");
    }
  });
  
  server.on("/mode", HTTP_GET, []() {
    if (server.hasArg("mode")) {
      config.auto_mode = (server.arg("mode") == "auto");
      saveConfig();
      if (!config.auto_mode) s31.setRelay(false);
    }
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/toggle", HTTP_GET, []() {
    if (!config.auto_mode) s31.toggleRelay();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/stats", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["total_runtime"] = pumpStats.totalRuntimeSeconds / 60;
    doc["total_energy"] = pumpStats.totalEnergyKwh;
    doc["pump_cycles"] = pumpStats.pumpCycles;
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
  
  server.on("/factoryreset", HTTP_GET, []() {
    server.send(200, "text/plain", "Factory resetting...");
    delay(100);
    factoryReset();
  });
  
  server.on("/reboot", HTTP_GET, []() {
    server.send(200, "text/plain", "Rebooting...");
    delay(100);
    rebootDevice();
  });
  
  server.on("/info", HTTP_GET, []() {
    String json = "{\"mac\":\"" + WiFi.macAddress() + "\",\"ip\":\"" + 
                  (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"}";
    server.send(200, "application/json", json);
  });
  
  server.on("/connect", HTTP_POST, []() {
    if (server.hasArg("ssid")) {
      strcpy(config.ssid, server.arg("ssid").c_str());
      if (server.hasArg("password")) strcpy(config.password, server.arg("password").c_str());
      saveConfig();
      server.send(200, "text/html", "<html><body><h1>Saved! Restarting...</h1></body></html>");
      delay(1000);
      ESP.restart();
    }
  });
}

// ========== Main Setup ==========
void setup() {
  #if DEBUG
    Serial.begin(115200);
    delay(100);
    DEBUG_PRINTLN("\n=== Smart Pump v2.9.1 (PF=0 when idle) ===");
  #endif
  
  loadConfig();
  
  uint32_t chipId = ESP.getChipId();
  deviceName = "s31-pump-" + String(chipId & 0xFFFF, HEX);
  apSSID = "SmartPump-" + String(chipId & 0xFFFF, HEX);
  
  s31.begin();
  
  if (strlen(config.ssid) > 0) {
    WiFi.hostname(deviceName);
    if (connectToWiFi()) {
      DEBUG_PRINTLN("WiFi Connected: " + WiFi.localIP().toString());
      mqtt.setServer(config.mqtt_server, config.mqtt_port);
      mqtt.setCallback(mqttCallback);
      initEspNow();
    } else {
      setupAPMode();
    }
  } else {
    setupAPMode();
  }
  
  setupWebServer();
  ArduinoOTA.begin();
  if (!apMode) {
    MDNS.begin(deviceName.c_str());
  }
  server.begin();
  
  DEBUG_PRINTLN("Setup complete - " + deviceName);
  DEBUG_PRINTLN("Power Factor: 0 when pump OFF / no load");
}

// ========== Main Loop ==========
void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastS31Update >= S31_UPDATE_INTERVAL) {
    lastS31Update = currentMillis;
    s31.update();
    updatePowerQuality();
  }
  
  unsigned long startTime = micros();
  
  if (currentMillis - lastWebServer >= WEB_SERVER_INTERVAL) {
    lastWebServer = currentMillis;
    server.handleClient();
  }
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  if (currentMillis - lastOTA >= OTA_INTERVAL) {
    lastOTA = currentMillis;
    ArduinoOTA.handle();
  }
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  checkAPModeTimeout();
  
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) {
      reconnectMQTT();
    } else {
      mqtt.loop();
    }
    if (micros() - startTime > AQUIRE_TIMEOUT) return;
    
    controlPump();
    if (micros() - startTime > AQUIRE_TIMEOUT) return;
    
    if (currentMillis - lastMDNS >= MDNS_INTERVAL) {
      lastMDNS = currentMillis;
      MDNS.update();
    }
  } else if (apMode) {
    controlPump();
  }
  
  delay(1);
}