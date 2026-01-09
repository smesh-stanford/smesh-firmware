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
    // number of samples we can store for wind direction averaging
    static constexpr uint8_t WIND_DIRECTION_BUFFER_SIZE = 20;

    // minimum counts change to consider a valid wind sample
    //
    // Note: at very low wind speeds, the counter may increment very slowly,
    // leading to noisy direction readings. By setting a minimum counts threshold,
    // we only record direction samples when there is sufficient wind activity.
    static constexpr uint8_t MIN_WIND_COUNTS = 10;

    // AS5600 sensor conversion constants
    // AS5600 provides a 12-bit angle value in the range [0, 4095].
    // Use these to convert raw sensor units into degrees [0, 360).
    static constexpr uint16_t AS5600_MAX_ANGLE = 4095;
    static constexpr double AS5600_TO_DEG = 360.0 / static_cast<double>(AS5600_MAX_ANGLE);

    // minimum interval over which to average wind speed
    static constexpr uint32_t MIN_WIND_SPEED_AVERAGE_INTERVAL = 5;  // seconds

    // Circular buffer for wind samples
    struct WindSample {
        uint16_t direction;      // Wind direction in degrees (0-360)
        int16_t countDelta;      // PCNT counts since last sample
    };

    Adafruit_AS5600 as5600;
    WindSample windBuffer[WIND_DIRECTION_BUFFER_SIZE];
    uint8_t bufferIndex;         // Current write position in circular buffer
    uint8_t bufferCount;         // Number of valid wind samples in buffer
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