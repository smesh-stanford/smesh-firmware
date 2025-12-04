#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AS5600.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SMeshWindSensor.h"
#include "TelemetrySensor.h"
#include "gps/GeoCoord.h"
#include <string>

#ifdef ARCH_ESP32
#include "driver/pcnt.h"
#endif

// Static variable to track last time wind speed was calculated
static uint32_t lastWindSpeedMillis = 0;

SMeshWindSensor::SMeshWindSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SMESH_WIND_VANE, "SMesh Wind Sensor") {}

bool SMeshWindSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    // Initialize AS5600
    status = as5600.begin(dev->address.address, bus);
    if (!status) {
        LOG_ERROR("AS5600 begin() failed - sensor not responding");
        initI2CSensor();
        return status;
    }

    // Configure AS5600 (from AS5600_basic.ino)
    as5600.enableWatchdog(false);
    as5600.setPowerMode(AS5600_POWER_MODE_NOM);
    as5600.setHysteresis(AS5600_HYSTERESIS_OFF);
    as5600.setOutputStage(AS5600_OUTPUT_STAGE_ANALOG_FULL);
    as5600.setSlowFilter(AS5600_SLOW_FILTER_16X);
    as5600.setFastFilterThresh(AS5600_FAST_FILTER_THRESH_SLOW_ONLY);
    as5600.setZPosition(0);
    as5600.setMPosition(4095);
    as5600.setMaxAngle(4095);

    // Check magnet detection
    if (!as5600.isMagnetDetected()) {
        LOG_WARN("AS5600: No magnet detected - readings will be invalid");
    }

    // Check AGC gain overflows
    if (as5600.isAGCminGainOverflow()) {
        LOG_WARN("AS5600: Magnet too strong - move it further away");
    }
    if (as5600.isAGCmaxGainOverflow()) {
        LOG_WARN("AS5600: Magnet too weak - move closer or use stronger magnet");
    }

#ifdef ARCH_ESP32
#ifdef GPIO_WIND_COUNTER
    // CRITICAL FIX: Initialize PCNT for wind speed counter
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = GPIO_WIND_COUNTER,  // GPIO 33
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,      // Count rising edges
        .neg_mode = PCNT_COUNT_DIS,      // Ignore falling edges
        .counter_h_lim = 32767,
        .counter_l_lim = -32768,
        .unit = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0,
    };

    esp_err_t err = pcnt_unit_config(&pcnt_config);
    if (err != ESP_OK) {
        LOG_ERROR("PCNT unit config failed: %d", err);
        status = false;
        initI2CSensor();
        return status;
    }

    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);

    LOG_INFO("PCNT initialized on GPIO %d", GPIO_WIND_COUNTER);
#else
    LOG_WARN("GPIO_WIND_COUNTER not defined - wind speed disabled");
#endif
#endif

    LOG_DEBUG("SMesh Wind Sensor init succeeded");
    initI2CSensor();
    return status;
}

bool SMeshWindSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Get wind direction as scaled angle value from AS5600
    uint16_t windDirection = as5600.getAngle();
    measurement->variant.environment_metrics.has_wind_direction = true;
    measurement->variant.environment_metrics.wind_direction = windDirection;
    LOG_INFO("Wind Direction: %u (AS5600 scaled angle)", windDirection);

    // Calculate delta millis since last wind speed reading
    uint32_t currentMillis = millis();
    uint32_t deltaMillis = currentMillis - lastWindSpeedMillis;

    // Only update wind speed if at least 1000ms have passed
    if (deltaMillis < 1000) {
        LOG_DEBUG("Wind speed update skipped (delta: %u ms)", deltaMillis);
        return true;
    }

    // Update the last millis timestamp
    lastWindSpeedMillis = currentMillis;

#ifdef ARCH_ESP32
#ifdef GPIO_WIND_COUNTER
    // Read the PCNT counter value
    int16_t counterValue = 0;
    esp_err_t err = pcnt_get_counter_value(PCNT_UNIT_0, &counterValue);
    if (err != ESP_OK) {
        LOG_ERROR("PCNT get counter value failed: %d", err);
        measurement->variant.environment_metrics.has_wind_speed = true;
        measurement->variant.environment_metrics.wind_speed = 0.0f;
        return true;
    }

    // Log the raw counter value before clearing
    LOG_INFO("Raw counter value: %d", counterValue);

    // Clear the counter
    err = pcnt_counter_clear(PCNT_UNIT_0);
    if (err != ESP_OK) {
        LOG_ERROR("PCNT counter clear failed: %d", err);
    }

    // Calculate counts per second
    float countsPerSecond = ((float)counterValue / (float)deltaMillis) * 1000.0f;

    // Store wind speed in measurement
    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = countsPerSecond;

    // Log the calculated values
    LOG_INFO("Counts before clearing: %d, Counts/second: %.2f, Delta millis: %u",
             counterValue, countsPerSecond, deltaMillis);
#else
    // GPIO_WIND_COUNTER not defined
    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = 0.0f;
    LOG_DEBUG("Wind speed not available (GPIO_WIND_COUNTER not defined)");
#endif // GPIO_WIND_COUNTER
#else
    // Fallback for non-ESP32 platforms
    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = 0.0f;
    LOG_WARN("PCNT not available on this platform");
#endif // ARCH_ESP32

    return true;
}

#endif