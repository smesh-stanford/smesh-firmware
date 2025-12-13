#pragma once

#ifndef _MT_SMESHWINDSENSOR_H
#define _MT_SMESHWINDSENSOR_H
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AS5600.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_AS5600.h>
#include <string>

class SMeshWindSensor : public TelemetrySensor
{
  private:
    // Wind direction averaging configuration
    static constexpr uint8_t WIND_DIRECTION_BUFFER_SIZE = 20;
    static constexpr uint8_t MIN_WIND_COUNTS = 10;
    static constexpr uint32_t MIN_WIND_SPEED_AVERAGE_INTERVAL = 5;  // seconds

    // Circular buffer for wind samples
    struct WindSample {
        uint16_t direction;      // Wind direction in degrees (0-360)
        int16_t countDelta;      // PCNT counts since last sample
    };

    Adafruit_AS5600 as5600;
    WindSample windBuffer[WIND_DIRECTION_BUFFER_SIZE];
    uint8_t bufferIndex;         // Current write position in circular buffer
    uint8_t bufferCount;         // Number of valid samples in buffer
    int16_t lastCounterValue;    // Last PCNT reading for delta calculation
    uint32_t lastSampleMillis;   // Timestamp of last sample
    uint32_t samplePeriodMs;     // Calculated sampling period

  public:
    SMeshWindSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
#endif