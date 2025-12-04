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
    Adafruit_AS5600 as5600;

  public:
    SMeshWindSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
#endif