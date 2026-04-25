#include <SonoffS31.h>

SonoffS31 s31(12);  // Relay on GPIO12

void setup() {
    Serial.begin(115200);
    s31.begin();
    
    // Optional: Set callbacks
    s31.onPowerUpdate([](float power, float voltage, float current) {
        Serial.printf("Power: %.1fW, Voltage: %.1fV, Current: %.3fA\n", 
                      power, voltage, current);
    });
    
    s31.onRelayChange([](bool state) {
        Serial.printf("Relay changed to: %s\n", state ? "ON" : "OFF");
    });
    
    // Turn on relay
    s31.setRelay(true);
}

void loop() {
    s31.update();  // Must call this regularly
    
    // Read measurements anytime
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 2000) {
        Serial.printf("Power: %.1f W, Energy: %.3f kWh\n", 
                      s31.getPower(), s31.getEnergy());
        lastPrint = millis();
    }
    
    // Example: Toggle relay every 10 seconds
    static unsigned long lastToggle = 0;
    if (millis() - lastToggle > 10000) {
        s31.toggleRelay();
        lastToggle = millis();
    }
    
    delay(10);
}