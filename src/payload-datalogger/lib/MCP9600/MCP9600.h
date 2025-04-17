#ifndef MCP9600_H
#define MCP9600_H

#include <Arduino.h>
#include <Wire.h>

// regsisters
#define MCP9600_HOTJUNCTION 0x00
#define MCP9600_JUNCTIONDELTA 0x01
#define MCP9600_COLDJUNCTION 0x02
#define MCP9600_RAWDATAADC 0x03
#define MCP9600_STATUS 0x04
#define MCP9600_SENSORCONFIG 0x05
#define MCP9600_DEVICECONFIG 0x06
#define MCP9600_DEVICEID 0x20

#define MCP9600_ALERT1_CONFIG 0x08
#define MCP9600_ALERT2_CONFIG 0x09
#define MCP9600_ALERT3_CONFIG 0x0A
#define MCP9600_ALERT4_CONFIG 0x0B
#define MCP9600_ALERT1_HYSTERESIS 0x0C
#define MCP9600_ALERT2_HYSTERESIS 0x0D
#define MCP9600_ALERT3_HYSTERESIS 0x0E
#define MCP9600_ALERT4_HYSTERESIS 0x0F
#define MCP9600_ALERT1_LIMIT 0x10
#define MCP9600_ALERT2_LIMIT 0x11
#define MCP9600_ALERT3_LIMIT 0x12
#define MCP9600_ALERT4_LIMIT 0x13

class MCP9600
{
    // arguments: wire, address
    public:
        MCP9600(uint8_t address) : address(address) {}

        bool checkDeviceID();
        bool isTemperatureReady();
        bool resetConfiguration();

        // Cold junction temperature resolution
        // 0 - 0.0625 C (250ms conversion time)
        // 1 - 0.25 C (63ms conversion time)
        // datasheet page 32
        bool setAmbientResolution(uint8_t resolution);

        // Thermocouple temperature ADC resolution
        // 0 - 18-bit
        // 1 - 16-bit
        // 2 - 14-bit
        // 3 - 12-bit
        // datasheet page 32
        bool setADCResolution(uint8_t resolution);

        // Thermocouple type
        // 0 - K
        // 1 - J
        // 2 - T
        // 3 - N
        // 4 - S
        // 5 - E
        // 6 - B
        // 7 - R
        // datasheet page 31
        bool setThermocoupleType(uint8_t type);


        // Filter coefficient
        // 0-7 where 0 is no filter and 7 is the most aggressive filter
        // datasheet page 31
        bool setFilterCoefficient(uint8_t coeff);

        // Number of temperature samples for burst mode
        // 0 - 1 sample
        // 1 - 2 samples
        // 2 - 4 samples
        // 3 - 8 samples
        // 4 - 16 samples
        // 5 - 32 samples
        // 6 - 64 samples
        // 7 - 128 samples
        bool setBurstSamples(uint8_t samples);

        // If in burst mode, returns true if burst complete is set
        bool isBurstAvailable();

        bool startBurst();

        // Shutdown mode
        // 0 - normal operation
        // 1 - shutdown mode
        // 2 - burst mode
        // datasheet page 32
        bool setShutdownMode(uint8_t mode);

        float getHotJunctionTemp();
        float getColdJunctionTemp();
        float getDeltaJunctionTemp();

        bool isInputRangeExceeded();

    private:
        uint8_t address;

        bool writeReg8(uint8_t reg, uint8_t value);
        bool writeReg16(uint8_t reg, uint16_t value);
        uint8_t readReg8(uint8_t reg);
        uint16_t readReg16(uint8_t reg);
};

#endif