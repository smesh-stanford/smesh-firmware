/*
This is custom code for the AS5600 magnetic rotary position sensor.
It is used to read wind direction using magnetic encoding
Based on Python implementation by Joseph Rener, UCAR
*/

#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class AS5600Sensor : public TelemetrySensor
{
  private:
    TwoWire *_wire;
    uint8_t _addr;
    
    // AS5600 register definitions
    static const uint8_t AS5600_RAW_ANGLE_H = 0x0C;
    static const uint8_t AS5600_RAW_ANGLE_L = 0x0D;
    static const uint8_t AS5600_STATUS = 0x0B;
    
    // Helper methods
    float readWindDirection();
    uint16_t readRawAngle();
    bool isConnected();
    void begin(TwoWire *wire, uint8_t addr);

  protected:
    virtual void setup() override;

  public:
    AS5600Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif