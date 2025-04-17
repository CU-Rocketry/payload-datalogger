#include "MCP9600.h"
#include <Wire.h>

bool MCP9600::isInputRangeExceeded()
{
    uint8_t status = readReg8(MCP9600_STATUS);
    return (status & (1<<4)); // check bit 4
}

float MCP9600::getDeltaJunctionTemp()
{
    int16_t delta_junction = readReg16(MCP9600_JUNCTIONDELTA);
    float c = ((float)delta_junction * 0.0625f);
    return c;
}

float MCP9600::getColdJunctionTemp()
{
    int16_t cold_junction = readReg16(MCP9600_COLDJUNCTION);
    float c = ((float)cold_junction * 0.0625f);
    return c;
}

float MCP9600::getHotJunctionTemp()
{
    int16_t hot_junction = readReg16(MCP9600_HOTJUNCTION);
    float c = ((float)hot_junction * 0.0625f);

    // clear the data ready bit
    uint8_t status = readReg8(MCP9600_STATUS);
    status &= ~(1<<6); // clear bit 6
    writeReg8(MCP9600_STATUS, status); // write back

    return c;
}

bool MCP9600::setShutdownMode(uint8_t mode)
{
    uint8_t device_config = readReg8(MCP9600_DEVICECONFIG);
    device_config &= ~(0x3); // clear bits 1:0
    device_config |= (mode & 0x3); // set bits 1:0 to mode
    return writeReg8(MCP9600_DEVICECONFIG, device_config);
}

bool MCP9600::startBurst()
{
    uint8_t status = readReg8(MCP9600_STATUS);
    status &= ~(1<<7); // clear bit 7
    bool success = writeReg8(MCP9600_STATUS, status); // write back to the status register
    success |= setShutdownMode(2); // set the shutdown mode to burst
    return success;
}

bool MCP9600::isBurstAvailable()
{
  uint8_t status = readReg8(MCP9600_STATUS);
  return (status >> 7); // return bit 7
}

bool MCP9600::setBurstSamples(uint8_t samples){
    uint8_t device_config = readReg8(MCP9600_DEVICECONFIG);
    device_config &= ~(0x7<<2); // clear bits 4:2
    device_config |= (samples & 0x7) << 2; // set bits 4:2 to samples
    return writeReg8(MCP9600_DEVICECONFIG, device_config);
}

bool MCP9600::setFilterCoefficient(uint8_t coeff){
    uint8_t sensor_config = readReg8(MCP9600_SENSORCONFIG);
    sensor_config &= ~(0x7); // clear bits 2:0
    sensor_config |= (coeff & 0x7); // set bits 2:0 to coeff
    return writeReg8(MCP9600_SENSORCONFIG, sensor_config);
}

bool MCP9600::setThermocoupleType(uint8_t type){
    uint8_t sensor_config = readReg8(MCP9600_SENSORCONFIG);
    sensor_config &= ~(0x7 << 4); // clear bits 6:4
    sensor_config |= (type & 0x7) << 4; // set bits 6:4 to type
    return writeReg8(MCP9600_SENSORCONFIG, sensor_config);
}

bool MCP9600::setADCResolution(uint8_t resolution){
    uint8_t device_config = readReg8(MCP9600_DEVICECONFIG);
    device_config &= ~(0x3<<5); // clear bits 5 and 6
    device_config |= (resolution & 0x3) << 5; // set bits 5 and 6 to resolution
    return writeReg8(MCP9600_DEVICECONFIG, device_config);
}

bool MCP9600::setAmbientResolution(uint8_t resolution){
    uint8_t device_config = readReg8(MCP9600_DEVICECONFIG);
    device_config &= ~(1<<7); // clear bit 7
    device_config |= (resolution & 1) << 7; // set bit 7 to resolution
    return writeReg8(MCP9600_DEVICECONFIG, device_config);
}

bool MCP9600::resetConfiguration()
{
  bool ret;
  ret |= writeReg8(MCP9600_STATUS, 0x00);
  ret |= writeReg8(MCP9600_SENSORCONFIG, 0x00);
  ret |= writeReg8(MCP9600_DEVICECONFIG, 0x00);
  ret |= writeReg8(MCP9600_ALERT1_CONFIG, 0x00);
  ret |= writeReg8(MCP9600_ALERT2_CONFIG, 0x00);
  ret |= writeReg8(MCP9600_ALERT3_CONFIG, 0x00);
  ret |= writeReg8(MCP9600_ALERT4_CONFIG, 0x00);
  ret |= writeReg8(MCP9600_ALERT1_HYSTERESIS, 0x00);
  ret |= writeReg8(MCP9600_ALERT2_HYSTERESIS, 0x00);
  ret |= writeReg8(MCP9600_ALERT3_HYSTERESIS, 0x00);
  ret |= writeReg8(MCP9600_ALERT4_HYSTERESIS, 0x00);
  ret |= writeReg16(MCP9600_ALERT1_LIMIT, 0x0000);
  ret |= writeReg16(MCP9600_ALERT2_LIMIT, 0x0000);
  ret |= writeReg16(MCP9600_ALERT3_LIMIT, 0x0000);
  ret |= writeReg16(MCP9600_ALERT4_LIMIT, 0x0000);
  return ret;
}

bool MCP9600::isTemperatureReady() {
    return (readReg8(MCP9600_STATUS) & (1<<6));
}

bool MCP9600::checkDeviceID(){
    uint8_t id = readReg16(MCP9600_DEVICEID);
    return (id & 0xFF00)>>8 == 0x40;
}

bool MCP9600::writeReg8(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

bool MCP9600::writeReg16(uint8_t reg, uint16_t value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value >> 8);
    Wire.write(value & 0xFF);
    return (Wire.endTransmission() == 0);
}

uint8_t MCP9600::readReg8(uint8_t reg)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(address, (uint8_t)1);
    return Wire.read();
}

uint16_t MCP9600::readReg16(uint8_t reg)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(address, (uint8_t)2);
    uint16_t value = (Wire.read() << 8) | Wire.read();
    return value;
}