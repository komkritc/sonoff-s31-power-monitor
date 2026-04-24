/*
 * Sonoff S31 Web Dashboard
 * Using SonoffS31 library for CSE7766 power monitoring and relay control
 * 
 * Features:
 * - Real-time power monitoring using CSE7766
 * - Web dashboard with responsive design
 * - Relay control via web interface
 * - mDNS support (http://s31-xxxx.local)
 * - OTA updates
 * 
 * Author: Komkrit Chooraung
 * Date: 2025-12-21
 * Version: 1.0.0
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SonoffS31.h>

// WiFi Configuration
const char* ssid = "redmi4xx";
const char* password = "komkritc";

// Create SonoffS31 instance
// Parameters: RX pin (for CSE7766), Relay pin
SonoffS31 s31(3, 12);

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
        
        button.action {
            background: #667eea;
            color: white;
            border: none;
            padding: 5px 10px;
            border-radius: 5px;
            font-size: 10px;
            cursor: pointer;
            margin-top: 10px;
        }
        
        button.action:hover {
            background: #5a67d8;
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
            <h1>⚡ Sonoff S31</h1>
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
                    <div class="stat-label">Power Factor</div>
                    <div class="stat-value">
                        <span id="pf">0</span>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">Energy</div>
                    <div class="stat-value">
                        <span id="energy">0</span><span class="stat-unit">kWh</span>
                    </div>
                </div>
            </div>
            
            <button class="relay-btn off" id="relayBtn" onclick="toggleRelay()">
                <span id="relayText">OFF</span>
            </button>
            
            <div style="text-align: center; margin-top: 10px;">
                <button class="action" onclick="resetEnergy()">Reset Energy</button>
            </div>
            
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
                document.getElementById('pf').innerText = data.powerFactor.toFixed(2);
                document.getElementById('energy').innerText = data.energy.toFixed(3);
                
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
        
        async function resetEnergy() {
            if (confirm('Reset energy counter?')) {
                try {
                    await fetch('/reset');
                    setTimeout(fetchData, 100);
                } catch(e) {
                    console.error('Reset error:', e);
                }
            }
        }
        
        async function getDeviceInfo() {
            try {
                const res = await fetch('/info');
                const info = await res.json();
                document.getElementById('deviceId').innerHTML = info.hostname + '.local | ' + info.ip;
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

void setup() {
    // Initialize Serial for debugging (optional)
    Serial.begin(115200);
    
    // Initialize Sonoff S31 library
    s31.begin(4800);
    
    // Generate unique hostname
    uint32_t chipId = ESP.getChipId();
    deviceName = "s31-" + String(chipId & 0xFFFF, HEX);
    deviceName.toLowerCase();
    
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
        s31.update();
        PowerData data = s31.getPowerData();
        
        String json = "{";
        json += "\"voltage\":" + String(data.voltage, 1) + ",";
        json += "\"current\":" + String(data.current, 3) + ",";
        json += "\"power\":" + String(data.power, 1) + ",";
        json += "\"apparentPower\":" + String(data.apparentPower, 1) + ",";
        json += "\"powerFactor\":" + String(data.powerFactor, 2) + ",";
        json += "\"energy\":" + String(data.energy, 3) + ",";
        json += "\"relayState\":" + String(data.relayState ? "true" : "false") + ",";
        json += "\"isValid\":" + String(data.isValid ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/toggle", HTTP_GET, []() {
        s31.relayToggle();
        server.send(200, "text/plain", "OK");
    });
    
    server.on("/on", HTTP_GET, []() {
        s31.relayOn();
        server.send(200, "text/plain", "ON");
    });
    
    server.on("/off", HTTP_GET, []() {
        s31.relayOff();
        server.send(200, "text/plain", "OFF");
    });
    
    server.on("/reset", HTTP_GET, []() {
        s31.resetEnergy();
        server.send(200, "text/plain", "OK");
    });
    
    server.on("/info", HTTP_GET, []() {
        String json = "{";
        json += "\"hostname\":\"" + deviceName + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"freeHeap\":" + String(ESP.getFreeHeap());
        json += "}";
        server.send(200, "application/json", json);
    });
    
    server.begin();
}

void loop() {
    // Update sensor readings
    s31.update();
    
    // Handle OTA and web server
    ArduinoOTA.handle();
    MDNS.update();
    server.handleClient();
    
    delay(10);
}