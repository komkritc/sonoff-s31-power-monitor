#include "SonoffS31.h"

SonoffS31::SonoffS31(int rxPin, int relayPin) {
    _rxPin = rxPin;
    _relayPin = relayPin;
    _baudRate = 4800;
    _debug = false;
    
    // Initialize variables
    _voltage = 0.0;
    _current = 0.0;
    _power = 0.0;
    _energy = 0.0;
    _accumulatedEnergy = 0.0;
    _lastPower = 0.0;
    _lastEnergyCalc = 0;
    
    // Calibration values (will be updated from sensor)
    _voltageCal = 1912;
    _currentCal = 16140;
    _powerCal = 5364;
    
    _relayState = false;
    _byteCounter = 0;
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
}

bool SonoffS31::begin(long baudRate) {
    _baudRate = baudRate;
    
    // Initialize Serial for CSE7766
    Serial.begin(_baudRate, SERIAL_8E1);
    delay(100);
    
    // Flush any pending data
    while (Serial.available()) {
        Serial.read();
    }
    
    // Initialize relay pin
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, LOW);
    _relayState = false;
    
    _lastEnergyCalc = millis();
    
    if (_debug) {
        Serial.println("SonoffS31 initialized");
        Serial.print("Baud rate: ");
        Serial.println(_baudRate);
        Serial.print("Relay pin: ");
        Serial.println(_relayPin);
    }
    
    return true;
}

void SonoffS31::update() {
    readSensor();
}

void SonoffS31::readSensor() {
    while (Serial.available()) {
        int rb = Serial.read();
        if (rb != -1) {
            processByte((uint8_t)rb);
        }
    }
}

void SonoffS31::processByte(uint8_t b) {
    _rxBuffer[_byteCounter++] = b;

    if (_byteCounter == CSE_PACKET_LEN) {
        // Calculate checksum
        uint8_t checksum = 0;
        for (int i = 2; i < (CSE_PACKET_LEN - 1); i++) {
            checksum += _rxBuffer[i];
        }
        
        if (checksum == _rxBuffer[CSE_PACKET_LEN - 1]) {
            parsePacket();
        } else if (_debug) {
            Serial.println("CSE Checksum error");
        }
        _byteCounter = 0;
    }

    // Byte alignment
    if (_byteCounter == 1 && _rxBuffer[0] != CSE_HEADER1) {
        _byteCounter = 0;
    }
    if (_byteCounter == 2 && _rxBuffer[1] != CSE_HEADER2) {
        _rxBuffer[0] = _rxBuffer[1];
        _byteCounter = (_rxBuffer[0] == CSE_HEADER1) ? 1 : 0;
    }
}

void SonoffS31::parsePacket() {
    uint8_t header = _rxBuffer[0];
    uint8_t adj = _rxBuffer[20];

    // Update calibration values from packet
    if (header != 0xAA) {
        long newVoltageCal = ((_rxBuffer[2] << 16) | (_rxBuffer[3] << 8) | _rxBuffer[4]) / 100;
        long newCurrentCal = (_rxBuffer[8] << 16) | (_rxBuffer[9] << 8) | _rxBuffer[10];
        long newPowerCal = ((_rxBuffer[14] << 16) | (_rxBuffer[15] << 8) | _rxBuffer[16]) / 1000;
        
        if (newVoltageCal > 0 && newVoltageCal < 10000) {
            _voltageCal = newVoltageCal;
        }
        if (newCurrentCal > 0 && newCurrentCal < 100000) {
            _currentCal = newCurrentCal;
        }
        if (newPowerCal > 0 && newPowerCal < 10000) {
            _powerCal = newPowerCal;
        }
    }

    // Calculate measurements
    long voltageCycle = (_rxBuffer[5] << 16) | (_rxBuffer[6] << 8) | _rxBuffer[7];
    long currentCycle = (_rxBuffer[11] << 16) | (_rxBuffer[12] << 8) | _rxBuffer[13];
    long powerCycle = (_rxBuffer[17] << 16) | (_rxBuffer[18] << 8) | _rxBuffer[19];

    // Voltage
    if ((adj & 0x40) && voltageCycle > 0) {
        float newVoltage = (_voltageCal * 100.0) / voltageCycle;
        if (newVoltage > 50 && newVoltage < 300) {
            _voltage = newVoltage;
        }
    }

    // Power
    if ((adj & 0x10) && powerCycle > 0) {
        if ((header & 0xF2) != 0xF2) {
            float newPower = (_powerCal * 1000.0) / powerCycle;
            if (newPower >= 0 && newPower < 5000) {
                _power = newPower;
            }
        }
    }

    // Current
    if ((adj & 0x20) && currentCycle > 0 && _power > 0.1) {
        float newCurrent = _currentCal / (float)currentCycle;
        if (newCurrent >= 0 && newCurrent < 20) {
            _current = newCurrent;
        }
    }
    
    // Calculate energy
    calculateEnergy();
}

void SonoffS31::calculateEnergy() {
    unsigned long now = millis();
    if (_lastEnergyCalc > 0 && _lastPower > 0) {
        float hoursElapsed = (now - _lastEnergyCalc) / 3600000.0;
        float avgPower = (_power + _lastPower) / 2.0;
        _accumulatedEnergy += (avgPower / 1000.0) * hoursElapsed;
        _energy = _accumulatedEnergy;
    }
    _lastPower = _power;
    _lastEnergyCalc = now;
}

float SonoffS31::getVoltage() {
    return _voltage;
}

float SonoffS31::getCurrent() {
    return _current;
}

float SonoffS31::getPower() {
    return _power;
}

float SonoffS31::getEnergy() {
    return _energy;
}

float SonoffS31::getApparentPower() {
    return _voltage * _current;
}

float SonoffS31::getPowerFactor() {
    float apparentPower = getApparentPower();
    if (apparentPower > 0) {
        return _power / apparentPower;
    }
    return 0.0;
}

PowerData SonoffS31::getPowerData() {
    PowerData data;
    data.voltage = _voltage;
    data.current = _current;
    data.power = _power;
    data.energy = _energy;
    data.apparentPower = getApparentPower();
    data.powerFactor = getPowerFactor();
    data.relayState = _relayState;
    data.isValid = (_voltage > 0 && _power >= 0);
    return data;
}

void SonoffS31::relayOn() {
    digitalWrite(_relayPin, HIGH);
    _relayState = true;
}

void SonoffS31::relayOff() {
    digitalWrite(_relayPin, LOW);
    _relayState = false;
}

void SonoffS31::relayToggle() {
    if (_relayState) {
        relayOff();
    } else {
        relayOn();
    }
}

bool SonoffS31::getRelayState() {
    return _relayState;
}

void SonoffS31::resetEnergy() {
    _accumulatedEnergy = 0.0;
    _energy = 0.0;
    _lastEnergyCalc = millis();
}

void SonoffS31::setEnergy(float kWh) {
    _accumulatedEnergy = kWh;
    _energy = kWh;
}

void SonoffS31::enableDebug(bool enable) {
    _debug = enable;
}