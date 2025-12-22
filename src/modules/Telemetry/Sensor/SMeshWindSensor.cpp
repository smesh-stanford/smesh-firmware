#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AS5600.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SMeshWindSensor.h"
#include "TelemetrySensor.h"
#include "gps/GeoCoord.h"
#include <cmath>
#include <string>

// For sending diagnostic text messages
#include "MeshService.h"
#include "Router.h"
#include <cstring>

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

        // Broadcast a short diagnostic text message to the mesh so other nodes/apps can see the error
        // {
        //     meshtastic_MeshPacket *p = router->allocForSending();
        //     p->to = NODENUM_BROADCAST;
        //     p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        //     const char msg[] = "SMESH: wind sensor AS5600 init failed";
        //     size_t mlen = strlen(msg);
        //     if (mlen > meshtastic_Constants_DATA_PAYLOAD_LEN)
        //         mlen = meshtastic_Constants_DATA_PAYLOAD_LEN;
        //     p->decoded.payload.size = (uint16_t)mlen;
        //     memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
        //     service->sendToMesh(p, RX_SRC_LOCAL, true);
        // }

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
    // Initialize PCNT for wind speed counter
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = GPIO_WIND_COUNTER,  // Conneect to anemometer hall effect output
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,         // Count rising edges
        .neg_mode = PCNT_COUNT_DIS,         // Ignore falling edges
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

    // Initialize wind direction averaging buffer
    bufferIndex = 0;
    bufferCount = 0;
    lastCounterValue = 0;
    lastSampleMillis = 0;

    // Calculate sampling period based on telemetry interval
    uint32_t telemetryIntervalSec = moduleConfig.telemetry.environment_update_interval;
    if (telemetryIntervalSec == 0) {
        telemetryIntervalSec = 180;  // Use default from smesh_defaults.h
    }

    // Calculate period: interval / buffer_size, clamped to constraints
    samplePeriodMs = (telemetryIntervalSec * 1000) / WIND_DIRECTION_BUFFER_SIZE;
    if (samplePeriodMs < MIN_WIND_SPEED_AVERAGE_INTERVAL * 1000) {
        samplePeriodMs = MIN_WIND_SPEED_AVERAGE_INTERVAL * 1000;
    }
    if (samplePeriodMs >= telemetryIntervalSec * 1000) {
        samplePeriodMs = (telemetryIntervalSec * 1000) - 1000;  // Leave 1 second margin
    }

    LOG_INFO("Wind direction averaging: %u samples every %u ms (telemetry interval: %u s)",
             WIND_DIRECTION_BUFFER_SIZE, samplePeriodMs, telemetryIntervalSec);

    LOG_DEBUG("SMesh Wind Sensor init succeeded");
    initI2CSensor();
    return status;
}

int32_t SMeshWindSensor::runOnce()
{
    if (!status) {
        return INT32_MAX;  // Sensor not initialized
    }

    uint32_t currentMillis = millis();

    // Check if it's time to take a sample
    if (lastSampleMillis == 0 || (currentMillis - lastSampleMillis) >= samplePeriodMs) {

#ifdef ARCH_ESP32
#ifdef GPIO_WIND_COUNTER
        // Read current PCNT counter value (don't clear it)
        int16_t currentCounterValue = 0;
        esp_err_t err = pcnt_get_counter_value(PCNT_UNIT_0, &currentCounterValue);
        if (err != ESP_OK) {
            LOG_ERROR("PCNT get counter value failed in runOnce: %d", err);
            return samplePeriodMs;
        }

        // Calculate delta since last sample
        int16_t countDelta = currentCounterValue - lastCounterValue;

        // Fake wind speed for testing
           countDelta += random(0, 50);

        // Only sample direction if sufficient wind activity
        if (abs(countDelta) > MIN_WIND_COUNTS) {
            // Read AS5600 direction
            uint16_t rawDirection = as5600.getAngle();

            // Convert 12-bit AS5600 angle (0-4095) to degrees [0,360)
            double deg = rawDirection * SMeshWindSensor::AS5600_TO_DEG;
            uint16_t scaledDirection = static_cast<uint16_t>(lround(deg) % 360);

            // Store sample in circular buffer
            windBuffer[bufferIndex].direction = scaledDirection;
            windBuffer[bufferIndex].countDelta = countDelta;

            LOG_INFO("Wind sample [%u/%u]: dir=%u°, counts=%d",
                     bufferIndex + 1, WIND_DIRECTION_BUFFER_SIZE,
                     scaledDirection, countDelta);

            // Advance buffer position (circular)
            bufferIndex = (bufferIndex + 1) % WIND_DIRECTION_BUFFER_SIZE;
            if (bufferCount < WIND_DIRECTION_BUFFER_SIZE) {
                bufferCount++;
            }
        } else {
            LOG_DEBUG("Wind sample skipped: pulse count delta=%d < threshold=%d",
                      countDelta, MIN_WIND_COUNTS);
        }

        // Update for next delta calculation
        lastCounterValue = currentCounterValue;
#endif
#endif

        lastSampleMillis = currentMillis;
    }

    return samplePeriodMs;  // Request to be called again in samplePeriodMs milliseconds
}

bool SMeshWindSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Calculate averaged wind direction from buffer using weighted vector averaging
    if (bufferCount > 0) {
        double sumX = 0.0;
        double sumY = 0.0;

        for (uint8_t i = 0; i < bufferCount; i++) {
            // Convert direction to radians
            double angleRad = windBuffer[i].direction * M_PI / 180.0;

            // Weight by wind speed (count delta as proxy)
            double weight = abs(windBuffer[i].countDelta);

            // Accumulate vector components
            sumX += weight * cos(angleRad);
            sumY += weight * sin(angleRad);
        }

        // Calculate average direction from vector sum
        double avgAngleRad = atan2(sumY, sumX);

        // Convert back to degrees [0, 360)
        double avgAngleDeg = avgAngleRad * 180.0 / M_PI;
        if (avgAngleDeg < 0) {
            avgAngleDeg += 360.0;
        }

        measurement->variant.environment_metrics.has_wind_direction = true;
        measurement->variant.environment_metrics.wind_direction = (uint16_t)avgAngleDeg;

        LOG_INFO("Wind Direction (averaged over %u samples): %u°",
                 bufferCount, (uint16_t)avgAngleDeg);
    } else {
        // No samples collected - report 0 and warn
        measurement->variant.environment_metrics.has_wind_direction = true;
        measurement->variant.environment_metrics.wind_direction = 0;
        LOG_INFO("No wind detected during this interval (buffer empty)");
    }

    // Clear buffer for next averaging period
    bufferIndex = 0;
    bufferCount = 0;

    // Calculate wind speed (existing logic with minor modifications)
    uint32_t currentMillis = millis();
    uint32_t deltaMillis = currentMillis - lastWindSpeedMillis;

    if (deltaMillis < 1000) {
        LOG_DEBUG("Wind speed update skipped (delta: %u ms)", deltaMillis);
        return true;
    }

    lastWindSpeedMillis = currentMillis;

#ifdef ARCH_ESP32
#ifdef GPIO_WIND_COUNTER
    // Read and clear the PCNT counter
    int16_t counterValue = 0;
    esp_err_t err = pcnt_get_counter_value(PCNT_UNIT_0, &counterValue);
    if (err != ESP_OK) {
        LOG_ERROR("PCNT get counter value failed: %d", err);
        measurement->variant.environment_metrics.has_wind_speed = true;
        measurement->variant.environment_metrics.wind_speed = 0.0f;
        return true;
    }

    LOG_INFO("Raw counter value: %d", counterValue);

    // Clear the counter for next period
    err = pcnt_counter_clear(PCNT_UNIT_0);
    if (err != ESP_OK) {
        LOG_ERROR("PCNT counter clear failed: %d", err);
    }

    // Reset lastCounterValue since we cleared the counter
    lastCounterValue = 0;

    // Calculate counts per second
    float countsPerSecond = ((float)counterValue / (float)deltaMillis) * 1000.0f;

    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = countsPerSecond;

    LOG_INFO("Counts: %d, Counts/second: %.2f, Delta millis: %u",
             counterValue, countsPerSecond, deltaMillis);
#else
    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = 0.0f;
    LOG_DEBUG("Wind speed not available (GPIO_WIND_COUNTER not defined)");
#endif
#else
    measurement->variant.environment_metrics.has_wind_speed = true;
    measurement->variant.environment_metrics.wind_speed = 0.0f;
    LOG_WARN("PCNT not available on this platform");
#endif

    return true;
}

#endif