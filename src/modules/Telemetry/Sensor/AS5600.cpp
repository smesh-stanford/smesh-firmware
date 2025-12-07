#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AS5600.h"
#include "TelemetrySensor.h"
#include <math.h>

AS5600Sensor::AS5600Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_AS5600, "AS5600") {}

int32_t AS5600Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = 1; // Assume success, will be set to 0 if connection fails
    begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);

    return initI2CSensor();
}

void AS5600Sensor::setup() 
{
    // Test connection by trying to read status register
    if (!isConnected()) {
        LOG_ERROR("AS5600: Could not find a valid AS5600 sensor, check wiring!");
        status = 0;
    } else {
        LOG_INFO("AS5600: Sensor initialized successfully");
    }
}

void AS5600Sensor::begin(TwoWire *wire, uint8_t addr)
{
    _wire = wire;
    _addr = addr;
    _wire->begin();
}

bool AS5600Sensor::isConnected()
{
    _wire->beginTransmission(_addr);
    _wire->write(AS5600_STATUS);
    uint8_t result = _wire->endTransmission();
    return (result == 0);
}

uint16_t AS5600Sensor::readRawAngle()
{
    // Read high byte
    _wire->beginTransmission(_addr);
    _wire->write(AS5600_RAW_ANGLE_H);
    _wire->endTransmission();
    _wire->requestFrom(_addr, (uint8_t)1);
    uint16_t high = _wire->read();

    // Read low byte  
    _wire->beginTransmission(_addr);
    _wire->write(AS5600_RAW_ANGLE_L);
    _wire->endTransmission();
    _wire->requestFrom(_addr, (uint8_t)1);
    uint16_t low = _wire->read();

    // Combine high and low bytes
    return (high << 8) | low;
}

float AS5600Sensor::readWindDirection()
{
    uint16_t rawAngle = readRawAngle();
    
    // Convert raw angle to degrees (AS5600 gives 0-4095 for 0-360°)
    float angle = rawAngle * 0.0879; // 360.0 / 4096.0 ≈ 0.0879
    
    // Convert to radians for vector calculation (matching Python code)
    float r = (angle * 71.0) / 4068.0;
    
    // Calculate North-South and East-West vectors
    float NS_vector = cos(r);
    float EW_vector = sin(r);
    
    // Calculate angle from vectors
    angle = (atan2(EW_vector, NS_vector) * 4068.0) / 71.0;
    
    // Adjust if necessary (ensure positive angle)
    if (angle < 0) {
        angle = 360.0 + angle;
    }
    
    return angle;
}

bool AS5600Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!status) {
        return false;
    }

    float windDirection = readWindDirection();
    
    // Store wind direction in the measurement (convert to uint16_t)
    measurement->variant.environment_metrics.has_wind_direction = true;
    measurement->variant.environment_metrics.wind_direction = (uint16_t)round(windDirection);

    LOG_DEBUG("AS5600 Wind Direction: %.2f degrees", windDirection);

    return true;
}

#endif