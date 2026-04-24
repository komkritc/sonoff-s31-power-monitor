<<<<<<< HEAD
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
 * Version: 1.0.2 - Serial logging disabled for CSE7766 compatibility
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
SonoffS31 s31(3, 12);  // RX pin 3, Relay pin 12

// Web Server
ESP8266WebServer server(80);

// Device name
String deviceName = "s31";

// Timing variables
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000; // Update every second

// Cache for power data
PowerData cachedData;

// HTML Dashboard (same as before - kept for brevity)
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
        .error {
            color: #ef4444;
            font-size: 12px;
            text-align: center;
            margin-top: 10px;
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
                <div id="errorMsg" class="error"></div>
            </div>
        </div>
    </div>
    <script>
        let retryCount = 0;
        const maxRetries = 3;
        
        async function fetchData() {
            try {
                const response = await fetch('/data');
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}`);
                }
                const data = await response.json();
                
                document.getElementById('power').innerText = data.power.toFixed(1);
                document.getElementById('voltage').innerText = data.voltage.toFixed(1);
                document.getElementById('current').innerText = data.current.toFixed(2);
                document.getElementById('pf').innerText = data.powerFactor.toFixed(2);
                document.getElementById('energy').innerText = data.energy.toFixed(3);
                
                const relayBtn = document.getElementById('relayBtn');
                const relayText = document.getElementById('relayText');
                
                if (data.relayState) {
                    relayBtn.className = 'relay-btn on';
                    relayText.innerText = 'ON';
                } else {
                    relayBtn.className = 'relay-btn off';
                    relayText.innerText = 'OFF';
                }
                
                document.getElementById('statusLed').className = 'status online';
                document.getElementById('statusText').innerHTML = 'Connected';
                document.getElementById('updateTime').innerHTML = 'Updated: ' + new Date().toLocaleTimeString();
                document.getElementById('errorMsg').innerHTML = '';
                retryCount = 0;
                
            } catch(e) {
                console.error('Fetch error:', e);
                retryCount++;
                document.getElementById('statusLed').className = 'status offline';
                document.getElementById('statusText').innerHTML = 'Reconnecting...';
                
                if (retryCount > maxRetries) {
                    document.getElementById('errorMsg').innerHTML = 'Connection issues. Check if device is powered.';
                }
            }
        }
        
        async function toggleRelay() {
            try {
                const response = await fetch('/toggle');
                if (response.ok) {
                    setTimeout(fetchData, 200);
                }
            } catch(e) {
                console.error('Toggle error:', e);
                document.getElementById('errorMsg').innerHTML = 'Failed to toggle relay. Check connection.';
            }
        }
        
        async function resetEnergy() {
            if (confirm('Reset energy counter?')) {
                try {
                    await fetch('/reset');
                    setTimeout(fetchData, 500);
                } catch(e) {
                    console.error('Reset error:', e);
                    document.getElementById('errorMsg').innerHTML = 'Failed to reset energy.';
                }
            }
        }
        
        async function getDeviceInfo() {
            try {
                const res = await fetch('/info');
                if (res.ok) {
                    const info = await res.json();
                    document.getElementById('deviceId').innerHTML = info.hostname + '.local | ' + info.ip;
                }
            } catch(e) {
                console.log('Info fetch error:', e);
            }
        }
        
        fetchData();
        getDeviceInfo();
        setInterval(fetchData, 2000);
        setInterval(getDeviceInfo, 30000);
    </script>
</body>
</html>
)rawliteral";

void setup() {
    // ==============================================
    // CRITICAL: Disable Serial for CSE7766 to work
    // ==============================================
    
    // Option 1: Uncomment below to see initial boot message, then disable
    /*
    Serial.begin(115200);
    Serial.println("\n\nSonoff S31 Starting...");
    delay(500);
    Serial.end();  // End serial to free UART for CSE7766
    delay(100);
    */
    
    // Option 2: Complete silence (recommended for maximum compatibility)
    // Don't initialize Serial at all - this gives the UART fully to CSE7766
    
    // Initialize Sonoff S31 - this will now have exclusive UART access
    pinMode(3, INPUT);  // Ensure RX pin is input
    delay(500);         // Give chip time to stabilize
    
    // Try different initialization methods
    // Method 1: Standard
    s31.begin(4800);
    delay(500);
    
    // Method 2: If Method 1 fails, uncomment and try with parity
    // Serial.begin(4800, SERIAL_8E1);
    // Serial.end();
    // s31.begin(4800);
    
    // Generate unique hostname
    uint32_t chipId = ESP.getChipId();
    deviceName = "s31-" + String(chipId & 0xFFFF, HEX);
    deviceName.toLowerCase();
    
    // Connect to WiFi (no serial output)
    WiFi.hostname(deviceName);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // Wait for WiFi connection without serial output
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        attempts++;
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
        json += "\"voltage\":" + String(cachedData.voltage, 1) + ",";
        json += "\"current\":" + String(cachedData.current, 3) + ",";
        json += "\"power\":" + String(cachedData.power, 1) + ",";
        json += "\"apparentPower\":" + String(cachedData.apparentPower, 1) + ",";
        json += "\"powerFactor\":" + String(cachedData.powerFactor, 2) + ",";
        json += "\"energy\":" + String(cachedData.energy, 3) + ",";
        json += "\"relayState\":" + String(cachedData.relayState ? "true" : "false") + ",";
        json += "\"isValid\":" + String(cachedData.isValid ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/toggle", HTTP_GET, []() {
        s31.relayToggle();
        delay(50);
        s31.update();
        cachedData = s31.getPowerData();
        server.send(200, "text/plain", "OK");
    });
    
    server.on("/on", HTTP_GET, []() {
        s31.relayOn();
        delay(50);
        s31.update();
        cachedData = s31.getPowerData();
        server.send(200, "text/plain", "ON");
    });
    
    server.on("/off", HTTP_GET, []() {
        s31.relayOff();
        delay(50);
        s31.update();
        cachedData = s31.getPowerData();
        server.send(200, "text/plain", "OFF");
    });
    
    server.on("/reset", HTTP_GET, []() {
        s31.resetEnergy();
        delay(50);
        s31.update();
        cachedData = s31.getPowerData();
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
    
    // Initial reading
    delay(1000);
    s31.update();
    cachedData = s31.getPowerData();
    
    // Optional: Quick check - if you have a LED, flash it to indicate boot complete
    // pinMode(LED_BUILTIN, OUTPUT);
    // for(int i=0; i<3; i++) {
    //   digitalWrite(LED_BUILTIN, LOW);
    //   delay(100);
    //   digitalWrite(LED_BUILTIN, HIGH);
    //   delay(100);
    // }
}

void loop() {
    // Update sensor readings periodically
    if (millis() - lastUpdate >= updateInterval) {
        s31.update();
        cachedData = s31.getPowerData();
        lastUpdate = millis();
        
        // If you have an LED, you can use it for debugging instead of Serial
        // static bool ledState = false;
        // if(cachedData.power > 10) {  // Blink when device is consuming power
        //   ledState = !ledState;
        //   digitalWrite(LED_BUILTIN, ledState);
        // }
    }
    
    // Handle OTA and web server
    ArduinoOTA.handle();
    MDNS.update();
    server.handleClient();
    
    delay(10);
=======
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SonoffS31.h>

const char* ssid = "your_ssid";
const char* password = "your_password";

SonoffS31 s31(3, 12);
ESP8266WebServer server(80);

void setup() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    s31.begin();
    
    server.on("/", []() {
        String html = "<html><head><meta http-equiv='refresh' content='1'></head><body>";
        html += "<h1>Sonoff S31</h1>";
        html += "<p>Voltage: " + String(s31.getVoltage()) + "V</p>";
        html += "<p>Current: " + String(s31.getCurrent()) + "A</p>";
        html += "<p>Power: " + String(s31.getPower()) + "W</p>";
        html += "<p>Energy: " + String(s31.getEnergy()) + "kWh</p>";
        html += "<a href='/on'><button>ON</button></a>";
        html += "<a href='/off'><button>OFF</button></a>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    });
    
    server.on("/on", []() {
        s31.relayOn();
        server.send(200, "text/plain", "ON");
    });
    
    server.on("/off", []() {
        s31.relayOff();
        server.send(200, "text/plain", "OFF");
    });
    
    server.begin();
}

void loop() {
    s31.update();
    server.handleClient();
>>>>>>> 70a5202885ec4032b62d52e3d2352cd56e4bed09
}
