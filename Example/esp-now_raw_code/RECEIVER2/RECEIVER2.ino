// S31 control water pump
#include <ESP8266WiFi.h>
#include <espnow.h>

// =====================================================
// SEND "get_measure" → expect reply from sensor node

// ================= BROADCAST =================
uint8_t broadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ================= DATA STRUCT =================
typedef struct {
  uint32_t seq;
  uint32_t timestamp;
  char msg[64];
} Packet;

Packet outgoing;
Packet incoming;

uint32_t seq = 0;
unsigned long lastSend = 0;

// =====================================================
// CALLBACK: SEND STATUS
void onSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Send Status: ");
  Serial.println(sendStatus == 0 ? "OK" : "FAIL");
}

// =====================================================
// CALLBACK: RECEIVE RESPONSE
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {

  if (len != sizeof(Packet)) return;

  memcpy(&incoming, data, sizeof(incoming));

  Serial.print("Reply from: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }

  Serial.print(" | msg=");
  Serial.println(incoming.msg);
}

// =====================================================
// SETUP
void setup() {

  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("===== ESP8266 ESP-NOW SENDER =====");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true);
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Add broadcast peer
  if (esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) {
    Serial.println("Failed to add broadcast peer");
  }

  Serial.println("Ready to send 'get_measure'");
}

// =====================================================
// LOOP
void loop() {

  if (millis() - lastSend >= 15000) {

    lastSend = millis();

    outgoing.seq = seq++;
    outgoing.timestamp = micros();
    strcpy(outgoing.msg, "get_measure");

    Serial.println("Sending: get_measure");

    esp_now_send(broadcastMac,
                 (uint8_t *)&outgoing,
                 sizeof(outgoing));
  }

  yield();  // IMPORTANT for ESP8266
}