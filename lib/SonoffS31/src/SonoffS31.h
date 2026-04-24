#ifndef SonoffS31_h
#define SonoffS31_h

#include <Arduino.h>

// Relay states
#define RELAY_ON HIGH
#define RELAY_OFF LOW

// CSE7766 constants
#define CSE_HEADER1 0x55
#define CSE_HEADER2 0x5A
#define CSE_PACKET_LEN 24

// Power data structure
struct PowerData {
    float voltage;
    float current;
    float power;
    float energy;
    float apparentPower;
    float powerFactor;
    bool relayState;
    bool isValid;
};

class SonoffS31 {
public:
    // Constructor
    SonoffS31(int rxPin = 3, int relayPin = 12);
    
    // Initialization
    bool begin(long baudRate = 4800);
    void update();
    
    // Power monitoring
    float getVoltage();
    float getCurrent();
    float getPower();
    float getEnergy();
    float getApparentPower();
    float getPowerFactor();
    PowerData getPowerData();
    
    // Relay control
    void relayOn();
    void relayOff();
    void relayToggle();
    bool getRelayState();
    
    // Energy management
    void resetEnergy();
    void setEnergy(float kWh);
    
    // Debug
    void enableDebug(bool enable);
    
private:
    int _rxPin;
    int _relayPin;
    long _baudRate;
    bool _debug;
    
    // CSE7766 variables
    uint8_t _rxBuffer[25];
    int _byteCounter;
    
    // Power measurements
    float _voltage;
    float _current;
    float _power;
    float _energy;
    float _accumulatedEnergy;
    float _lastPower;
    unsigned long _lastEnergyCalc;
    
    // Calibration values
    long _voltageCal;
    long _currentCal;
    long _powerCal;
    
    // Relay state
    bool _relayState;
    
    // Internal methods
    void processByte(uint8_t b);
    void parsePacket();
    void calculateEnergy();
    void readSensor();
};

#endif