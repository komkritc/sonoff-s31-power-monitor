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
}