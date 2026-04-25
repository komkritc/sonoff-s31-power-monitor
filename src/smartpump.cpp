/*
 * Sonoff S31 Smart Pump Controller - Complete Configuration UI
 * Version: 2.8.0 - MQTT Subscribe with Test Publisher & Payload Display
 * Features:
 * - MQTT Subscribe only with test message publisher
 * - Display full incoming MQTT payload in web UI
 * - Parse TankMonitor888/status payload format
 * - AP mode with 10-minute timeout and auto-reboot
 * - Real-time pump control with water level monitoring
 */

// ========== MQTT Buffer Configuration ==========
// Place at top of file before ANY includes
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

// ========== Timing Constants (Non-blocking) ==========
#define S31_UPDATE_INTERVAL     100     // s31.update every 100ms (priority)
#define MQTT_RECONNECT_INTERVAL 5000    // MQTT reconnect every 5 seconds
#define CONTROL_INTERVAL        500     // Pump control check every 500ms
#define WEB_SERVER_INTERVAL     10      // Web server handle every 10ms
#define OTA_INTERVAL            50      // OTA handle every 50ms
#define MDNS_INTERVAL           1000    // MDNS update every second
#define AQUIRE_TIMEOUT          100     // Max time per loop iteration
#define AP_MODE_TIMEOUT         600000  // AP Mode timeout: 10 minutes (600,000 ms)
#define MAX_WIFI_ATTEMPTS       20      // Maximum WiFi connection attempts (10 seconds)

// ========== Configuration Structure with Magic Number ==========
struct Config {
  uint32_t magic = 0xDEADBEEF;
  char ssid[32] = "";
  char password[32] = "";
  char mqtt_server[40] = "broker.hivemq.com";
  int mqtt_port = 1883;
  char mqtt_topic[64] = "TankMonitor888/status";
  char mqtt_publish_topic[64] = "TankMonitor888/command";  // New: topic for sending test messages
  float low_threshold = 30.0;
  float high_threshold = 80.0;
  bool auto_mode = true;
  float min_power_threshold = 5.0;
  unsigned long pump_protection_time = 300;
  
  // ESP-NOW Configuration
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

// Timing variables
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

// Store last received MQTT payload for display
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

// EEPROM backup
Config config_backup;

// ========== Debug Logging ==========
#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

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
void publishTestMessage(String topic, String message);

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
        .status-warning { background: #F59E0B; }
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
        .test-mqtt-box {
            background: #f0f0f0;
            padding: 15px;
            border-radius: 8px;
            margin-top: 15px;
        }
        .inline-input {
            display: flex;
            gap: 10px;
            margin-bottom: 10px;
        }
        .inline-input input {
            flex: 2;
        }
        .inline-input button {
            flex: 0;
            margin-top: 0;
        }
        @media (max-width: 600px) {
            .stats-grid { grid-template-columns: 1fr; }
            .nav-buttons { flex-direction: column; }
            .inline-input { flex-direction: column; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>💧 Smart Pump Controller v1.04</h1>
            <div class="subtitle" id="deviceInfo">Loading...</div>
            
            <div id="apWarning" class="warning-banner" style="display:none;">
                ⚠️ AP MODE ACTIVE - Device will reboot in <span id="apCountdown">10:00</span> to search for WiFi
            </div>
            
            <div class="nav-buttons">
                <button class="nav-btn active" onclick="showSection('dashboard')">Dashboard</button>
                <button class="nav-btn" onclick="showSection('wifi')">WiFi</button>
                <button class="nav-btn" onclick="showSection('mqtt')">MQTT</button>
                <button class="nav-btn" onclick="showSection('espnow')">ESP-NOW</button>
                <button class="nav-btn" onclick="showSection('pump')">Pump Settings</button>
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
                    <div class="stat"><div class="stat-label">Power</div><div class="stat-value"><span id="power">0</span> W</div></div>
                    <div class="stat"><div class="stat-label">Voltage</div><div class="stat-value"><span id="voltage">0</span> V</div></div>
                    <div class="stat"><div class="stat-label">Current</div><div class="stat-value"><span id="current">0</span> A</div></div>
                    <div class="stat"><div class="stat-label">Energy</div><div class="stat-value"><span id="energy">0</span> kWh</div></div>
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
                    <div class="config-group">
                        <div><span class="status-led" id="wifiLed"></span> WiFi: <span id="wifiStatus">Disconnected</span></div>
                        <div><span class="status-led" id="mqttLed"></span> MQTT: <span id="mqttStatus">Disconnected</span></div>
                        <div><span class="status-led" id="espnowLed"></span> ESP-NOW: <span id="espnowStatus">Disabled</span></div>
                        <div>Data Source: <span id="dataSource">-</span></div>
                        <div id="lastUpdate" style="margin-top: 10px; font-size: 11px; color: #666;"></div>
                    </div>
                </div>
                
                <!-- Sensor Information Section -->
                <div class="sensor-info" id="sensorInfo" style="display:none;">
                    <strong>📡 Water Level Sensor:</strong><br>
                    Device: <span id="sensorDevice">-</span><br>
                    IP: <span id="sensorIP">-</span><br>
                    Battery: <span id="sensorBattery">-</span>% (<span id="sensorBatteryStatus">-</span>)<br>
                    Last Reading: <span id="sensorTimestamp">-</span><br>
                    Next Online: <span id="sensorNextOnline">-</span>
                </div>
            </div>
            
            <!-- WiFi Configuration Section -->
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
                    <div class="info-text">Access device at: http://[hostname].local</div>
                </div>
                <button onclick="scanWiFi()">Scan Networks</button>
                <button onclick="saveWiFi()">Save & Restart</button>
                <button class="danger" onclick="resetWiFi()">Reset WiFi Settings</button>
                <button class="danger" onclick="factoryReset()">Factory Reset All</button>
                <button class="warning" onclick="rebootNow()">Reboot Device</button>
                <div id="wifiScanResult" class="peer-list" style="margin-top: 10px;"></div>
            </div>
            
            <!-- MQTT Configuration Section with Test Publisher -->
            <div id="mqttSection" style="display:none;">
                <h3>MQTT Configuration (Subscribe Only)</h3>
                <div class="config-group">
                    <label>MQTT Broker</label>
                    <input type="text" id="mqttServer" placeholder="broker.hivemq.com">
                </div>
                <div class="config-group">
                    <label>MQTT Port</label>
                    <input type="number" id="mqttPort" placeholder="1883">
                </div>
                <div class="config-group">
                    <label>MQTT Subscribe Topic</label>
                    <input type="text" id="mqttTopic" placeholder="TankMonitor888/status">
                    <div class="info-text">Device subscribes to this topic for water level data</div>
                </div>
                <div class="config-group">
                    <label>MQTT Publish Topic (for testing)</label>
                    <input type="text" id="mqttPublishTopic" placeholder="TankMonitor888/command">
                    <div class="info-text">Topic used when sending test messages</div>
                </div>
                <button onclick="saveMQTT()">Save MQTT Settings</button>
                <button onclick="testMQTT()">Test Connection</button>
                
                <!-- Test Message Sender -->
                <div class="test-mqtt-box">
                    <h4>📤 Send Test MQTT Message</h4>
                    <div class="inline-input">
                        <input type="text" id="testTopic" placeholder="Topic (leave empty to use publish topic)">
                        <button onclick="sendTestMessage()">Send</button>
                    </div>
                    <textarea id="testMessage" rows="4" placeholder='Enter JSON message, e.g.:
{"level_percent":45.2,"distance_cm":25.5,"volume_liters":1250,"battery_percentage":85}'>{"level_percent":45.2,"distance_cm":25.5,"volume_liters":1250,"battery_percentage":85,"battery_status":"Good"}</textarea>
                    <div class="info-text">Send a test message via MQTT (device must be connected to broker)</div>
                </div>
                
                <!-- Incoming Payload Display -->
                <div style="margin-top: 20px;">
                    <h4>📥 Last Received MQTT Message</h4>
                    <div class="config-group">
                        <label>Topic: <span id="lastMqttTopic" style="color:#667eea;">-</span></label>
                        <div class="mqtt-payload" id="lastMqttPayload">No messages received yet</div>
                    </div>
                    <button onclick="clearPayloadDisplay()">Clear Display</button>
                </div>
                
                <div id="mqttTestResult" class="info-text" style="margin-top: 10px;"></div>
            </div>
            
            <!-- ESP-NOW Configuration Section -->
            <div id="espnowSection" style="display:none;">
                <h3>ESP-NOW Configuration</h3>
                <div class="config-group">
                    <label>Enable ESP-NOW</label>
                    <select id="useEspNow">
                        <option value="false">Disabled</option>
                        <option value="true">Enabled</option>
                    </select>
                    <div class="info-text">ESP-NOW allows direct communication with sensors without WiFi</div>
                </div>
                <div class="config-group">
                    <label>Sensor MAC Address</label>
                    <input type="text" id="peerMac" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17">
                    <div class="info-text">Enter the MAC address of your water level sensor</div>
                </div>
                <div class="config-group">
                    <label>WiFi Channel</label>
                    <input type="number" id="espnowChannel" min="1" max="13" placeholder="1">
                </div>
                <button onclick="addEspNowPeer()">Add/Update Peer</button>
                <button onclick="saveEspNow()">Save ESP-NOW Settings</button>
                
                <h3 style="margin-top: 20px;">This Device Info</h3>
                <div class="mac-address" id="deviceMac"></div>
            </div>
            
            <!-- Pump Settings Section -->
            <div id="pumpSection" style="display:none;">
                <h3>Pump Control Settings</h3>
                <div class="config-group">
                    <label>Low Threshold (Pump ON below %)</label>
                    <input type="number" id="lowThreshold" step="5" placeholder="30">
                    <div class="info-text">Pump turns ON when water level drops below this value</div>
                </div>
                <div class="config-group">
                    <label>High Threshold (Pump OFF above %)</label>
                    <input type="number" id="highThreshold" step="5" placeholder="80">
                    <div class="info-text">Pump turns OFF when water level reaches above this value</div>
                </div>
                <div class="config-group">
                    <label>Minimum Power Detection (Watts)</label>
                    <input type="number" id="minPower" step="1" placeholder="5">
                    <div class="info-text">Minimum power consumption to detect pump is running</div>
                </div>
                <div class="config-group">
                    <label>Dry Run Protection (seconds)</label>
                    <input type="number" id="dryRunProtection" step="30" placeholder="300">
                    <div class="info-text">Stop pump after this time if no power draw detected</div>
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
                    <div class="stat"><div class="stat-label">Last Pump Start</div><div class="stat-value" id="lastStart">Never</div></div>
                </div>
                <button onclick="resetStats()">Reset Statistics</button>
                <button onclick="exportStats()">Export Statistics</button>
            </div>
        </div>
    </div>
    
    <script>
        let currentSection = 'dashboard';
        let apModeActive = false;
        
        function showSection(section) {
            currentSection = section;
            const sections = ['dashboard', 'wifi', 'mqtt', 'espnow', 'pump', 'stats'];
            sections.forEach(s => {
                document.getElementById(s + 'Section').style.display = s === section ? 'block' : 'none';
            });
            document.querySelectorAll('.nav-btn').forEach((btn, idx) => {
                btn.classList.toggle('active', btn.textContent.toLowerCase().includes(section));
            });
            if (section === 'wifi') loadWiFiSettings();
            if (section === 'mqtt') loadMQTTSettings();
            if (section === 'espnow') loadEspNowSettings();
            if (section === 'pump') loadPumpSettings();
            if (section === 'stats') loadStats();
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
            document.getElementById('mqttPublishTopic').value = config.mqtt_publish_topic || 'TankMonitor888/command';
        }
        
        async function loadEspNowSettings() {
            const config = await fetchConfig();
            document.getElementById('useEspNow').value = config.use_espnow ? 'true' : 'false';
            document.getElementById('peerMac').value = config.peer_mac || 'FF:FF:FF:FF:FF:FF';
            document.getElementById('espnowChannel').value = config.espnow_channel || 1;
            
            const response = await fetch('/info');
            const info = await response.json();
            document.getElementById('deviceMac').innerHTML = info.mac || 'Unknown';
        }
        
        async function loadPumpSettings() {
            const config = await fetchConfig();
            document.getElementById('lowThreshold').value = config.low_threshold || 30;
            document.getElementById('highThreshold').value = config.high_threshold || 80;
            document.getElementById('minPower').value = config.min_power || 5;
            document.getElementById('dryRunProtection').value = config.dry_run_protection || 300;
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
                document.getElementById('voltage').innerText = data.voltage.toFixed(1);
                document.getElementById('current').innerText = data.current.toFixed(2);
                document.getElementById('energy').innerText = data.energy.toFixed(2);
                
                const pumpBtn = document.getElementById('pumpBtn');
                const pumpText = document.getElementById('pumpText');
                if (data.pumpState) {
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
                document.getElementById('espnowStatus').innerHTML = data.espnowActive ? 'Active' : 'Disabled';
                document.getElementById('espnowLed').className = 'status-led ' + (data.espnowActive ? 'status-online' : 'status-offline');
                document.getElementById('dataSource').innerHTML = data.dataSource || 'MQTT';
                document.getElementById('lastUpdate').innerHTML = 'Updated: ' + new Date().toLocaleTimeString();
                document.getElementById('deviceInfo').innerHTML = data.hostname + ' | ' + data.ip;
                
                // Update MQTT payload display if on MQTT section
                if (currentSection === 'mqtt' && data.lastMqttPayload) {
                    document.getElementById('lastMqttTopic').innerHTML = data.lastMqttTopic || '-';
                    document.getElementById('lastMqttPayload').innerHTML = data.lastMqttPayload;
                }
                
                // Show sensor info if available
                if (data.sensorInfo && data.sensorInfo.available) {
                    document.getElementById('sensorInfo').style.display = 'block';
                    document.getElementById('sensorDevice').innerHTML = data.sensorInfo.device || '-';
                    document.getElementById('sensorIP').innerHTML = data.sensorInfo.ip || '-';
                    document.getElementById('sensorBattery').innerHTML = data.sensorInfo.battery_percentage || '-';
                    document.getElementById('sensorBatteryStatus').innerHTML = data.sensorInfo.battery_status || '-';
                    document.getElementById('sensorTimestamp').innerHTML = data.sensorInfo.timestamp || '-';
                    document.getElementById('sensorNextOnline').innerHTML = data.sensorInfo.next_online || '-';
                } else {
                    document.getElementById('sensorInfo').style.display = 'none';
                }
                
                // Show AP mode warning
                if (data.apModeActive) {
                    document.getElementById('apWarning').style.display = 'block';
                    apModeActive = true;
                } else {
                    document.getElementById('apWarning').style.display = 'none';
                    apModeActive = false;
                }
                
                // Update countdown if in AP mode
                if (apModeActive && data.apTimeRemaining) {
                    const minutes = Math.floor(data.apTimeRemaining / 60);
                    const seconds = data.apTimeRemaining % 60;
                    document.getElementById('apCountdown').innerText = `${minutes.toString().padStart(2,'0')}:${seconds.toString().padStart(2,'0')}`;
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
                document.getElementById('lastStart').innerText = stats.last_start || 'Never';
            } catch(e) {}
        }
        
        async function scanWiFi() {
            const response = await fetch('/scan');
            const networks = await response.json();
            const html = '<h4>Available Networks:</h4>' + networks.map(n => 
                `<div class="peer-item" onclick="document.getElementById('wifiSsid').value='${n.ssid}'">${n.ssid}</div>`
            ).join('');
            document.getElementById('wifiScanResult').innerHTML = html;
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
                mqtt_topic: document.getElementById('mqttTopic').value,
                mqtt_publish_topic: document.getElementById('mqttPublishTopic').value
            };
            await fetch('/config/mqtt', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(settings)});
            alert('MQTT settings saved!');
            location.reload();
        }
        
        async function testMQTT() {
            const server = document.getElementById('mqttServer').value;
            const port = document.getElementById('mqttPort').value;
            const response = await fetch(`/testmqtt?server=${server}&port=${port}`);
            const result = await response.text();
            document.getElementById('mqttTestResult').innerHTML = result;
        }
        
        async function sendTestMessage() {
            let topic = document.getElementById('testTopic').value.trim();
            const publishTopic = document.getElementById('mqttPublishTopic').value;
            const message = document.getElementById('testMessage').value;
            
            if (!topic) {
                topic = publishTopic;
            }
            
            if (!topic) {
                alert('Please enter a topic or configure publish topic in settings');
                return;
            }
            
            if (!message) {
                alert('Please enter a message to send');
                return;
            }
            
            try {
                const response = await fetch('/mqtt/send', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({topic: topic, message: message})
                });
                const result = await response.text();
                document.getElementById('mqttTestResult').innerHTML = result;
                setTimeout(() => {
                    document.getElementById('mqttTestResult').innerHTML = '';
                }, 5000);
            } catch(e) {
                alert('Failed to send message');
            }
        }
        
        function clearPayloadDisplay() {
            document.getElementById('lastMqttPayload').innerHTML = 'No messages received yet';
            document.getElementById('lastMqttTopic').innerHTML = '-';
        }
        
        async function saveEspNow() {
            const settings = {
                use_espnow: document.getElementById('useEspNow').value === 'true',
                peer_mac: document.getElementById('peerMac').value,
                espnow_channel: parseInt(document.getElementById('espnowChannel').value)
            };
            await fetch('/config/espnow', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(settings)});
            alert('ESP-NOW settings saved!');
            location.reload();
        }
        
        async function addEspNowPeer() {
            const mac = document.getElementById('peerMac').value;
            await fetch(`/espnow/add?mac=${mac}`);
            alert('Peer added!');
        }
        
        async function savePumpSettings() {
            const settings = {
                low_threshold: parseFloat(document.getElementById('lowThreshold').value),
                high_threshold: parseFloat(document.getElementById('highThreshold').value),
                min_power: parseFloat(document.getElementById('minPower').value),
                dry_run_protection: parseInt(document.getElementById('dryRunProtection').value)
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
        
        async function resetWiFi() {
            if (confirm('Reset WiFi settings? Device will enter AP mode')) {
                await fetch('/resetwifi');
                alert('Resetting...');
                setTimeout(() => location.reload(), 3000);
            }
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
        
        async function exportStats() {
            window.location.href = '/exportstats';
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
            font-size: 14px;
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
        .info {
            margin-top: 20px;
            padding: 10px;
            background: #f0f0f0;
            border-radius: 8px;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>💧 Smart Pump Controller</h1>
        <div class="warning">
            ⚠️ NO WiFi CONFIGURED<br>
            Device will reboot in <span id="countdown" class="countdown">10:00</span> to retry
        </div>
        <p>Please configure your WiFi network:</p>
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
        let remainingSeconds = 600;
        
        function updateCountdown() {
            const minutes = Math.floor(remainingSeconds / 60);
            const seconds = remainingSeconds % 60;
            document.getElementById('countdown').innerText = `${minutes.toString().padStart(2,'0')}:${seconds.toString().padStart(2,'0')}`;
            
            if (remainingSeconds <= 0) {
                document.getElementById('countdown').innerText = "Rebooting...";
            }
            remainingSeconds--;
        }
        
        setInterval(updateCountdown, 1000);
        
        async function scanNetworks() {
            const response = await fetch('/scan');
            const networks = await response.json();
            document.getElementById('networks').innerHTML = networks.map(n => n.ssid).join('<br>');
        }
        scanNetworks();
        setInterval(scanNetworks, 10000);
    </script>
</body>
</html>
)rawliteral";

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
  
  Config verifyConfig;
  EEPROM.get(0, verifyConfig);
  EEPROM.end();
  
  if (committed && verifyConfig.magic == 0xDEADBEEF) {
    DEBUG_PRINTLN("✓ Config saved successfully!");
  } else {
    DEBUG_PRINTLN("✗ ERROR: Config save failed!");
  }
}

void loadConfig() { 
  EEPROM.begin(512);
  EEPROM.get(0, config);
  EEPROM.end();
  
  if (!isConfigValid()) {
    DEBUG_PRINTLN("⚠ Invalid config detected! Using defaults...");
    
    if (config_backup.magic == 0xDEADBEEF && isConfigValid()) {
      config = config_backup;
      DEBUG_PRINTLN("Restored from backup");
    } else {
      Config defaultConfig;
      config = defaultConfig;
      DEBUG_PRINTLN("Using factory defaults");
      // Set default publish topic
      strcpy(config.mqtt_publish_topic, "TankMonitor888/command");
    }
    
    saveConfig();
  } else {
    DEBUG_PRINTLN("✓ Config loaded successfully");
    config_backup = config;
  }
  
  // Fix invalid values
  if (config.mqtt_port <= 0 || config.mqtt_port > 65535) config.mqtt_port = 1883;
  if (config.low_threshold >= config.high_threshold) {
    config.low_threshold = 30.0;
    config.high_threshold = 80.0;
  }
  if (config.espnow_channel < 1 || config.espnow_channel > 13) config.espnow_channel = 1;
  if (strlen(config.mqtt_publish_topic) == 0) {
    strcpy(config.mqtt_publish_topic, "TankMonitor888/command");
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
  
  DEBUG_PRINTLN("Factory reset complete");
  delay(1000);
  ESP.restart();
}

void rebootDevice() {
  DEBUG_PRINTLN("Rebooting device...");
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
  if (strlen(config.ssid) == 0) {
    DEBUG_PRINTLN("No SSID configured");
    return false;
  }
  
  DEBUG_PRINT("Connecting to WiFi: ");
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
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi connected!");
    DEBUG_PRINT("IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
    return true;
  } else {
    DEBUG_PRINTLN("WiFi connection failed!");
    return false;
  }
}

void checkAPModeTimeout() {
  if (apMode && (millis() - apModeStartTime) >= AP_MODE_TIMEOUT) {
    DEBUG_PRINTLN("AP Mode timeout reached (10 minutes), rebooting to search for WiFi...");
    rebootDevice();
  }
}

void setupAPMode() {
  apMode = true;
  apModeStartTime = millis();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), "12345678");
  DEBUG_PRINTLN("AP Mode active: " + apSSID);
  DEBUG_PRINTLN("AP IP: 192.168.4.1");
  DEBUG_PRINTLN("Will timeout and reboot in 10 minutes");
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
    DEBUG_PRINTLN("ESP-NOW: Level=" + String(currentWaterLevel, 1) + "%");
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
    DEBUG_PRINTLN("ESP-NOW peer: " + macToString(config.peer_mac));
  }
  
  espnow_initialized = true;
  DEBUG_PRINTLN("ESP-NOW initialized on channel " + String(config.espnow_channel));
}

// ========== MQTT Functions ==========
void publishTestMessage(String topic, String message) {
  if (!mqtt.connected()) {
    DEBUG_PRINTLN("MQTT not connected, cannot send test message");
    return;
  }
  
  if (mqtt.publish(topic.c_str(), message.c_str())) {
    DEBUG_PRINTLN("Test message sent to topic: " + topic);
    DEBUG_PRINTLN("Message: " + message);
  } else {
    DEBUG_PRINTLN("Failed to send test message");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Store the topic
  lastMqttTopic = String(topic);
  
  // Convert payload to string and store
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  lastMqttPayload = String(message);
  
  DEBUG_PRINTLN("MQTT Message received on topic: " + lastMqttTopic);
  DEBUG_PRINTLN("Full Payload: " + lastMqttPayload);
  
  // Parse JSON payload
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    DEBUG_PRINTLN("MQTT JSON parse failed: " + String(error.c_str()));
    return;
  }
  
  // Extract water level data from payload
  if (doc.containsKey("level_percent")) {
    currentWaterLevel = doc["level_percent"];
    DEBUG_PRINTLN("Water Level: " + String(currentWaterLevel, 1) + "%");
  }
  
  if (doc.containsKey("distance_cm")) {
    currentDistance = doc["distance_cm"];
    DEBUG_PRINTLN("Distance: " + String(currentDistance, 1) + "cm");
  }
  
  if (doc.containsKey("volume_liters")) {
    currentVolume = doc["volume_liters"];
    DEBUG_PRINTLN("Volume: " + String(currentVolume, 1) + "L");
  }
  
  if (doc.containsKey("battery_voltage")) {
    batteryVoltage = doc["battery_voltage"];
  }
  
  if (doc.containsKey("battery_percentage")) {
    batteryPercentage = doc["battery_percentage"];
  }
  
  if (doc.containsKey("battery_status")) {
    batteryStatus = doc["battery_status"].as<String>();
  }
  
  if (doc.containsKey("device")) {
    sensorDeviceId = doc["device"].as<String>();
  }
  
  if (doc.containsKey("ip_address")) {
    sensorIpAddress = doc["ip_address"].as<String>();
  }
  
  if (doc.containsKey("timestamp")) {
    sensorTimestamp = doc["timestamp"].as<String>();
  }
  
  if (doc.containsKey("next_online")) {
    sensorNextOnline = doc["next_online"].as<String>();
  }
  
  lastMqttData = millis();
  mqttDataValid = true;
  
  DEBUG_PRINTLN("--- Sensor Data Updated ---");
}

void reconnectMQTT() {
  if (mqtt.connected() || apMode || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttAttempt < MQTT_RECONNECT_INTERVAL) return;
  lastMqttAttempt = millis();
  
  DEBUG_PRINT("Attempting MQTT connection...");
  
  if (mqtt.connect(deviceName.c_str())) {
    DEBUG_PRINTLN("connected!");
    if (mqtt.subscribe(config.mqtt_topic)) {
      DEBUG_PRINTLN("Subscribed to: " + String(config.mqtt_topic));
    } else {
      DEBUG_PRINTLN("Subscribe failed!");
    }
  } else {
    DEBUG_PRINTLN("failed, state=" + String(mqtt.state()));
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
    pumpStats.totalEnergyKwh = s31.getEnergy();
    DEBUG_PRINTLN("Pump ON");
  } else if (!currentState && pumpStats.wasRunning) {
    unsigned long runtime = (millis() - pumpStats.lastPumpOnTime) / 1000;
    pumpStats.totalRuntimeSeconds += runtime;
    pumpStats.wasRunning = false;
    DEBUG_PRINTLN("Pump OFF (Ran for " + String(runtime) + "s)");
  }
  
  if (shouldPumpOn && !currentState) {
    s31.setRelay(true);
  } else if (!shouldPumpOn && currentState) {
    s31.setRelay(false);
  }
}

// ========== Web Server Setup ==========
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    if (apMode) server.send_P(200, "text/html", ap_html);
    else server.send_P(200, "text/html", index_html);
  });
  
  server.on("/data", HTTP_GET, []() {
    StaticJsonDocument<1536> doc;
    doc["waterLevel"] = currentWaterLevel;
    doc["distance"] = currentDistance;
    doc["volume"] = currentVolume;
    doc["power"] = s31.getPower();
    doc["voltage"] = s31.getVoltage();
    doc["current"] = s31.getCurrent();
    doc["energy"] = s31.getEnergy();
    doc["pumpState"] = s31.getRelayState();
    doc["autoMode"] = config.auto_mode;
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["mqttConnected"] = mqtt.connected();
    doc["espnowActive"] = espnow_initialized;
    doc["apModeActive"] = apMode;
    doc["hostname"] = deviceName;
    doc["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["lastMqttTopic"] = lastMqttTopic;
    doc["lastMqttPayload"] = lastMqttPayload;
    
    JsonObject sensorInfo = doc.createNestedObject("sensorInfo");
    sensorInfo["available"] = mqttDataValid;
    sensorInfo["device"] = sensorDeviceId;
    sensorInfo["ip"] = sensorIpAddress;
    sensorInfo["battery_percentage"] = batteryPercentage;
    sensorInfo["battery_status"] = batteryStatus;
    sensorInfo["battery_voltage"] = batteryVoltage;
    sensorInfo["timestamp"] = sensorTimestamp;
    sensorInfo["next_online"] = sensorNextOnline;
    sensorInfo["uptime"] = sensorUptime;
    
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
    doc["mqtt_publish_topic"] = config.mqtt_publish_topic;
    doc["low_threshold"] = config.low_threshold;
    doc["high_threshold"] = config.high_threshold;
    doc["min_power"] = config.min_power_threshold;
    doc["dry_run_protection"] = config.pump_protection_time;
    doc["use_espnow"] = config.use_espnow;
    doc["peer_mac"] = macToString(config.peer_mac);
    doc["espnow_channel"] = config.espnow_channel;
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
      if (doc.containsKey("mqtt_publish_topic")) strcpy(config.mqtt_publish_topic, doc["mqtt_publish_topic"]);
      saveConfig();
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    }
  });
  
  // New endpoint for sending test MQTT messages
  server.on("/mqtt/send", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      String topic = doc["topic"].as<String>();
      String message = doc["message"].as<String>();
      
      if (topic.length() > 0 && message.length() > 0) {
        publishTestMessage(topic, message);
        server.send(200, "text/plain", "✅ Message sent to topic: " + topic);
      } else {
        server.send(400, "text/plain", "❌ Topic and message required");
      }
    } else {
      server.send(400, "text/plain", "❌ Invalid request");
    }
  });
  
  server.on("/config/espnow", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("use_espnow")) config.use_espnow = doc["use_espnow"];
      if (doc.containsKey("peer_mac")) {
        String mac = doc["peer_mac"].as<String>();
        stringToMac(mac, config.peer_mac);
        strcpy(config.peer_mac_str, mac.c_str());
      }
      if (doc.containsKey("espnow_channel")) config.espnow_channel = doc["espnow_channel"];
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
      if (doc.containsKey("dry_run_protection")) config.pump_protection_time = doc["dry_run_protection"];
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
  
  server.on("/exportstats", HTTP_GET, []() {
    String csv = "Timestamp,Runtime(min),Energy(kWh),Cycles,LastStart\n";
    csv += String(millis() / 1000) + ",";
    csv += String(pumpStats.totalRuntimeSeconds / 60) + ",";
    csv += String(pumpStats.totalEnergyKwh) + ",";
    csv += String(pumpStats.pumpCycles) + ",";
    csv += String(pumpStats.lastStartStr) + "\n";
    server.send(200, "text/csv", csv);
  });
  
  server.on("/resetwifi", HTTP_GET, []() {
    memset(config.ssid, 0, sizeof(config.ssid));
    memset(config.password, 0, sizeof(config.password));
    saveConfig();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
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
  
  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanComplete();
    if (n == -2) {
      WiFi.scanNetworks(true);
      server.send(200, "application/json", "[]");
    } else if (n >= 0) {
      String json = "[";
      for (int i = 0; i < n; ++i) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
      }
      json += "]";
      server.send(200, "application/json", json);
      WiFi.scanDelete();
    } else {
      server.send(200, "application/json", "[]");
    }
  });
  
  server.on("/espnow/add", HTTP_GET, []() {
    if (server.hasArg("mac")) {
      uint8_t mac[6];
      if (stringToMac(server.arg("mac"), mac)) {
        esp_now_add_peer(mac, ESP_NOW_ROLE_SLAVE, config.espnow_channel, NULL, 0);
        memcpy(config.peer_mac, mac, 6);
        strcpy(config.peer_mac_str, server.arg("mac").c_str());
        saveConfig();
        server.send(200, "text/plain", "OK");
      }
    }
  });
  
  server.on("/testmqtt", HTTP_GET, []() {
    WiFiClient testClient;
    PubSubClient testMqtt(testClient);
    testMqtt.setServer(config.mqtt_server, config.mqtt_port);
    if (testMqtt.connect("testclient")) {
      server.send(200, "text/plain", "✅ MQTT connection successful! Device will subscribe to: " + String(config.mqtt_topic));
      testMqtt.disconnect();
    } else {
      server.send(200, "text/plain", "❌ MQTT connection failed: " + String(testMqtt.state()));
    }
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
    DEBUG_PRINTLN("\n=== Smart Pump Controller v2.8.0 (MQTT Test Publisher) ===");
  #endif
  
  loadConfig();
  
  #if DEBUG
    dumpEEPROM();
  #endif
  
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
      DEBUG_PRINTLN("Failed to connect to WiFi, entering AP mode");
      setupAPMode();
    }
  } else {
    DEBUG_PRINTLN("No SSID configured, entering AP mode");
    setupAPMode();
  }
  
  setupWebServer();
  ArduinoOTA.begin();
  if (!apMode) {
    MDNS.begin(deviceName.c_str());
  }
  server.begin();
  
  DEBUG_PRINTLN("Setup complete - " + deviceName);
  DEBUG_PRINTLN("MQTT Subscribe Topic: " + String(config.mqtt_topic));
  DEBUG_PRINTLN("MQTT Publish Topic: " + String(config.mqtt_publish_topic));
  DEBUG_PRINTLN("===============================\n");
}

// ========== Main Loop ==========
void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastS31Update >= S31_UPDATE_INTERVAL) {
    lastS31Update = currentMillis;
    s31.update();
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
