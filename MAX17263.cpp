/*
MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "MAX17263.h"
#include <Wire.h>

// Check if battery is present by examining the status register
bool MAX17263::batteryPresent() {
    uint16_t status = getStatus();
    // Check BSt bit (bit 3) - 0 means battery present
    return !(status & 0x0008);
}

// Check if a power-on reset event has occurred
bool MAX17263::powerOnResetEvent() {
    uint16_t status = getStatus();
    // Check POR bit (bit 1)
    return (status & 0x0002);
}

// Initialize the MAX17263 fuel gauge
void MAX17263::initialize() {
    // Exit hibernate mode first
    exitHibernate();
    
    // Store original hibernate configuration
    storeHibernateCFG();
    
    // Wait for data to be ready
    if (!waitForDNRdataNotReady()) {
        return; // Timeout or error
    }
    
    // Clear power-on reset flag
    clearPORpowerOnReset();
    
    // Calculate multipliers based on sense resistor
    calcMultipliers(rSense);
    
    // Configure EZ model
    setEZconfig();
    
    // Configure LED settings if needed
    setLEDCfg1();
    setLEDCfg2();
    
    // Restore hibernate configuration
    restoreHibernateCFG();
}

// Production test function
void MAX17263::productionTest() {
    // Read and verify key registers
    uint16_t status = getStatus();
    uint16_t modelCfg = readReg16Bit(regModelCfg);
    uint16_t designCapReg = readReg16Bit(regDesignCap);
    
    // Verify values are within expected ranges
    if (status == 0xFFFF) {
        // Communication error
        return;
    }
    
    // Additional production tests can be added here
    // For example: verify voltage, current readings are reasonable
    float voltage = getVcell();
    if (voltage < 2.5 || voltage > 4.5) {
        // Voltage out of expected range for Li-ion
    }
}

// Get current in mA
float MAX17263::getCurrent() {
    int16_t currentRaw = (int16_t)readReg16Bit(regCurrent);
    return (float)currentRaw * current_multiplier_mV;
}

// Get cell voltage in V
float MAX17263::getVcell() {
    uint16_t vcellRaw = readReg16Bit(regVCell);
    return (float)vcellRaw * voltage_multiplier_V;
}

// Get capacity in mAh
float MAX17263::getCapacity_mAh() {
    uint16_t repCap = readReg16Bit(regRepCap);
    return (float)repCap * capacity_multiplier_mAH;
}

// Get state of charge in %
float MAX17263::getSOC() {
    uint16_t soc = readReg16Bit(regRepSOC);
    return (float)soc * SOC_multiplier;
}

// Get time to empty in hours
float MAX17263::getTimeToEmpty() {
    uint16_t tte = readReg16Bit(regTimeToEmpty);
    if (tte == 0xFFFF) {
        return -1; // Indicates charging or no valid estimate
    }
    return (float)tte * time_multiplier_Hours;
}

// Get temperature in Celsius
float MAX17263::getTemp() {
    int16_t tempRaw = (int16_t)readReg16Bit(regTemp);
    // Temperature is in 1/256 degrees Celsius
    return (float)tempRaw / 256.0;
}

// Get average cell voltage
float MAX17263::getAvgVCell() {
    uint16_t avgVcellRaw = readReg16Bit(regAvgVCell);
    return (float)avgVcellRaw * voltage_multiplier_V;
}

// Private functions

// Get status register
uint16_t MAX17263::getStatus() {
    return readReg16Bit(regStatus);
}

// Wait for DNR (Data Not Ready) bit to clear
bool MAX17263::waitForDNRdataNotReady() {
    uint32_t timeout = millis() + 1000; // 1 second timeout
    
    while (millis() < timeout) {
        uint16_t fstat = readReg16Bit(regFStat);
        // Check DNR bit (bit 0)
        if (!(fstat & 0x0001)) {
            return true; // Data is ready
        }
        delay(10);
    }
    return false; // Timeout
}

// Clear power-on reset flag
void MAX17263::clearPORpowerOnReset() {
    uint16_t status = readReg16Bit(regStatus);
    // Clear POR bit by writing 0 to bit 1, keep other bits
    status &= ~0x0002;
    writeReg16Bit(regStatus, status);
}

// Calculate current and capacity multipliers based on sense resistor
void MAX17263::calcMultipliers(float rSense) {
    // Current LSB = 1.5625μV / Rsense
    current_multiplier_mV = 1.5625e-6 / rSense * 1000; // Convert to mA
    
    // Capacity LSB = 5μVh / Rsense
    capacity_multiplier_mAH = 5.0e-6 / rSense; // Convert to mAh
}

// Set design capacity
void MAX17263::setDesignCap_mAh(long c) {
    uint16_t designCapReg = (uint16_t)(c / capacity_multiplier_mAH);
    writeReg16Bit(regDesignCap, designCapReg);
}

// Set charge termination current
void MAX17263::setIchgTerm(uint16_t i) {
    writeReg16Bit(regIchgTerm, i);
}

// Set empty voltage
void MAX17263::setVEmpty(float vf) {
    // VEmpty register format: bit 15-7 for VE (10mV resolution), bit 6-0 for VR (40mV resolution)
    uint16_t ve = (uint16_t)(vf * 100); // Convert to 10mV units
    ve = (ve << 7) | 0x0A; // Default VR value
    writeReg16Bit(regVEmpty, ve);
}

// Refresh model configuration
void MAX17263::refreshModelCFG(bool r100, bool vChg, byte modelID) {
    uint16_t modelCfg = readReg16Bit(regModelCfg);
    
    // Clear refresh bit and model bits
    modelCfg &= ~0x8F00;
    
    // Set model ID (bits 4-7)
    modelCfg |= ((modelID & 0x0F) << 4);
    
    // Set R100 bit (bit 13) if needed
    if (r100) {
        modelCfg |= 0x2000;
    }
    
    // Set VChg bit (bit 10) if needed
    if (vChg) {
        modelCfg |= 0x0400;
    }
    
    // Set refresh bit (bit 15)
    modelCfg |= 0x8000;
    
    writeReg16Bit(regModelCfg, modelCfg);
}

// Wait for model configuration refresh to complete
bool MAX17263::waitforModelCFGrefreshReady() {
    uint32_t timeout = millis() + 1000; // 1 second timeout
    
    while (millis() < timeout) {
        uint16_t modelCfg = readReg16Bit(regModelCfg);
        // Check if refresh bit (bit 15) is cleared
        if (!(modelCfg & 0x8000)) {
            return true; // Refresh complete
        }
        delay(10);
    }
    return false; // Timeout
}

// Configure EZ model
void MAX17263::setEZconfig() {
    // Wait for any ongoing operations
    waitForDNRdataNotReady();
    
    // Set design capacity
    setDesignCap_mAh(designCap_mAh);
    
    // Set charge termination current
    setIchgTerm(ichgTerm);
    
    // Set empty voltage
    setVEmpty(vEmpty);
    
    // Refresh model configuration
    refreshModelCFG(r100, vChg, modelID);
    
    // Wait for refresh to complete
    waitforModelCFGrefreshReady();
}

// Exit hibernate mode
void MAX17263::exitHibernate() {
    // Write to HibCfg register to exit hibernate
    writeReg16Bit(regHibCfg, 0x0000);
    
    // Write to Status register to wake up
    writeReg16Bit(regStatus, 0x0000);
    
    delay(10); // Allow time to wake up
}

// Store original hibernate configuration
void MAX17263::storeHibernateCFG() {
    originalHibernateCFG = readReg16Bit(regHibCfg);
}

// Restore hibernate configuration
void MAX17263::restoreHibernateCFG() {
    writeReg16Bit(regHibCfg, originalHibernateCFG);
}

// Configure LED settings 1
void MAX17263::setLEDCfg1() {
    // Example LED configuration - adjust as needed
    // Enable LED, set timing and thresholds
    uint16_t ledCfg1 = 0x0570; // Example value
    writeReg16Bit(regLedCfg1, ledCfg1);
}

// Configure LED settings 2
void MAX17263::setLEDCfg2() {
    // Example LED configuration - adjust as needed
    uint16_t ledCfg2 = 0x0000; // Example value
    writeReg16Bit(regLedCfg2, ledCfg2);
}

// Read 16-bit register
uint16_t MAX17263::readReg16Bit(byte reg) {
    Wire.beginTransmission(I2CAddress);
    Wire.write(reg);
    Wire.endTransmission(false);
    
    Wire.requestFrom(I2CAddress, (byte)2);
    
    uint16_t value = 0;
    if (Wire.available() >= 2) {
        value = Wire.read();
        value |= (uint16_t)Wire.read() << 8;
    }
    
    return value;
}

// Write 16-bit register
void MAX17263::writeReg16Bit(byte reg, uint16_t value) {
    Wire.beginTransmission(I2CAddress);
    Wire.write(reg);
    Wire.write(value & 0xFF);        // LSB
    Wire.write((value >> 8) & 0xFF); // MSB
    Wire.endTransmission();
}