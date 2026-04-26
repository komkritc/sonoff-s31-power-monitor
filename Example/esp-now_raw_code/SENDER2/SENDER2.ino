// water level sensor node

#include <ESP8266WiFi.h>
#include <espnow.h>

// =====================================================
// DEVICE NAME RESPONSE
#define DEVICE_NAME "esp8266 sensor node"

// =====================================================
// LED MEANING:
// 🔵 Blue LED  → ON while sending packet
// 🟢 Green LED → Blink when packet successfully sent
// 🔴 Red LED   → Blink when packet send fails

// ================= SAFE LED PINS =================
#define LED_BLUE D4
#define LED_RED D7
#define LED_GREEN D5

#define LED_ON HIGH
#define LED_OFF LOW

// ================= BROADCAST =================
uint8_t broadcastMac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ================= DATA STRUCT =================
typedef struct {
  uint32_t seq;
  uint32_t timestamp;
  char msg[64];  // message / command
} Packet;

// ================= VARIABLES =================
Packet outgoing;
Packet incoming;

uint32_t seq = 0;
unsigned long lastSend = 0;

// LED non-blocking
unsigned long ledTimer = 0;
bool ledActive = false;
int ledPin = -1;

// Flags
volatile bool sendOK = false;
volatile bool sendFail = false;

// =====================================================
// CALLBACK: SEND STATUS
void onSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) sendOK = true;
  else sendFail = true;
}

// =====================================================
// CALLBACK: RECEIVE DATA
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {

  if (len != sizeof(Packet)) return;

  memcpy(&incoming, data, sizeof(incoming));

  Serial.print("RX from: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" | msg=");
  Serial.println(incoming.msg);

  // =================================================
  // 🔥 COMMAND: "whoru"
  if (strcmp(incoming.msg, "whoru") == 0) {

    Serial.println("Command received: whoru → replying");

    Packet reply;
    reply.seq = incoming.seq;
    reply.timestamp = micros();

    // safer than strncpy
    snprintf(reply.msg, sizeof(reply.msg), "%s", DEVICE_NAME);

    esp_now_send(mac, (uint8_t *)&reply, sizeof(reply));
    return;  // ✅ stop here
  }

  // =================================================
  // 🔥 COMMAND: "get_measure"
  if (strcmp(incoming.msg, "get_measure") == 0) {

    Packet reply;
    reply.seq = incoming.seq;
    reply.timestamp = micros();

    // 🔥 Fake sensor values
    float distance = random(50, 3000) / 10.0;  // cm
    float tankHeight = 300.0;

    float levelPercent = 100.0 * (1.0 - (distance / tankHeight));
    levelPercent = constrain(levelPercent, 0, 100);

    float maxVolume = 2000.0;
    float volume = (levelPercent / 100.0) * maxVolume;

    float batteryVoltage = random(330, 420) / 100.0;

    // ✅ Ultra-compact JSON
    snprintf(reply.msg, sizeof(reply.msg),
             "{\"d\":%.1f,\"l\":%.1f,\"v\":%.1f,\"b\":%.2f}",
             distance,
             levelPercent,
             volume,
             batteryVoltage);

    Serial.print("TX size = ");
    Serial.println(strlen(reply.msg));  // should be ~40–60 bytes

    esp_now_send(mac, (uint8_t *)&reply, sizeof(reply));
    return;
  }
}

// =====================================================
// SETUP
void setup() {

  Serial.begin(115200);
  delay(300);

  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  digitalWrite(LED_BLUE, LED_OFF);
  digitalWrite(LED_RED, LED_OFF);
  digitalWrite(LED_GREEN, LED_OFF);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("===== ESP8266 SENSOR NODE =====");
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true) {
      digitalWrite(LED_RED, LED_ON);
      delay(100);
      digitalWrite(LED_RED, LED_OFF);
      delay(100);
    }
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Add broadcast peer
  if (esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) {
    Serial.println("Failed to add broadcast peer");
  }

  Serial.println("Sensor Node Ready");
}

// =====================================================
// LOOP
void loop() {

  // // ================= PERIODIC BROADCAST =================
  // if (millis() - lastSend >= 2000) {

  //   lastSend = millis();

  //   outgoing.seq = seq++;
  //   outgoing.timestamp = micros();
  //   strcpy(outgoing.msg, "ping");

  //   digitalWrite(LED_BLUE, LED_ON);

  //   esp_now_send(broadcastMac,
  //                (uint8_t *)&outgoing,
  //                sizeof(outgoing));
  // }

  // ================= SEND RESULT =================
  if (sendOK) {
    sendOK = false;
    digitalWrite(LED_BLUE, LED_OFF);

    digitalWrite(LED_GREEN, LED_ON);
    ledTimer = millis();
    ledActive = true;
    ledPin = LED_GREEN;
  }

  if (sendFail) {
    sendFail = false;
    digitalWrite(LED_BLUE, LED_OFF);

    digitalWrite(LED_RED, LED_ON);
    ledTimer = millis();
    ledActive = true;
    ledPin = LED_RED;
  }

  // ================= NON-BLOCKING LED =================
  if (ledActive && millis() - ledTimer > 50) {
    digitalWrite(ledPin, LED_OFF);
    ledActive = false;
  }

  yield();  // IMPORTANT
}