/**
 * ================================================================================================
 * Sonoff S31 Smart Pump Controller - Offline ESP-NOW Mode
 * ================================================================================================
 * @file        S31_ESPNOW_OFFLINE.ino
 * @version     3.1.0
 * @author      Smart Pump Controller
 * @license     MIT
 * 
 * @brief       Complete pump controller for Sonoff S31 smart plug with ESP-NOW sensor integration
 * 
 * ================================================================================================
 * @section     FEATURES
 * ================================================================================================
 * ✓ Always in AP mode (192.168.4.1) - No WiFi client connection required
 * ✓ ESP-NOW receives sensor data: {"d":351.9,"l":0.0,"v":0.0,"b":3.80}
 * ✓ Manual "get_measure" button in ESP-NOW tab for on-demand sensor polling
 * ✓ Modern toggle switch for Auto/Manual mode (replaces dual buttons)
 * ✓ Full web UI with 6 tabs: Dashboard, ESP-NOW, Pump Settings, Safety, Statistics, System
 * ✓ Sensor failure handling with 4 configurable strategies
 * ✓ Power Factor display with quality indicators
 * ✓ Dry run protection and pump statistics tracking
 * ✓ OTA updates support
 * 
 * ================================================================================================
 * @section     SENSOR_FAILURE_STRATEGIES
 * ================================================================================================
 * 0 - STOP PUMP    : Immediately stop pump for maximum safety (DEFAULT) - WORKS IN AUTO & MANUAL
 * 1 - MAINTAIN     : Keep pump in last known safe state
 * 2 - FORCE ON     : Force pump ON (RISKY - potential overflow!)
 * 3 - CYCLIC       : Run pump in cycles (e.g., 5min ON, 30min OFF)
 * 
 * ================================================================================================
 * @section       BUG_FIX_v3.1.0
 * ================================================================================================
 * FIX: Previously in Manual Mode, pump would stay ON even when sensor was dead.
 *      Now sensor failure protection applies to BOTH Auto and Manual modes.
 *      When Strategy 0 is selected, pump stops immediately regardless of mode.
 * 
 * ================================================================================================
 * @section     ESP_NOW_MESSAGE_FORMAT
 * ================================================================================================
 * Expected JSON from sensor: {"d":351.9,"l":0.0,"v":0.0,"b":3.80}
 * Where:
 *   d = distance (cm)
 *   l = water level (%)
 *   v = volume (L)
 *   b = battery voltage (V)
 * 
 * ================================================================================================
 * @section     HARDWARE_CONNECTIONS
 * ================================================================================================
 * Relay Pin  : GPIO12 (built-in relay on Sonoff S31)
 * 
 * ================================================================================================
 * @section     NETWORK_SETUP
 * ================================================================================================
 * AP SSID    : SmartPump-XXXX (where XXXX is last 4 digits of chip ID)
 * AP Password: 12345678
 * AP IP      : 192.168.4.1
 * 
 * ================================================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SonoffS31.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <espnow.h>

// ================================================================================================
// @section     TIMING CONSTANTS (milliseconds)
// ================================================================================================
#define S31_UPDATE_INTERVAL     100     /**< Sonoff power monitoring update interval */
#define CONTROL_INTERVAL        500     /**< Pump control logic interval */
#define WEB_SERVER_INTERVAL     10      /**< Web server handling interval */
#define OTA_INTERVAL            50      /**< OTA update handling interval */
#define MDNS_INTERVAL           1000    /**< mDNS announcement interval */
#define AQUIRE_TIMEOUT          100     /**< Microsecond timeout for task scheduling */

// ================================================================================================
// @section     ESP-NOW CONSTANTS
// ================================================================================================
#define ESP_NOW_SEND_INTERVAL   15000   /**< Auto-send "get_measure" every 15 seconds */
#define ESP_NOW_DATA_TIMEOUT    30000   /**< Consider data stale after 30 seconds */
#define MAX_LOG_ENTRIES         50      /**< Maximum ESP-NOW terminal log entries */

// ================================================================================================
// @section     SENSOR FAILURE CONSTANTS
// ================================================================================================
#define SENSOR_HEARTBEAT_TIMEOUT  120000  /**< 2 minutes - consider sensor dead after this */

// ================================================================================================
// @section     FEATURE FLAGS
// ================================================================================================
#define ENABLE_MDNS             true    /**< Enable mDNS for easy discovery */
#define ENABLE_OTA              true    /**< Enable Over-The-Air updates */
#define ENABLE_STATUS_LED       false   /**< Enable GPIO13 status LED (set false if not used) */

// ================================================================================================
// @section     PIN DEFINITIONS
// ================================================================================================
#define RELAY_PIN   12    /**< Sonoff S31 built-in relay control */

// ================================================================================================
// @struct      Config
// @brief       Configuration structure stored in EEPROM
// ================================================================================================
struct Config {
  uint32_t magic = 0xDEADBEEF;                    /**< Magic number for EEPROM validation */
  
  // Pump control thresholds
  float low_threshold = 30.0;                    /**< Pump ON when water level <= this (%) */
  float high_threshold = 80.0;                   /**< Pump OFF when water level >= this (%) */
  bool auto_mode = true;                         /**< true = Auto, false = Manual */
  float min_power_threshold = 5.0;               /**< Minimum power (W) to detect pump running */
  unsigned long pump_protection_time = 300;      /**< Dry run protection timeout (seconds) */
  
  // ESP-NOW Configuration
  bool use_espnow = true;                        /**< Enable/disable ESP-NOW */
  uint8_t peer_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  /**< Sensor MAC (broadcast) */
  char peer_mac_str[18] = "FF:FF:FF:FF:FF:FF";   /**< String representation of MAC */
  int espnow_channel = 1;                        /**< WiFi channel for ESP-NOW (1-13) */
  
  // Sensor Failure Configuration
  uint8_t sensor_failure_strategy = 0;           /**< 0=stop, 1=maintain, 2=force_on, 3=cyclic */
  unsigned long sensor_timeout = SENSOR_HEARTBEAT_TIMEOUT;  /**< Timeout before declaring sensor dead */
  bool sensor_emergency_stop = true;             /**< Enable emergency stop on sensor failure */
  unsigned long cyclic_on_duration = 300000;     /**< Cyclic mode: ON duration (ms) - default 5 min */
  unsigned long cyclic_off_duration = 1800000;   /**< Cyclic mode: OFF duration (ms) - default 30 min */
} config;  /**< Global configuration instance */

// ================================================================================================
// @section     GLOBAL OBJECTS
// ================================================================================================
SonoffS31 s31(RELAY_PIN);               /**< Sonoff power monitoring library instance */
ESP8266WebServer server(80);            /**< Web server on port 80 */

// ================================================================================================
// @section     GLOBAL VARIABLES - System State
// ================================================================================================
String deviceName = "s31-pump";         /**< Device hostname */
unsigned long lastS31Update = 0;        /**< Last Sonoff power update timestamp */
unsigned long lastControlCheck = 0;     /**< Last pump control check timestamp */
unsigned long lastWebServer = 0;        /**< Last web server handle timestamp */
unsigned long lastOTA = 0;              /**< Last OTA handle timestamp */
unsigned long lastMDNS = 0;             /**< Last mDNS update timestamp */

// ================================================================================================
// @section     GLOBAL VARIABLES - Sensor Data (from ESP-NOW)
// ================================================================================================
float currentWaterLevel = 0;            /**< Current tank water level (%) */
float currentDistance = 0;              /**< Distance from sensor to water (cm) */
float currentVolume = 0;                /**< Calculated water volume (L) */
float batteryVoltage = 0;               /**< Sensor battery voltage (V) */
unsigned long lastEspNowData = 0;       /**< Timestamp of last valid ESP-NOW data */
bool espnowDataValid = false;           /**< Is current ESP-NOW data valid? */

// ================================================================================================
// @section     GLOBAL VARIABLES - Sensor Health Monitoring
// ================================================================================================
bool sensorIsDead = false;              /**< Sensor declared dead flag */
bool sensorWarningIssued = false;       /**< Warning already issued flag */
unsigned long sensorDeadStartTime = 0;  /**< When sensor was declared dead */

// ================================================================================================
// @section     GLOBAL VARIABLES - Cyclic Mode Operation
// ================================================================================================
unsigned long cyclicLastSwitchTime = 0; /**< Last cyclic mode switch timestamp */
bool cyclicPumpState = false;           /**< Current cyclic mode pump state */

// ================================================================================================
// @section     GLOBAL VARIABLES - Power Quality
// ================================================================================================
float powerFactor = 0.0;                /**< Power factor (0-1) - 0 when pump off */
float apparentPower = 0.0;              /**< Apparent power (VA) */
float reactivePower = 0.0;              /**< Reactive power (VAR) */

// ================================================================================================
// @struct      EspNowPacket
// @brief       ESP-NOW communication packet structure
// ================================================================================================
typedef struct {
  uint32_t seq;           /**< Sequence number (increments each message) */
  uint32_t timestamp;     /**< Microsecond timestamp */
  char msg[64];           /**< Message payload (command or JSON data) */
} EspNowPacket;

EspNowPacket outgoing;    /**< Outgoing packet buffer */
EspNowPacket incoming;    /**< Incoming packet buffer */

// ================================================================================================
// @struct      EspNowLogEntry
// @brief       ESP-NOW log entry structure
// ================================================================================================
struct EspNowLogEntry {
  unsigned long timestamp;      /**< Millis when message was received */
  String mac;                   /**< Sender MAC address */
  String rawData;               /**< Raw JSON string */
  float distance;               /**< Parsed distance (cm) */
  float level;                  /**< Parsed water level (%) */
  float volume;                 /**< Parsed volume (L) */
  float battery;                /**< Parsed battery voltage (V) */
  bool valid;                   /**< Was parsing successful? */
};

// ================================================================================================
// @section     ESP-NOW GLOBALS
// ================================================================================================
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  /**< Broadcast MAC address */
bool espnow_initialized = false;        /**< ESP-NOW initialization flag */
unsigned long lastEspNowSend = 0;       /**< Last auto-request timestamp */
uint32_t espnow_seq = 0;                /**< Outgoing packet sequence counter */
uint32_t espnow_msg_counter = 0;        /**< Total received messages counter */
std::vector<EspNowLogEntry> espnow_log; /**< Circular buffer of ESP-NOW logs */

// ================================================================================================
// @struct      PumpStats
// @brief       Pump statistics structure
// ================================================================================================
struct PumpStats {
  unsigned long totalRuntimeSeconds = 0;    /**< Total pump runtime (seconds) */
  float totalEnergyKwh = 0;                 /**< Total energy consumed (kWh) */
  int pumpCycles = 0;                       /**< Number of pump on/off cycles */
  char lastStartStr[32] = "Never";          /**< Last start time formatted string */
  unsigned long lastPumpOnTime = 0;         /**< Timestamp when pump was last turned on */
  bool wasRunning = false;                  /**< Previous pump state (for cycle detection) */
} pumpStats;  /**< Global pump statistics instance */

// ================================================================================================
// @section     FAILURE LOG
// ================================================================================================
std::vector<String> failureLog;           /**< Circular buffer of sensor failure events */

// ================================================================================================
// @section     DEBUG MACROS (disabled for production)
// ================================================================================================
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)

// ================================================================================================
// @section     HELPER FUNCTIONS
// ================================================================================================

/**
 * @brief       Add entry to failure log (circular buffer)
 * @param       message Failure description to log
 */
void addFailureLogEntry(String message) {
  failureLog.insert(failureLog.begin(), message);
  while (failureLog.size() > 50) failureLog.pop_back();
}

/**
 * @brief       Generate HTML for failure log display
 * @return      HTML formatted failure log
 */
String getFailureLogHTML() {
  if (failureLog.empty()) return "<div class='terminal-entry'>No failure events logged</div>";
  String html = "";
  for (size_t i = 0; i < failureLog.size() && i < 20; i++) {
    html += "<div class='terminal-entry'>" + failureLog[i] + "</div>";
  }
  return html;
}

/**
 * @brief       Convert MAC address byte array to string
 * @param       mac Byte array of MAC address
 * @return      Formatted MAC string (XX:XX:XX:XX:XX:XX)
 */
String macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

/**
 * @brief       Convert MAC string to byte array
 * @param       macStr MAC string (XX:XX:XX:XX:XX:XX format)
 * @param       mac Output byte array (must be size 6)
 * @return      true if parsing successful
 */
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

// ================================================================================================
// @section     EEPROM CONFIGURATION MANAGEMENT
// ================================================================================================

/**
 * @brief       Validate configuration integrity
 * @return      true if config magic number matches and thresholds are valid
 */
bool isConfigValid() {
  return (config.magic == 0xDEADBEEF &&
          config.low_threshold >= 0 &&
          config.high_threshold <= 100 &&
          config.low_threshold < config.high_threshold);
}

/**
 * @brief       Save current configuration to EEPROM
 */
void saveConfig() { 
  EEPROM.begin(512);
  config.magic = 0xDEADBEEF;
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
}

/**
 * @brief       Load configuration from EEPROM, apply defaults if invalid
 */
void loadConfig() { 
  EEPROM.begin(512);
  EEPROM.get(0, config);
  EEPROM.end();
  
  if (!isConfigValid()) {
    Config defaultConfig;
    config = defaultConfig;
    saveConfig();
  }
  
  // Sanitize values
  if (config.low_threshold >= config.high_threshold) {
    config.low_threshold = 30.0;
    config.high_threshold = 80.0;
  }
  if (config.espnow_channel < 1 || config.espnow_channel > 13) {
    config.espnow_channel = 1;
  }
  if (config.sensor_timeout < 10000) {
    config.sensor_timeout = SENSOR_HEARTBEAT_TIMEOUT;
  }
}

/**
 * @brief       Factory reset - erase all settings and reboot
 */
void factoryReset() {
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

/**
 * @brief       Reboot device after delay
 */
void rebootDevice() {
  delay(500);
  ESP.restart();
}

// ================================================================================================
// @section     POWER QUALITY CALCULATION
// ================================================================================================

/**
 * @brief       Calculate power quality metrics (Power Factor, Apparent Power, Reactive Power)
 * 
 * Power Factor = Real Power / Apparent Power
 * - PF = 1 for purely resistive loads (heaters, incandescent bulbs)
 * - PF < 1 for inductive loads (pumps, motors, compressors)
 * - PF = 0 when pump is OFF
 */
void updatePowerQuality() {
  float voltage = s31.getVoltage();      // RMS voltage (V)
  float current = s31.getCurrent();      // RMS current (A)
  float realPower = s31.getPower();      // Real power (W)
  bool relayState = s31.getRelayState(); // Pump state
  
  apparentPower = voltage * current;     // Apparent power (VA)
  
  if (!relayState || realPower < 0.5) {
    // Pump is OFF - no power consumption
    powerFactor = 0.0;
    apparentPower = 0.0;
    reactivePower = 0.0;
  } else {
    // Pump is ON - calculate PF and reactive power
    if (apparentPower > 0.01) {
      powerFactor = realPower / apparentPower;
      if (powerFactor < 0) powerFactor = 0;
      if (powerFactor > 1) powerFactor = 1;
    } else {
      powerFactor = 0.0;
    }
    
    // Reactive power: Q = sqrt(VA² - W²)
    float vaSquared = apparentPower * apparentPower;
    float wSquared = realPower * realPower;
    if (vaSquared > wSquared) {
      reactivePower = sqrt(vaSquared - wSquared);
    } else {
      reactivePower = 0;
    }
  }
}

// ================================================================================================
// @section     SENSOR HEALTH MONITORING
// ================================================================================================

/**
 * @brief       Update sensor health status based on time since last data
 * 
 * States:
 * - Healthy: Data received within ESP_NOW_DATA_TIMEOUT
 * - Warning: Data older than timeout but within sensor_timeout
 * - Dead: No data for longer than sensor_timeout
 */
void updateSensorHealth() {
  unsigned long now = millis();
  unsigned long timeSinceLastData = (lastEspNowData > 0) ? (now - lastEspNowData) : config.sensor_timeout + 10000;
  
  if (timeSinceLastData < ESP_NOW_DATA_TIMEOUT) {
    // HEALTHY STATE - Sensor responding normally
    if (sensorIsDead) {
      sensorIsDead = false;
      sensorWarningIssued = false;
      addFailureLogEntry("✅ Sensor RECOVERED");
    }
  } 
  else if (timeSinceLastData < config.sensor_timeout) {
    // WARNING STATE - Data stale but not dead yet
    if (!sensorWarningIssued) {
      sensorWarningIssued = true;
      addFailureLogEntry("⚠️ Sensor WARNING: No data for " + String(timeSinceLastData/1000) + "s");
    }
    sensorIsDead = false;
  } 
  else {
    // DEAD STATE - Sensor not responding
    if (!sensorIsDead) {
      sensorIsDead = true;
      sensorDeadStartTime = now;
      addFailureLogEntry("❌ SENSOR DEAD: No data for " + String(timeSinceLastData/1000) + "s");
    }
  }
}

// ================================================================================================
// @section     EMERGENCY PUMP CONTROL (Sensor Failure Handling)
// ================================================================================================

/**
 * @brief       Handle pump control when sensor is dead
 * @param       currentState Current pump state (true=ON, false=OFF)
 * @return      Desired pump state based on failure strategy
 * 
 * Strategy 0 - STOP PUMP: Immediately turn off pump (safest)
 * Strategy 1 - MAINTAIN: Keep pump in last known state
 * Strategy 2 - FORCE ON: Force pump ON (RISKY - potential overflow)
 * Strategy 3 - CYCLIC: Run pump in timed cycles
 */
bool handleSensorFailurePumpControl(bool currentState) {
  if (!sensorIsDead) {
    return currentState;  // Normal operation
  }
  
  unsigned long now = millis();
  unsigned long timeSinceDead = now - sensorDeadStartTime;
  
  switch (config.sensor_failure_strategy) {
    case 0: // STOP PUMP - Most conservative, safest
      if (currentState) addFailureLogEntry("🛑 Emergency STOP");
      return false;
      
    case 1: // MAINTAIN STATE - Keep last known state
      return currentState;
      
    case 2: { // FORCE ON - RISKY! Only for critical applications
      if (timeSinceDead > config.sensor_timeout) {
        addFailureLogEntry("⚠️ FORCE ON - Risk of overflow!");
        return true;
      }
      return currentState;
    }
      
    case 3: { // CYCLIC MODE - Run in predictable cycles
      // Initialize on first dead event
      if (cyclicLastSwitchTime == 0) {
        cyclicLastSwitchTime = now;
        cyclicPumpState = false;
      }
      
      // Calculate current cycle duration
      unsigned long cycleDuration = cyclicPumpState ? 
        config.cyclic_on_duration : config.cyclic_off_duration;
      
      // Switch state when cycle completes
      if (now - cyclicLastSwitchTime >= cycleDuration) {
        cyclicPumpState = !cyclicPumpState;
        cyclicLastSwitchTime = now;
        addFailureLogEntry("🔄 Cyclic: Pump " + String(cyclicPumpState ? "ON" : "OFF"));
      }
      
      // Emergency override - prevent infinite running
      if (cyclicPumpState && (now - sensorDeadStartTime) > config.sensor_timeout) {
        addFailureLogEntry("⚠️ Cyclic emergency stop - timeout exceeded");
        return false;
      }
      
      return cyclicPumpState;
    }
      
    default:
      return false;  // Unknown strategy - default to safe
  }
}

// ================================================================================================
// @section     ESP-NOW COMMUNICATION
// ================================================================================================

/**
 * @brief       Add entry to ESP-NOW log circular buffer
 * @param       mac Sender MAC address
 * @param       rawData Raw JSON string received
 * @param       d Parsed distance
 * @param       l Parsed water level
 * @param       v Parsed volume
 * @param       b Parsed battery voltage
 */
void addEspNowLogEntry(String mac, String rawData, float d, float l, float v, float b) {
  EspNowLogEntry entry;
  entry.timestamp = millis();
  entry.mac = mac;
  entry.rawData = rawData;
  entry.distance = d;
  entry.level = l;
  entry.volume = v;
  entry.battery = b;
  entry.valid = (d > 0 || l > 0 || v > 0 || b > 0);
  
  espnow_log.insert(espnow_log.begin(), entry);
  while (espnow_log.size() > MAX_LOG_ENTRIES) espnow_log.pop_back();
  espnow_msg_counter++;
}

/**
 * @brief       Generate HTML table for ESP-NOW terminal log
 * @return      HTML formatted log
 */
String getEspNowLogHTML() {
  if (espnow_log.empty()) {
    return "<div class='terminal-entry'>No ESP-NOW messages received yet...</div>";
  }
  
  String html = "";
  for (size_t i = 0; i < espnow_log.size(); i++) {
    EspNowLogEntry& e = espnow_log[i];
    unsigned long age = (millis() - e.timestamp) / 1000;
    html += "<div class='terminal-entry " + String(e.valid ? "terminal-valid" : "terminal-invalid") + "'>";
    html += "<div><span class='terminal-time'>[" + String(age) + "s ago]</span> ";
    html += "<span class='terminal-mac'>📡 MAC: " + e.mac + "</span></div>";
    html += "<div><span class='terminal-data'>📨 Raw: " + e.rawData + "</span></div>";
    if (e.valid) {
      html += "<div><span class='terminal-parsed'>📊 Parsed: D=" + String(e.distance, 1) + "cm, ";
      html += "L=" + String(e.level, 1) + "%, ";
      html += "V=" + String(e.volume, 1) + "L, ";
      html += "B=" + String(e.battery, 2) + "V</span></div>";
    } else {
      html += "<div><span class='terminal-parsed'>⚠️ Failed to parse message</span></div>";
    }
    html += "</div>";
  }
  return html;
}

/**
 * @brief       ESP-NOW send callback - called after packet transmission
 */
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  // Silent - debug disabled for production
}

/**
 * @brief       ESP-NOW receive callback - called when sensor sends data
 * @param       mac Sender MAC address
 * @param       data Received data buffer
 * @param       len Data length
 * 
 * Parses JSON: {"d":distance,"l":level,"v":volume,"b":battery}
 */
void OnDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  // Validate packet size
  if (len != sizeof(EspNowPacket)) {
    addEspNowLogEntry(macToString(mac), "Invalid packet size: " + String(len), 0, 0, 0, 0);
    return;
  }
  
  memcpy(&incoming, data, sizeof(incoming));
  
  String senderMac = macToString(mac);
  String rawResponse = String(incoming.msg);
  
  float d = 0, l = 0, v = 0, b = 0;
  bool parseSuccess = false;
  
  // Parse JSON fields (manual parsing to avoid JSON library overhead)
  // Format: {"d":351.9,"l":0.0,"v":0.0,"b":3.80}
  
  // Distance (d)
  int dIndex = rawResponse.indexOf("\"d\":");
  if (dIndex != -1) {
    int start = dIndex + 4;
    int end = rawResponse.indexOf(",", start);
    if (end == -1) end = rawResponse.indexOf("}", start);
    if (end != -1) {
      d = rawResponse.substring(start, end).toFloat();
      parseSuccess = true;
    }
  }
  
  // Water level (l)
  int lIndex = rawResponse.indexOf("\"l\":");
  if (lIndex != -1) {
    int start = lIndex + 4;
    int end = rawResponse.indexOf(",", start);
    if (end == -1) end = rawResponse.indexOf("}", start);
    if (end != -1) {
      l = rawResponse.substring(start, end).toFloat();
      parseSuccess = true;
    }
  }
  
  // Volume (v)
  int vIndex = rawResponse.indexOf("\"v\":");
  if (vIndex != -1) {
    int start = vIndex + 4;
    int end = rawResponse.indexOf(",", start);
    if (end == -1) end = rawResponse.indexOf("}", start);
    if (end != -1) {
      v = rawResponse.substring(start, end).toFloat();
      parseSuccess = true;
    }
  }
  
  // Battery voltage (b)
  int bIndex = rawResponse.indexOf("\"b\":");
  if (bIndex != -1) {
    int start = bIndex + 4;
    int end = rawResponse.indexOf(",", start);
    if (end == -1) end = rawResponse.indexOf("}", start);
    if (end != -1) {
      b = rawResponse.substring(start, end).toFloat();
      parseSuccess = true;
    }
  }
  
  // Update global sensor variables on successful parse
  if (parseSuccess) {
    currentDistance = d;
    currentWaterLevel = l;
    currentVolume = v;
    batteryVoltage = b;
    lastEspNowData = millis();
    espnowDataValid = true;
    
    // Reset sensor dead flags on successful data receive
    if (sensorIsDead) {
      sensorIsDead = false;
      sensorWarningIssued = false;
      addFailureLogEntry("✅ Sensor RECOVERED - Data received");
    }
  }
  
  addEspNowLogEntry(senderMac, rawResponse, d, l, v, b);
}

/**
 * @brief       Initialize ESP-NOW communication
 * @return      true if initialization successful
 */
void initEspNow() {
  if (!config.use_espnow) {
    espnow_initialized = false;
    return;
  }
  
  // Configure WiFi in AP+STA mode for ESP-NOW
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();  // Don't connect to any AP
  
  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    espnow_initialized = false;
    return;
  }
  
  // Register callbacks
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Add peer (broadcast or specific MAC)
  uint8_t* peerMac = broadcastMac;
  if (config.peer_mac[0] != 0xFF && config.peer_mac[0] != 0x00) {
    peerMac = config.peer_mac;
  }
  
  if (esp_now_add_peer(peerMac, ESP_NOW_ROLE_COMBO, config.espnow_channel, NULL, 0) != 0) {
    espnow_initialized = false;
    return;
  }
  
  espnow_initialized = true;
}

/**
 * @brief       Send command to sensor via ESP-NOW
 * @param       command Command string (e.g., "get_measure")
 */
void sendEspNowCommand(const char* command) {
  if (!espnow_initialized) return;
  
  outgoing.seq = espnow_seq++;
  outgoing.timestamp = micros();
  strncpy(outgoing.msg, command, sizeof(outgoing.msg) - 1);
  outgoing.msg[sizeof(outgoing.msg) - 1] = '\0';
  
  uint8_t* peerMac = broadcastMac;
  if (config.peer_mac[0] != 0xFF && config.peer_mac[0] != 0x00) {
    peerMac = config.peer_mac;
  }
  
  esp_now_send(peerMac, (uint8_t *)&outgoing, sizeof(outgoing));
}

/**
 * @brief       Automatic sensor data request (called in loop)
 * Sends "get_measure" command at ESP_NOW_SEND_INTERVAL
 */
void requestSensorData() {
  if (!espnow_initialized) return;
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastEspNowSend >= ESP_NOW_SEND_INTERVAL) {
    lastEspNowSend = currentMillis;
    sendEspNowCommand("get_measure");
  }
}

/**
 * @brief       Manual sensor data request from web UI
 */
void manualRequestSensorData() {
  if (!espnow_initialized) {
    server.send(400, "text/plain", "ESP-NOW not initialized");
    return;
  }
  sendEspNowCommand("get_measure");
  server.send(200, "text/plain", "Sent 'get_measure' request");
}

// ================================================================================================
// @section     PUMP CONTROL LOGIC (CRITICAL FIX FOR MANUAL MODE)
// ================================================================================================

/**
 * @brief       Main pump control logic with sensor failure protection for BOTH modes
 * 
 * @details     CRITICAL FIX v3.1.0:
 *              Previously, Manual Mode would keep pump running even when sensor died.
 *              Now sensor failure protection applies to BOTH Auto and Manual modes.
 *              When Strategy 0 is selected, pump stops immediately regardless of mode.
 * 
 * Operation modes:
 * - AUTO MODE: Automatically control based on water level or failure strategy
 * - MANUAL MODE: User controls via web UI, but sensor failure can override for safety
 * 
 * Safety features:
 * - Sensor failure handling (configurable strategies) - WORKS IN BOTH MODES
 * - Dry run protection (Auto mode only)
 * - Pump cycle tracking for statistics
 */
void controlPump() {
  // Update sensor health status regardless of mode
  updateSensorHealth();
  
  // ============================================================================================
  // MANUAL MODE - With sensor failure protection!
  // ============================================================================================
  if (!config.auto_mode) {
    // Check if sensor is dead and we need to emergency stop
    if (sensorIsDead && config.sensor_failure_strategy == 0) {
      bool currentState = s31.getRelayState();
      if (currentState) {
        // Sensor is dead and pump is ON - emergency stop for safety!
        s31.setRelay(false);
        addFailureLogEntry("🚨 MANUAL MODE: Emergency stop - Sensor dead, pump turned OFF for safety");
      }
    }
    return;  // Exit - no auto control in manual mode
  }
  
  // ============================================================================================
  // AUTO MODE - Normal automatic control
  // ============================================================================================
  if (millis() - lastControlCheck < CONTROL_INTERVAL) return;
  lastControlCheck = millis();
  
  bool currentState = s31.getRelayState();
  bool shouldPumpOn = false;
  
  // Check if we have valid sensor data
  bool dataValid = (espnow_initialized && espnowDataValid && 
                    millis() - lastEspNowData < ESP_NOW_DATA_TIMEOUT);
  
  if (!dataValid || sensorIsDead) {
    // SENSOR FAILURE - Apply emergency strategy
    shouldPumpOn = handleSensorFailurePumpControl(currentState);
  } else {
    // NORMAL OPERATION - Control based on water level
    if (currentWaterLevel <= config.low_threshold) {
      shouldPumpOn = true;   // Tank low - turn pump ON
    } else if (currentWaterLevel >= config.high_threshold) {
      shouldPumpOn = false;  // Tank full - turn pump OFF
    } else {
      shouldPumpOn = currentState;  // Maintain current state
    }
  }
  
  // ============================================================================================
  // PUMP STATISTICS TRACKING
  // ============================================================================================
  
  // Detect pump start event
  if (currentState && !pumpStats.wasRunning) {
    pumpStats.pumpCycles++;
    pumpStats.lastPumpOnTime = millis();
    pumpStats.wasRunning = true;
    pumpStats.totalEnergyKwh = s31.getEnergy();
    
    // Format last start time (HH:MM)
    unsigned long runtimeSec = pumpStats.totalRuntimeSeconds;
    unsigned long hours = runtimeSec / 3600;
    unsigned long minutes = (runtimeSec % 3600) / 60;
    snprintf(pumpStats.lastStartStr, sizeof(pumpStats.lastStartStr), "%02lu:%02lu", hours, minutes);
  } 
  // Detect pump stop event
  else if (!currentState && pumpStats.wasRunning) {
    unsigned long runtime = (millis() - pumpStats.lastPumpOnTime) / 1000;
    pumpStats.totalRuntimeSeconds += runtime;
    pumpStats.wasRunning = false;
  }
  
  // ============================================================================================
  // DRY RUN PROTECTION (Auto mode only)
  // ============================================================================================
  // If pump is running but power is below threshold, it may be running dry
  if (currentState && s31.getPower() < config.min_power_threshold && !sensorIsDead) {
    unsigned long runtime = (millis() - pumpStats.lastPumpOnTime) / 1000;
    if (runtime > config.pump_protection_time) {
      shouldPumpOn = false;
      addFailureLogEntry("💧 Dry run protection - Pump stopped (low power)");
    }
  }
  
  // ============================================================================================
  // EXECUTE PUMP STATE CHANGE
  // ============================================================================================
  if (shouldPumpOn != currentState) {
    s31.setRelay(shouldPumpOn);
    addFailureLogEntry(String("Pump ") + (shouldPumpOn ? "ON" : "OFF") + 
                      " | Mode: " + (sensorIsDead ? "Emergency" : "Normal"));
  }
}

// ================================================================================================
// @section     SYSTEM SETUP FUNCTIONS
// ================================================================================================

/**
 * @brief       Setup Access Point mode
 * Creates WiFi AP for web interface access
 */
void setupAPMode() {
  String apSSID = "SmartPump-" + String(ESP.getChipId() & 0xFFFF, HEX);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), "12345678");
}

/**
 * @brief       Setup Arduino OTA (Over-The-Air updates)
 */
void setupArduinoOTA() {
  if (!ENABLE_OTA) return;
  
  ArduinoOTA.setHostname(deviceName.c_str());
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
  ArduinoOTA.begin();
}

// ================================================================================================
// @section     HTML UI PAGE (Complete with Modern Toggle Switch)
// ================================================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Pump Controller</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; padding: 20px; }
        .container { max-width: 1200px; margin: 0 auto; }
        .card { background: white; border-radius: 20px; padding: 25px; margin-bottom: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.2); }
        h1 { text-align: center; color: #333; margin-bottom: 5px; font-size: 24px; }
        .version { text-align: center; color: #999; font-size: 11px; margin-bottom: 5px; }
        .offline-badge { text-align: center; background: #FEF3C7; color: #92400E; padding: 8px; border-radius: 10px; font-size: 12px; margin-bottom: 15px; }
        .nav-buttons { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
        .nav-btn { flex: 1; background: #e5e7eb; color: #333; padding: 10px; border: none; border-radius: 10px; cursor: pointer; font-weight: bold; transition: all 0.3s; }
        .nav-btn.active { background: #667eea; color: white; }
        .water-level { background: linear-gradient(135deg, #3b82f6 0%, #1e3a8a 100%); border-radius: 15px; padding: 20px; text-align: center; margin-bottom: 20px; color: white; }
        .level-value { font-size: 56px; font-weight: bold; }
        .level-bar-container { background: rgba(255,255,255,0.2); border-radius: 10px; margin-top: 15px; height: 30px; overflow: hidden; }
        .level-bar { background: #10B981; height: 100%; transition: width 0.5s; display: flex; align-items: center; justify-content: center; color: white; font-size: 12px; font-weight: bold; }
        .stats-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin-bottom: 20px; }
        .stat { background: #f5f5f5; padding: 15px; border-radius: 12px; text-align: center; }
        .stat-label { font-size: 11px; color: #666; margin-bottom: 5px; text-transform: uppercase; }
        .stat-value { font-size: 22px; font-weight: bold; color: #333; }
        .pf-card { background: #f5f5f5; padding: 15px; border-radius: 12px; text-align: center; margin-bottom: 20px; }
        .pf-value { font-size: 28px; font-weight: bold; }
        .pf-good { color: #10B981; }
        .pf-warning { color: #F59E0B; }
        .pf-bad { color: #EF4444; }
        .pf-idle { color: #9CA3AF; }
        .mode-container { display: flex; align-items: center; justify-content: space-between; background: #f8f9fa; padding: 20px; border-radius: 15px; margin-bottom: 20px; flex-wrap: wrap; gap: 15px; }
        .mode-info { flex: 1; }
        .mode-label { font-size: 14px; color: #666; margin-bottom: 5px; }
        .mode-status { font-size: 18px; font-weight: bold; }
        .mode-status.auto { color: #10B981; }
        .mode-status.manual { color: #F59E0B; }
        .switch-container { display: flex; align-items: center; gap: 15px; }
        .switch-label { font-size: 14px; font-weight: bold; color: #666; }
        .switch { position: relative; display: inline-block; width: 70px; height: 34px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #EF4444; transition: 0.4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: 0.4s; border-radius: 50%; }
        input:checked + .slider { background-color: #10B981; }
        input:checked + .slider:before { transform: translateX(36px); }
        .pump-btn { width: 140px; height: 140px; border-radius: 70px; border: none; font-size: 24px; font-weight: bold; cursor: pointer; transition: all 0.3s; margin: 10px auto; display: block; }
        .pump-btn.running { background: #10B981; color: white; animation: pulse 2s infinite; }
        .pump-btn.stopped { background: #EF4444; color: white; }
        .pump-btn.disabled { background: #9CA3AF; cursor: not-allowed; opacity: 0.6; }
        @keyframes pulse { 0% { box-shadow: 0 0 0 0 rgba(16,185,129,0.7); } 70% { box-shadow: 0 0 0 15px rgba(16,185,129,0); } 100% { box-shadow: 0 0 0 0 rgba(16,185,129,0); } }
        .config-section { margin-top: 20px; padding: 15px; background: #f8f9fa; border-radius: 12px; }
        .config-group { margin-bottom: 15px; }
        label { display: block; font-weight: bold; margin-bottom: 5px; color: #333; }
        input, select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 8px; font-size: 14px; }
        button { background: #667eea; color: white; border: none; padding: 12px 24px; border-radius: 10px; cursor: pointer; font-weight: bold; margin-top: 10px; margin-right: 10px; }
        button:hover { background: #5a67d8; }
        button.danger { background: #EF4444; }
        button.warning { background: #F59E0B; }
        .info-text { font-size: 11px; color: #666; margin-top: 5px; }
        .status-led { display: inline-block; width: 8px; height: 8px; border-radius: 4px; margin-right: 5px; }
        .status-online { background: #10B981; animation: pulse 2s infinite; }
        .status-offline { background: #EF4444; }
        .status-warning { background: #F59E0B; }
        .mac-address { font-family: monospace; font-size: 14px; background: #f0f0f0; padding: 8px; border-radius: 5px; text-align: center; }
        .terminal-log { background: #1e1e1e; color: #d4d4d4; border-radius: 8px; padding: 15px; font-family: 'Courier New', monospace; font-size: 12px; height: 400px; overflow-y: auto; margin-top: 15px; }
        .terminal-entry { padding: 8px; border-bottom: 1px solid #333; font-family: monospace; font-size: 11px; }
        .terminal-time { color: #6a9955; }
        .terminal-mac { color: #ce9178; }
        .terminal-data { color: #9cdcfe; }
        .terminal-parsed { color: #c586c0; }
        .terminal-valid { border-left: 3px solid #4ec9b0; background: #1e3a2e; }
        .terminal-invalid { border-left: 3px solid #f48771; background: #3a1e1a; }
        .espnow-stats { display: flex; gap: 20px; margin-bottom: 15px; padding: 10px; background: #f0f0f0; border-radius: 8px; flex-wrap: wrap; }
        .espnow-stat { font-size: 12px; }
        .espnow-stat span { font-weight: bold; color: #667eea; }
        .manual-request-btn { background: #F59E0B; font-size: 16px; padding: 12px 24px; margin: 10px 0; }
        .sensor-health { display: inline-block; padding: 5px 10px; border-radius: 20px; font-size: 12px; font-weight: bold; }
        .sensor-health.healthy { background: #10B981; color: white; }
        .sensor-health.warning { background: #F59E0B; color: white; }
        .sensor-health.dead { background: #EF4444; color: white; animation: pulse 1s infinite; }
        .strategy-selector { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px; }
        .strategy-card { background: white; border: 2px solid #e5e7eb; border-radius: 10px; padding: 10px; cursor: pointer; transition: all 0.3s; }
        .strategy-card.selected { border-color: #667eea; background: #EEF2FF; }
        .strategy-title { font-weight: bold; margin-bottom: 5px; }
        .strategy-desc { font-size: 11px; color: #666; }
        @media (max-width: 600px) { .stats-grid { grid-template-columns: 1fr; } .nav-buttons { flex-direction: column; } .strategy-selector { grid-template-columns: 1fr; } }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>💧 Smart Pump Controller</h1>
            <div class="version">v3.1.0 | ESP-NOW with Sensor Safety (Fixed Manual Mode)</div>
            <div class="offline-badge">🔌 OFFLINE MODE - No WiFi Client | ESP-NOW Only</div>
            
            <div class="nav-buttons">
                <button class="nav-btn active" onclick="showSection('dashboard')">Dashboard</button>
                <button class="nav-btn" onclick="showSection('espnow')">ESP-NOW</button>
                <button class="nav-btn" onclick="showSection('pump')">Pump Settings</button>
                <button class="nav-btn" onclick="showSection('safety')">⚡ Safety</button>
                <button class="nav-btn" onclick="showSection('stats')">Statistics</button>
                <button class="nav-btn" onclick="showSection('system')">System</button>
            </div>
            
            <div id="dashboardSection">
                <div class="config-section" style="margin-bottom:15px">
                    <div>📡 Sensor: <span id="sensorHealth" class="sensor-health healthy">Checking...</span>
                    <span id="sensorLastSeen" style="font-size:11px;margin-left:10px"></span></div>
                </div>
                <div class="water-level">
                    <div>Current Water Level</div>
                    <div class="level-value"><span id="waterLevel">0</span>%</div>
                    <div class="level-bar-container"><div class="level-bar" id="levelBar" style="width:0%">0%</div></div>
                    <div style="font-size:12px;margin-top:5px">Distance: <span id="distance">0</span> cm | Volume: <span id="volume">0</span> L</div>
                </div>
                <div class="stats-grid">
                    <div class="stat"><div class="stat-label">Power</div><div class="stat-value"><span id="power">0</span> W</div></div>
                    <div class="stat"><div class="stat-label">Voltage</div><div class="stat-value"><span id="voltage">0</span> V</div></div>
                    <div class="stat"><div class="stat-label">Current</div><div class="stat-value"><span id="current">0</span> A</div></div>
                    <div class="stat"><div class="stat-label">Energy</div><div class="stat-value"><span id="energy">0</span> kWh</div></div>
                </div>
                <div class="pf-card"><div class="stat-label">Power Factor</div><div class="pf-value"><span id="powerFactor">0.00</span></div><div id="pfQuality" style="font-size:11px;margin-top:5px"></div></div>
                
                <div class="mode-container">
                    <div class="mode-info"><div class="mode-label">Control Mode</div><div class="mode-status" id="modeStatus">🤖 Auto Mode</div></div>
                    <div class="switch-container">
                        <span class="switch-label">Manual</span>
                        <label class="switch"><input type="checkbox" id="modeToggle" onchange="toggleMode()"><div class="slider"></div></label>
                        <span class="switch-label">Auto</span>
                    </div>
                </div>
                
                <button id="pumpBtn" class="pump-btn stopped" onclick="togglePump()"><span id="pumpText">PUMP OFF</span></button>
                <div id="pumpReason" style="text-align:center;font-size:12px;margin-top:10px"></div>
                
                <div class="config-section">
                    <div><span class="status-led" id="espnowLed"></span> ESP-NOW: <span id="espnowStatus">Disabled</span></div>
                    <div>Data Source: <span id="dataSource">-</span></div>
                    <div>Active Strategy: <span id="activeStrategy">-</span></div>
                    <div>AP IP: 192.168.4.1</div>
                    <div id="lastUpdate" style="margin-top:10px;font-size:11px"></div>
                </div>
            </div>
            
            <div id="espnowSection" style="display:none">
                <h3>ESP-NOW Configuration</h3>
                <div class="config-group"><label>Enable ESP-NOW</label><select id="useEspNow"><option value="true">Enabled</option><option value="false">Disabled</option></select></div>
                <div class="config-group"><label>Sensor MAC Address</label><input type="text" id="peerMac" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17"><div class="info-text">Leave as FF:FF:FF:FF:FF:FF for broadcast</div></div>
                <div class="config-group"><label>WiFi Channel (1-13)</label><input type="number" id="espnowChannel" min="1" max="13" placeholder="1"></div>
                <button onclick="saveEspNow()">Save Settings</button>
                <h3 style="margin-top:20px">📡 Manual Data Request</h3>
                <button id="manualRequestBtn" class="manual-request-btn" onclick="manualGetMeasure()">🔄 MANUAL: Send 'get_measure'</button>
                <div id="sendStatus" style="display:inline-block;margin-left:10px"></div>
                <h3 style="margin-top:20px">📡 ESP-NOW Terminal Log</h3>
                <div class="espnow-stats"><div class="espnow-stat">📨 Messages: <span id="espnowMsgCount">0</span></div><div class="espnow-stat">⏱️ Last: <span id="espnowLastTime">Never</span></div><div class="espnow-stat">📡 Status: <span id="espnowStatusText">Inactive</span></div><div class="espnow-stat">🔢 Last Seq: <span id="lastSeq">-</span></div></div>
                <div style="margin-bottom:10px"><button onclick="refreshEspNowLog()" style="background:#10B981">🔄 Refresh</button><button onclick="clearEspNowLog()" class="danger">🗑️ Clear Log</button><button onclick="exportEspNowLog()" style="background:#F59E0B">📥 Export Log</button></div>
                <div id="espnowTerminal" class="terminal-log">Waiting for ESP-NOW messages...</div>
                <div class="info-text">💡 Device sends "get_measure" every 15 seconds<br>📝 Expected: {"d":351.9,"l":0.0,"v":0.0,"b":3.80}</div>
                <h3 style="margin-top:20px">🖥️ Device Info</h3>
                <div class="mac-address" id="deviceMac"></div>
            </div>
            
            <div id="pumpSection" style="display:none">
                <h3>Pump Control Settings</h3>
                <div class="config-group"><label>Low Threshold (Pump ON below %)</label><input type="number" id="lowThreshold" step="5" placeholder="30"></div>
                <div class="config-group"><label>High Threshold (Pump OFF above %)</label><input type="number" id="highThreshold" step="5" placeholder="80"></div>
                <div class="config-group"><label>Minimum Power Detection (Watts)</label><input type="number" id="minPower" step="1" placeholder="5"><div class="info-text">Pump stops if power below this for dry run protection</div></div>
                <div class="config-group"><label>Dry Run Protection (seconds)</label><input type="number" id="dryRunProtection" step="30" placeholder="300"></div>
                <button onclick="savePumpSettings()">Save Settings</button>
            </div>
            
            <div id="safetySection" style="display:none">
                <h3>🔒 Sensor Failure Protection</h3>
                <div class="info-text" style="margin-bottom:15px">⚠️ Strategy 0 (Stop Pump) now works in BOTH Auto and Manual modes!</div>
                <div class="config-group"><label>Sensor Timeout (seconds)</label><input type="number" id="sensorTimeout" step="10" placeholder="120"></div>
                <div class="config-group"><label>📋 Failure Strategy</label>
                    <div class="strategy-selector">
                        <div class="strategy-card" data-strategy="0" onclick="selectStrategy(0)"><div class="strategy-title">🛑 Stop Pump</div><div class="strategy-desc">Immediately stop pump for maximum safety (Auto & Manual)</div></div>
                        <div class="strategy-card" data-strategy="1" onclick="selectStrategy(1)"><div class="strategy-title">⚖️ Maintain State</div><div class="strategy-desc">Keep pump in last known safe state</div></div>
                        <div class="strategy-card" data-strategy="2" onclick="selectStrategy(2)"><div class="strategy-title">⚠️ Force ON</div><div class="strategy-desc">Force pump ON (RISKY - potential overflow!)</div></div>
                        <div class="strategy-card" data-strategy="3" onclick="selectStrategy(3)"><div class="strategy-title">🔄 Cyclic Mode</div><div class="strategy-desc">Run pump in cycles (e.g., 5min ON, 30min OFF)</div></div>
                    </div>
                </div>
                <div id="cyclicSettings" style="display:none"><div class="config-group"><label>ON Duration (seconds)</label><input type="number" id="cyclicOnTime" step="60" placeholder="300"></div><div class="config-group"><label>OFF Duration (seconds)</label><input type="number" id="cyclicOffTime" step="60" placeholder="1800"></div></div>
                <button onclick="saveSafetySettings()">Save Safety Settings</button>
                <h3 style="margin-top:20px">📊 Failure Log</h3>
                <div id="failureLog" class="terminal-log" style="height:200px"></div>
                <button onclick="clearFailureLog()" class="warning">Clear Log</button>
            </div>
            
            <div id="statsSection" style="display:none">
                <h3>Pump Statistics</h3>
                <div class="stats-grid">
                    <div class="stat"><div class="stat-label">Total Runtime</div><div class="stat-value" id="totalRuntime">0 min</div></div>
                    <div class="stat"><div class="stat-label">Total Energy</div><div class="stat-value" id="totalEnergy">0 kWh</div></div>
                    <div class="stat"><div class="stat-label">Pump Cycles</div><div class="stat-value" id="pumpCycles">0</div></div>
                    <div class="stat"><div class="stat-label">Last Start</div><div class="stat-value" id="lastStart">Never</div></div>
                </div>
                <button onclick="resetStats()">Reset Statistics</button>
                <button onclick="exportStats()">Export CSV</button>
            </div>
            
            <div id="systemSection" style="display:none">
                <h3>System</h3>
                <div class="config-group"><label>Device Name</label><input type="text" id="hostname" placeholder="Device Name"></div>
                <button onclick="saveSystem()">Save & Restart</button>
                <button class="danger" onclick="factoryReset()">Factory Reset</button>
                <button class="warning" onclick="rebootNow()">Reboot</button>
                <div style="margin-top:20px;padding:15px;background:#f0f0f0;border-radius:10px">
                    <strong>📡 Connection Info:</strong><br>SSID: <span id="apSsid"></span><br>Password: 12345678<br>IP: 192.168.4.1
                </div>
            </div>
        </div>
    </div>
    
    <script>
        let currentSection = 'dashboard', selectedStrategy = 0;
        function showSection(s) {
            currentSection = s;
            document.getElementById('dashboardSection').style.display = s==='dashboard'?'block':'none';
            document.getElementById('espnowSection').style.display = s==='espnow'?'block':'none';
            document.getElementById('pumpSection').style.display = s==='pump'?'block':'none';
            document.getElementById('safetySection').style.display = s==='safety'?'block':'none';
            document.getElementById('statsSection').style.display = s==='stats'?'block':'none';
            document.getElementById('systemSection').style.display = s==='system'?'block':'none';
            document.querySelectorAll('.nav-btn').forEach(btn=>btn.classList.remove('active'));
            event.target.classList.add('active');
            if(s==='espnow'){loadEspNowSettings();refreshEspNowLog();}
            if(s==='pump')loadPumpSettings();
            if(s==='safety'){loadSafetySettings();loadFailureLog();}
            if(s==='system')loadSystemSettings();
            if(s==='stats')loadStats();
        }
        async function toggleMode(){const mode=document.getElementById('modeToggle').checked?'auto':'manual';await fetch('/mode?mode='+mode);fetchData();updatePumpButtonState();}
        function updateModeUI(autoMode){const toggle=document.getElementById('modeToggle');const modeStatus=document.getElementById('modeStatus');const pumpBtn=document.getElementById('pumpBtn');if(autoMode){toggle.checked=true;modeStatus.innerHTML='🤖 Auto Mode';modeStatus.className='mode-status auto';pumpBtn.classList.add('disabled');}else{toggle.checked=false;modeStatus.innerHTML='👆 Manual Mode';modeStatus.className='mode-status manual';pumpBtn.classList.remove('disabled');}}
        function updatePumpButtonState(){const toggle=document.getElementById('modeToggle');const pumpBtn=document.getElementById('pumpBtn');if(toggle&&!toggle.checked)pumpBtn.classList.remove('disabled');else if(toggle&&toggle.checked)pumpBtn.classList.add('disabled');}
        function selectStrategy(s){selectedStrategy=s;document.querySelectorAll('.strategy-card').forEach(c=>c.classList.remove('selected'));document.querySelector(`.strategy-card[data-strategy="${s}"]`).classList.add('selected');document.getElementById('cyclicSettings').style.display=s===3?'block':'none';}
        async function manualGetMeasure(){const btn=document.getElementById('manualRequestBtn');const ot=btn.innerHTML;btn.innerHTML='⏳ Sending...';btn.disabled=true;document.getElementById('sendStatus').innerHTML='⏳ Sending...';try{const r=await fetch('/espnow/request');const t=await r.text();document.getElementById('sendStatus').innerHTML='✅ '+t;setTimeout(()=>document.getElementById('sendStatus').innerHTML='',3000);refreshEspNowLog();}catch(e){document.getElementById('sendStatus').innerHTML='❌ Failed';}finally{btn.innerHTML=ot;btn.disabled=false;}}
        function getPFClass(pf,running){if(!running)return'pf-idle';if(pf>=0.95)return'pf-good';if(pf>=0.8)return'pf-warning';return'pf-bad';}
        function getPFQuality(pf,running){if(!running)return'⏸ Idle (PF=0)';if(pf>=0.95)return'✓ Excellent';if(pf>=0.9)return'✓ Good';if(pf>=0.8)return'⚠ Fair';return' ';}
        async function loadSafetySettings(){const cfg=await fetchConfig();document.getElementById('sensorTimeout').value=cfg.sensor_timeout||120;selectedStrategy=cfg.failure_strategy||0;selectStrategy(selectedStrategy);document.getElementById('cyclicOnTime').value=cfg.cyclic_on_duration||300;document.getElementById('cyclicOffTime').value=cfg.cyclic_off_duration||1800;}
        async function loadEspNowSettings(){const cfg=await fetchConfig();document.getElementById('useEspNow').value=cfg.use_espnow?'true':'false';document.getElementById('peerMac').value=cfg.peer_mac||'FF:FF:FF:FF:FF:FF';document.getElementById('espnowChannel').value=cfg.espnow_channel||1;const r=await fetch('/info');const i=await r.json();document.getElementById('deviceMac').innerHTML=i.mac||'Unknown';document.getElementById('apSsid').innerHTML=i.ap_ssid||'Unknown';}
        async function loadPumpSettings(){const cfg=await fetchConfig();document.getElementById('lowThreshold').value=cfg.low_threshold||30;document.getElementById('highThreshold').value=cfg.high_threshold||80;document.getElementById('minPower').value=cfg.min_power||5;document.getElementById('dryRunProtection').value=cfg.dry_run_protection||300;}
        async function loadSystemSettings(){const cfg=await fetchConfig();document.getElementById('hostname').value=cfg.hostname||'s31-pump';}
        async function fetchConfig(){const r=await fetch('/config');return await r.json();}
        async function refreshEspNowLog(){const r=await fetch('/espnow/log');const h=await r.text();document.getElementById('espnowTerminal').innerHTML=h;}
        async function clearEspNowLog(){await fetch('/espnow/clear',{method:'POST'});refreshEspNowLog();}
        async function exportEspNowLog(){window.location.href='/espnow/export';}
        async function loadFailureLog(){const r=await fetch('/failurelog');const h=await r.text();document.getElementById('failureLog').innerHTML=h;}
        async function clearFailureLog(){await fetch('/failurelog/clear',{method:'POST'});loadFailureLog();}
        async function fetchData(){try{const r=await fetch('/data');const d=await r.json();document.getElementById('waterLevel').innerText=d.waterLevel.toFixed(1);document.getElementById('distance').innerText=d.distance.toFixed(1);document.getElementById('volume').innerText=d.volume.toFixed(0);document.getElementById('levelBar').style.width=d.waterLevel+'%';document.getElementById('levelBar').innerHTML=d.waterLevel.toFixed(0)+'%';document.getElementById('power').innerText=d.power.toFixed(1);document.getElementById('voltage').innerText=d.voltage.toFixed(1);document.getElementById('current').innerText=d.current.toFixed(2);document.getElementById('energy').innerText=d.energy.toFixed(2);const isRunning=d.pumpState;const pf=isRunning?d.powerFactor:0;document.getElementById('powerFactor').innerText=pf.toFixed(3);document.getElementById('powerFactor').className=getPFClass(pf,isRunning);document.getElementById('pfQuality').innerHTML=getPFQuality(pf,isRunning);const pumpBtn=document.getElementById('pumpBtn');const pumpText=document.getElementById('pumpText');if(isRunning){pumpBtn.className='pump-btn running';pumpText.innerText='PUMP ON';}else{pumpBtn.className='pump-btn stopped';pumpText.innerText='PUMP OFF';}document.getElementById('pumpReason').innerText=d.pumpReason;updateModeUI(d.autoMode);const sH=document.getElementById('sensorHealth');if(d.sensorHealthy){sH.className='sensor-health healthy';sH.innerHTML='✅ Sensor Healthy';}else if(d.sensorWarning){sH.className='sensor-health warning';sH.innerHTML='⚠️ Sensor Warning';}else{sH.className='sensor-health dead';sH.innerHTML='❌ Sensor Dead';}document.getElementById('sensorLastSeen').innerHTML=d.sensorLastSeen||'';document.getElementById('espnowStatus').innerHTML=d.espnowActive?'Active':'Disabled';document.getElementById('espnowLed').className='status-led '+(d.espnowActive?(d.espnowDataValid?'status-online':'status-warning'):'status-offline');document.getElementById('dataSource').innerHTML=d.dataSource||'No Data';document.getElementById('activeStrategy').innerHTML=d.activeStrategy||'-';document.getElementById('lastUpdate').innerHTML='Updated: '+new Date().toLocaleTimeString();if(currentSection==='espnow'){document.getElementById('espnowMsgCount').innerText=d.espnowMsgCount||0;document.getElementById('espnowLastTime').innerText=d.espnowLastTime||'Never';document.getElementById('espnowStatusText').innerHTML=d.espnowActive?(d.espnowDataValid?'✅ Receiving':'⚠️ No Data'):'❌ Disabled';document.getElementById('lastSeq').innerText=d.lastSeq||'-';}}catch(e){console.error(e);}}
        async function loadStats(){try{const r=await fetch('/stats');const s=await r.json();document.getElementById('totalRuntime').innerText=s.total_runtime+' min';document.getElementById('totalEnergy').innerText=s.total_energy+' kWh';document.getElementById('pumpCycles').innerText=s.pump_cycles;document.getElementById('lastStart').innerText=s.last_start||'Never';}catch(e){}}
        async function saveEspNow(){const s={use_espnow:document.getElementById('useEspNow').value==='true',peer_mac:document.getElementById('peerMac').value,espnow_channel:parseInt(document.getElementById('espnowChannel').value)};await fetch('/config/espnow',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(s)});alert('Saved! Rebooting...');setTimeout(()=>location.reload(),3000);}
        async function saveSafetySettings(){const s={sensor_timeout:parseInt(document.getElementById('sensorTimeout').value),failure_strategy:selectedStrategy,cyclic_on_duration:parseInt(document.getElementById('cyclicOnTime').value)||300,cyclic_off_duration:parseInt(document.getElementById('cyclicOffTime').value)||1800};await fetch('/config/safety',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(s)});alert('Safety settings saved!');}
        async function savePumpSettings(){const s={low_threshold:parseFloat(document.getElementById('lowThreshold').value),high_threshold:parseFloat(document.getElementById('highThreshold').value),min_power:parseFloat(document.getElementById('minPower').value),dry_run_protection:parseInt(document.getElementById('dryRunProtection').value)};await fetch('/config/pump',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(s)});alert('Pump settings saved!');}
        async function saveSystem(){const s={hostname:document.getElementById('hostname').value};await fetch('/config/system',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(s)});alert('Saved! Rebooting...');setTimeout(()=>location.reload(),3000);}
        async function togglePump(){const cfg=await fetchConfig();if(cfg.auto_mode){alert('Switch to Manual Mode first');return;}await fetch('/toggle');fetchData();}
        async function factoryReset(){if(confirm('FACTORY RESET - Erase ALL settings?')){await fetch('/factoryreset');}}
        async function rebootNow(){if(confirm('Reboot device?')){await fetch('/reboot');alert('Rebooting...');}}
        async function resetStats(){if(confirm('Reset statistics?')){await fetch('/resetstats');loadStats();}}
        async function exportStats(){window.location.href='/exportstats';}
        setInterval(fetchData,2000);setInterval(loadStats,10000);if(currentSection==='safety')setInterval(loadFailureLog,5000);fetchData();loadStats();
    </script>
</body>
</html>
)rawliteral";

// ================================================================================================
// @section     WEB SERVER SETUP
// ================================================================================================

/**
 * @brief       Setup all web server endpoints
 * 
 * Endpoints:
 * - /                 : Main HTML page
 * - /data             : JSON sensor data
 * - /config           : Configuration JSON
 * - /config/espnow    : Save ESP-NOW settings
 * - /config/safety    : Save safety settings
 * - /config/pump      : Save pump settings
 * - /config/system    : Save system settings
 * - /espnow/request   : Manual sensor request
 * - /espnow/log       : ESP-NOW log HTML
 * - /espnow/clear     : Clear ESP-NOW log
 * - /espnow/export    : Export ESP-NOW log as CSV
 * - /failurelog       : Failure log HTML
 * - /failurelog/clear : Clear failure log
 * - /mode             : Set auto/manual mode
 * - /toggle           : Toggle pump (manual mode only)
 * - /stats            : Pump statistics JSON
 * - /resetstats       : Reset statistics
 * - /exportstats      : Export stats as CSV
 * - /factoryreset     : Factory reset
 * - /reboot           : Reboot device
 * - /info             : Device info JSON
 */
void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", index_html); });
  
  server.on("/data", HTTP_GET, []() {
    updatePowerQuality();
    updateSensorHealth();
    StaticJsonDocument<1024> doc;
    doc["waterLevel"] = currentWaterLevel;
    doc["distance"] = currentDistance;
    doc["volume"] = currentVolume;
    doc["power"] = s31.getPower();
    doc["voltage"] = s31.getVoltage();
    doc["current"] = s31.getCurrent();
    doc["energy"] = s31.getEnergy();
    doc["powerFactor"] = powerFactor;
    doc["pumpState"] = s31.getRelayState();
    doc["autoMode"] = config.auto_mode;
    doc["espnowActive"] = espnow_initialized;
    doc["espnowDataValid"] = espnowDataValid;
    doc["espnowMsgCount"] = espnow_msg_counter;
    doc["lastSeq"] = espnow_seq;
    doc["sensorHealthy"] = !sensorIsDead && (millis() - lastEspNowData < ESP_NOW_DATA_TIMEOUT);
    doc["sensorWarning"] = !sensorIsDead && (millis() - lastEspNowData >= ESP_NOW_DATA_TIMEOUT);
    if (lastEspNowData > 0) doc["sensorLastSeen"] = String((millis() - lastEspNowData)/1000) + "s ago";
    else doc["sensorLastSeen"] = "Never";
    if (lastEspNowData > 0) doc["espnowLastTime"] = String((millis() - lastEspNowData)/1000) + "s ago";
    else doc["espnowLastTime"] = "Never";
    if (espnow_initialized && espnowDataValid && millis() - lastEspNowData < ESP_NOW_DATA_TIMEOUT) doc["dataSource"] = "ESP-NOW";
    else doc["dataSource"] = "No Data";
    String strategyName;
    switch(config.sensor_failure_strategy) {
      case 0: strategyName = "🛑 Stop Pump"; break;
      case 1: strategyName = "⚖️ Maintain"; break;
      case 2: strategyName = "⚠️ Force ON"; break;
      case 3: strategyName = "🔄 Cyclic"; break;
      default: strategyName = "Stop Pump";
    }
    doc["activeStrategy"] = strategyName;
    String reason = "";
    if (!config.auto_mode) reason = "Manual Control";
    else if (s31.getRelayState()) reason = "Water level low (" + String(currentWaterLevel,1) + "%)";
    else if (currentWaterLevel >= config.high_threshold) reason = "Tank full";
    else reason = "Level OK";
    doc["pumpReason"] = reason;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/config", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["low_threshold"] = config.low_threshold;
    doc["high_threshold"] = config.high_threshold;
    doc["min_power"] = config.min_power_threshold;
    doc["dry_run_protection"] = config.pump_protection_time;
    doc["use_espnow"] = config.use_espnow;
    doc["peer_mac"] = macToString(config.peer_mac);
    doc["espnow_channel"] = config.espnow_channel;
    doc["auto_mode"] = config.auto_mode;
    doc["hostname"] = deviceName;
    doc["sensor_timeout"] = config.sensor_timeout / 1000;
    doc["failure_strategy"] = config.sensor_failure_strategy;
    doc["cyclic_on_duration"] = config.cyclic_on_duration / 1000;
    doc["cyclic_off_duration"] = config.cyclic_off_duration / 1000;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/config/espnow", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("use_espnow")) config.use_espnow = doc["use_espnow"];
      if (doc.containsKey("peer_mac")) { String mac = doc["peer_mac"].as<String>(); stringToMac(mac, config.peer_mac); strcpy(config.peer_mac_str, mac.c_str()); }
      if (doc.containsKey("espnow_channel")) config.espnow_channel = doc["espnow_channel"];
      saveConfig();
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    }
  });
  
  server.on("/config/safety", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("sensor_timeout")) config.sensor_timeout = doc["sensor_timeout"].as<unsigned long>() * 1000;
      if (doc.containsKey("failure_strategy")) config.sensor_failure_strategy = doc["failure_strategy"];
      if (doc.containsKey("cyclic_on_duration")) config.cyclic_on_duration = doc["cyclic_on_duration"].as<unsigned long>() * 1000;
      if (doc.containsKey("cyclic_off_duration")) config.cyclic_off_duration = doc["cyclic_off_duration"].as<unsigned long>() * 1000;
      saveConfig();
      server.send(200, "text/plain", "OK");
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
  
  server.on("/config/system", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("hostname")) deviceName = doc["hostname"].as<String>();
      saveConfig();
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    }
  });
  
  server.on("/espnow/request", HTTP_GET, []() { manualRequestSensorData(); });
  server.on("/espnow/log", HTTP_GET, []() { server.send(200, "text/html", getEspNowLogHTML()); });
  server.on("/espnow/clear", HTTP_POST, []() { espnow_log.clear(); espnow_msg_counter = 0; server.send(200, "text/plain", "OK"); });
  server.on("/espnow/export", HTTP_GET, []() {
    String csv = "Timestamp,Age(s),MAC,RawData,Distance_cm,Level_Percent,Volume_Liters,Battery_Voltage,Valid\n";
    for (size_t i = 0; i < espnow_log.size(); i++) {
      EspNowLogEntry& e = espnow_log[i];
      unsigned long age = (millis() - e.timestamp) / 1000;
      csv += String(e.timestamp) + "," + String(age) + "," + e.mac + ",\"" + e.rawData + "\",";
      csv += String(e.distance,1) + "," + String(e.level,1) + "," + String(e.volume,1) + "," + String(e.battery,2) + "," + String(e.valid?"Yes":"No") + "\n";
    }
    server.send(200, "text/csv", csv);
  });
  
  server.on("/failurelog", HTTP_GET, []() { server.send(200, "text/html", getFailureLogHTML()); });
  server.on("/failurelog/clear", HTTP_POST, []() { failureLog.clear(); server.send(200, "text/plain", "OK"); });
  server.on("/mode", HTTP_GET, []() { if(server.hasArg("mode")){config.auto_mode=(server.arg("mode")=="auto");saveConfig();if(!config.auto_mode)s31.setRelay(false);} server.send(200,"text/plain","OK"); });
  server.on("/toggle", HTTP_GET, []() { if(!config.auto_mode)s31.toggleRelay(); server.send(200,"text/plain","OK"); });
  server.on("/stats", HTTP_GET, []() { StaticJsonDocument<512> doc; doc["total_runtime"]=pumpStats.totalRuntimeSeconds/60; doc["total_energy"]=pumpStats.totalEnergyKwh; doc["pump_cycles"]=pumpStats.pumpCycles; doc["last_start"]=pumpStats.lastStartStr; String response; serializeJson(doc,response); server.send(200,"application/json",response); });
  server.on("/resetstats", HTTP_GET, []() { pumpStats.totalRuntimeSeconds=0; pumpStats.totalEnergyKwh=0; pumpStats.pumpCycles=0; server.send(200,"text/plain","OK"); });
  server.on("/exportstats", HTTP_GET, []() { String csv="Timestamp,Runtime(min),Energy(kWh),Cycles,LastStart\n"; csv+=String(millis()/1000)+","+String(pumpStats.totalRuntimeSeconds/60)+","+String(pumpStats.totalEnergyKwh)+","+String(pumpStats.pumpCycles)+","+String(pumpStats.lastStartStr)+"\n"; server.send(200,"text/csv",csv); });
  server.on("/factoryreset", HTTP_GET, []() { server.send(200,"text/plain","Factory resetting..."); delay(100); factoryReset(); });
  server.on("/reboot", HTTP_GET, []() { server.send(200,"text/plain","Rebooting..."); delay(100); rebootDevice(); });
  server.on("/info", HTTP_GET, []() { String json="{\"mac\":\""+WiFi.macAddress()+"\",\"ap_ssid\":\""+WiFi.softAPSSID()+"\",\"ip\":\"192.168.4.1\"}"; server.send(200,"application/json",json); });
}

// ================================================================================================
// @section     SETUP & LOOP
// ================================================================================================

/**
 * @brief       Arduino setup function - runs once on boot
 * 
 * Initialization order:
 * 1. Load configuration from EEPROM
 * 2. Initialize Sonoff power monitoring
 * 3. Setup Access Point mode
 * 4. Initialize ESP-NOW
 * 5. Setup web server
 * 6. Setup OTA updates
 * 7. Setup mDNS
 */
void setup() {
  loadConfig();                     // Load saved settings from EEPROM
  
  uint32_t chipId = ESP.getChipId();
  deviceName = "s31-pump-" + String(chipId & 0xFFFF, HEX);
  
  s31.begin();                      // Initialize Sonoff S31 power monitoring
  setupAPMode();                    // Create WiFi Access Point
  initEspNow();                     // Initialize ESP-NOW communication
  setupWebServer();                 // Setup HTTP endpoints
  setupArduinoOTA();                // Setup Over-The-Air updates
  
  if (ENABLE_MDNS) {
    MDNS.begin(deviceName.c_str());
    MDNS.addService("http", "tcp", 80);
  }
  
  server.begin();                   // Start web server
}

/**
 * @brief       Arduino main loop - runs continuously
 * 
 * Task scheduling with timing constraints:
 * - Sonoff updates every S31_UPDATE_INTERVAL
 * - OTA handling every OTA_INTERVAL
 * - Web server handling every WEB_SERVER_INTERVAL
 * - ESP-NOW requests at configured interval
 * - Pump control every CONTROL_INTERVAL
 * - mDNS updates every MDNS_INTERVAL
 * 
 * AQUIRE_TIMEOUT prevents any single task from exceeding time budget
 */
void loop() {
  unsigned long currentMillis = millis();
  
  // Update Sonoff power monitoring (placeholder - actual update happens in web requests)
  if (currentMillis - lastS31Update >= S31_UPDATE_INTERVAL) {
    lastS31Update = currentMillis;
    s31.update();
  }
  
  unsigned long startTime = micros();
  
  // Handle OTA updates
  if (ENABLE_OTA && currentMillis - lastOTA >= OTA_INTERVAL) {
    lastOTA = currentMillis;
    ArduinoOTA.handle();
  }
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  // Handle web server requests
  if (currentMillis - lastWebServer >= WEB_SERVER_INTERVAL) {
    lastWebServer = currentMillis;
    server.handleClient();
  }
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  // Send automatic sensor data requests
  requestSensorData();
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  // Run pump control logic (with sensor failure protection for both modes)
  controlPump();
  if (micros() - startTime > AQUIRE_TIMEOUT) return;
  
  // Update mDNS
  if (ENABLE_MDNS && currentMillis - lastMDNS >= MDNS_INTERVAL) {
    lastMDNS = currentMillis;
    MDNS.update();
  }
  
  delay(1);  // Yield to prevent watchdog timeout
}