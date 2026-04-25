#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SonoffS31.h>

// WiFi Configuration
const char* ssid = "redmi4xx";
const char* password = "komkritc";

// Device name
String deviceName = "s31";

// Create SonoffS31 instance (relay on GPIO12)
SonoffS31 s31(12);

// Web Server
ESP8266WebServer server(80);

// HTML Dashboard (same as before)
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
                document.getElementById('current').innerText = data.current.toFixed(3);
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
        
        async function getDeviceInfo() {
            try {
                const res = await fetch('/info');
                const info = await res.json();
                document.getElementById('deviceId').innerHTML = info.hostname + '.local';
            } catch(e) {}
        }
        
        setInterval(fetchData, 1000);
        setInterval(getDeviceInfo, 30000);
        
        fetchData();
        getDeviceInfo();
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);  // For debugging
  
  // Initialize the S31 library
  s31.begin();
  
  // Optional: Set callbacks for debugging
  s31.onPowerUpdate([](float power, float voltage, float current) {
    // Optional: Log power updates
    // Serial.printf("Power: %.1fW, Voltage: %.1fV, Current: %.3fA\n", power, voltage, current);
  });
  
  s31.onRelayChange([](bool state) {
    Serial.printf("Relay changed to: %s\n", state ? "ON" : "OFF");
  });
  
  // Connect to WiFi
  WiFi.hostname(deviceName);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  
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
    json += "\"voltage\":" + String(s31.getVoltage(), 1) + ",";
    json += "\"current\":" + String(s31.getCurrent(), 3) + ",";
    json += "\"power\":" + String(s31.getPower(), 1) + ",";
    json += "\"energy\":" + String(s31.getEnergy(), 3) + ",";
    json += "\"relayState\":" + String(s31.getRelayState() ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });
  
  server.on("/toggle", HTTP_GET, []() {
    s31.toggleRelay();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/info", HTTP_GET, []() {
    String json = "{";
    json += "\"hostname\":\"" + deviceName + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });
  
  server.on("/reset", HTTP_GET, []() {
    s31.resetEnergy();
    server.send(200, "text/plain", "Energy reset");
  });
  
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  // Update the S31 library (reads power data)
  s31.update();
  
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Handle mDNS
  MDNS.update();
  
  // Handle web server
  server.handleClient();
  
  delay(5);
}