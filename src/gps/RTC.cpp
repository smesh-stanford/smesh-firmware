#include "RTC.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "main.h"
#include <Throttle.h>
#include <sys/time.h>
#include <time.h>

#if defined(DS3231_RTC)
#include <Wire.h>

static TwoWire &ds3231Bus()
{
#if WIRE_INTERFACES_COUNT == 2
    // Use the I2C bus where the scanner found the RTC.
    return rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire;
#else
    return Wire;
#endif
}

// DS3231 stores time/date digits in packed BCD (e.g., 0x45 == decimal 45).
// Convert from packed BCD register value to a normal integer.
static uint8_t ds3231Bcd2bin(uint8_t packedBcdValue)
{
    uint8_t tens = (uint8_t)((packedBcdValue >> 4) & 0x0FU);
    uint8_t ones = (uint8_t)(packedBcdValue & 0x0FU);
    return (uint8_t)((tens * 10U) + ones);
}

// Convert from normal integer back to packed BCD for DS3231 writes.
static uint8_t ds3231Bin2bcd(uint8_t decimalValue)
{
    uint8_t tens = (uint8_t)(decimalValue / 10U);
    uint8_t ones = (uint8_t)(decimalValue % 10U);
    return (uint8_t)((tens << 4) | ones);
}

static bool ds3231ReadTime(struct tm *calendarTime)
{
    // DS3231 calendar registers 0x00..0x06 are stored in this order:
    // [0] = seconds (0-59, CH bit in bit7)
    // [1] = minutes (0-59)
    // [2] = hours (12h/24h format with mode bits)
    // [3] = day of week (1-7)
    // [4] = day of month (1-31)
    // [5] = month (1-12, century bit in bit7)
    // [6] = year (0-99)
    uint8_t rtcRegisters[7];
    TwoWire &i2cBus = ds3231Bus();
    i2cBus.beginTransmission((uint8_t)DS3231_RTC);
    i2cBus.write((uint8_t)0); // Start reading at register 0x00 (seconds).

    // Repeated-start so we can issue read immediately after setting address pointer.
    if (i2cBus.endTransmission(false) != 0)
        return false;

    size_t bytesRead = i2cBus.requestFrom((int)(uint8_t)DS3231_RTC, 7);
    if (bytesRead != 7)
        return false;

    for (int registerIndex = 0; registerIndex < 7; registerIndex++) {
        if (!i2cBus.available())
            return false;
        rtcRegisters[registerIndex] = (uint8_t)i2cBus.read();
    }

    // Seconds and minutes are BCD; mask out status bits before conversion.
    calendarTime->tm_sec = ds3231Bcd2bin((uint8_t)(rtcRegisters[0] & 0x7FU));
    calendarTime->tm_min = ds3231Bcd2bin((uint8_t)(rtcRegisters[1] & 0x7FU));

    // Hours can be encoded in either 24-hour mode or 12-hour mode.
    uint8_t hoursRegister = rtcRegisters[2];
    bool is12HourMode = (hoursRegister & 0x40U) != 0;
    if (is12HourMode) {
        uint8_t hourIn12HourClock = ds3231Bcd2bin((uint8_t)(hoursRegister & 0x1FU));
        bool isPm = (hoursRegister & 0x20U) != 0;

        // Convert 12-hour encoding to 24-hour tm_hour convention.
        if (isPm && hourIn12HourClock < 12)
            hourIn12HourClock = (uint8_t)(hourIn12HourClock + 12U);
        else if (!isPm && hourIn12HourClock == 12)
            hourIn12HourClock = 0;

        calendarTime->tm_hour = hourIn12HourClock;
    } else {
        calendarTime->tm_hour = ds3231Bcd2bin((uint8_t)(hoursRegister & 0x3FU));
    }

    // DS3231 day-of-week is 1..7; struct tm uses 0..6.
    int dayOfWeekOneBased = ds3231Bcd2bin((uint8_t)(rtcRegisters[3] & 0x3FU));
    if (dayOfWeekOneBased >= 1 && dayOfWeekOneBased <= 7)
        calendarTime->tm_wday = dayOfWeekOneBased - 1;

    calendarTime->tm_mday = ds3231Bcd2bin((uint8_t)(rtcRegisters[4] & 0x3FU));

    uint8_t monthRegister = rtcRegisters[5];
    calendarTime->tm_mon = ds3231Bcd2bin((uint8_t)(monthRegister & 0x1FU)) - 1; // tm_mon is 0-based.

    int fullYear = 2000 + ds3231Bcd2bin(rtcRegisters[6]);
    if (monthRegister & 0x80U)
        fullYear += 100; // Century bit: 21xx when set.
    calendarTime->tm_year = fullYear - 1900; // tm_year stores years since 1900.

    return true;
}

static bool ds3231WriteTime(const struct tm *calendarTime)
{
    uint8_t rtcRegisters[7];

    rtcRegisters[0] = ds3231Bin2bcd((uint8_t)calendarTime->tm_sec);
    rtcRegisters[1] = ds3231Bin2bcd((uint8_t)calendarTime->tm_min);
    rtcRegisters[2] = ds3231Bin2bcd((uint8_t)calendarTime->tm_hour); // write as 24-hour format

    int weekdayZeroBased = calendarTime->tm_wday;
    if (weekdayZeroBased < 0)
        weekdayZeroBased = 0;
    rtcRegisters[3] = ds3231Bin2bcd((uint8_t)(weekdayZeroBased + 1)); // DS3231 expects 1..7.

    rtcRegisters[4] = ds3231Bin2bcd((uint8_t)calendarTime->tm_mday);

    int fullYear = 1900 + calendarTime->tm_year;
    uint8_t monthOneBased = (uint8_t)(calendarTime->tm_mon + 1);
    uint8_t yearTwoDigits = (uint8_t)(fullYear % 100);

    rtcRegisters[6] = ds3231Bin2bcd(yearTwoDigits);

    uint8_t monthRegister = ds3231Bin2bcd(monthOneBased);
    if (fullYear >= 2100)
        monthRegister |= 0x80U; // Set century bit for 21xx.
    rtcRegisters[5] = monthRegister;

    TwoWire &i2cBus = ds3231Bus();
    i2cBus.beginTransmission((uint8_t)DS3231_RTC);
    i2cBus.write((uint8_t)0); // Start writing at register 0x00.
    for (int registerIndex = 0; registerIndex < 7; registerIndex++)
        i2cBus.write(rtcRegisters[registerIndex]);

    return i2cBus.endTransmission() == 0;
}
#endif // DS3231_RTC

static RTCQuality currentQuality = RTCQualityNone;
uint32_t lastSetFromPhoneNtpOrGps = 0;

static uint32_t lastTimeValidationWarning = 0;
static const uint32_t TIME_VALIDATION_WARNING_INTERVAL_MS = 15000; // 15 seconds

RTCQuality getRTCQuality()
{
    return currentQuality;
}

// stuff that really should be in in the instance instead...
static uint32_t
    timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

/**
 * Reads the current date and time from the RTC module and updates the system time.
 * @return True if the RTC was successfully read and the system time was updated, false otherwise.
 */
RTCSetResult readFromRTC()
{
    struct timeval tv; /* btw settimeofday() is helpful here too*/
#ifdef RV3028_RTC
    if (rtc_found.address == RV3028_RTC) {
        uint32_t now = millis();
        Melopero_RV3028 rtc;
#if WIRE_INTERFACES_COUNT == 2
        rtc.initI2C(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
        rtc.initI2C();
#endif
        tm t;
        t.tm_year = rtc.getYear() - 1900;
        t.tm_mon = rtc.getMonth() - 1;
        t.tm_mday = rtc.getDate();
        t.tm_hour = rtc.getHour();
        t.tm_min = rtc.getMinute();
        t.tm_sec = rtc.getSecond();
        tv.tv_sec = gm_mktime(&t);
        tv.tv_usec = 0;
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms

#ifdef BUILD_EPOCH
        if (tv.tv_sec < BUILD_EPOCH) {
            if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                LOG_WARN("RTC time (%ld) before build epoch (%ld); using for local clock until mesh/GPS/NTP sync",
                         (long)printableEpoch, (long)BUILD_EPOCH);
                lastTimeValidationWarning = millis();
            }
        }
#endif

        LOG_DEBUG("Read RTC time from RV3028 getTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
        if (currentQuality == RTCQualityNone) {
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
            currentQuality = RTCQualityDevice;
        }
        return RTCSetResultSuccess;
    }
#elif defined(PCF8563_RTC)
    if (rtc_found.address == PCF8563_RTC) {
        uint32_t now = millis();
        PCF8563_Class rtc;

#if WIRE_INTERFACES_COUNT == 2
        rtc.begin(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
        rtc.begin();
#endif

        auto tc = rtc.getDateTime();
        tm t;
        t.tm_year = tc.year - 1900;
        t.tm_mon = tc.month - 1;
        t.tm_mday = tc.day;
        t.tm_hour = tc.hour;
        t.tm_min = tc.minute;
        t.tm_sec = tc.second;
        tv.tv_sec = gm_mktime(&t);
        tv.tv_usec = 0;
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms

#ifdef BUILD_EPOCH
        if (tv.tv_sec < BUILD_EPOCH) {
            if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                LOG_WARN("RTC time (%ld) before build epoch (%ld); using for local clock until mesh/GPS/NTP sync",
                         (long)printableEpoch, (long)BUILD_EPOCH);
                lastTimeValidationWarning = millis();
            }
        }
#endif

        LOG_DEBUG("Read RTC time from PCF8563 getDateTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
        if (currentQuality == RTCQualityNone) {
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
            currentQuality = RTCQualityDevice;
        }
        return RTCSetResultSuccess;
    }
#elif defined(DS3231_RTC)
    if (rtc_found.address == DS3231_RTC) {
        uint32_t now = millis();
        struct tm t;
        // Read DS3231 calendar fields into struct tm, then convert to Unix epoch
        // so we can update system time via settimeofday(tv).
        if (!ds3231ReadTime(&t))
            return RTCSetResultNotSet;

        // convert to unix seconds 
        tv.tv_sec = gm_mktime(&t);
        tv.tv_usec = 0;
        uint32_t printableEpoch = tv.tv_sec;

#ifdef BUILD_EPOCH
        if (tv.tv_sec < BUILD_EPOCH) {
            if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                LOG_WARN("RTC time (%ld) before build epoch (%ld); using for local clock until mesh/GPS/NTP sync",
                         (long)printableEpoch, (long)BUILD_EPOCH);
                lastTimeValidationWarning = millis();
            }
        }
#endif

        LOG_DEBUG("Read RTC time from DS3231 as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (long)printableEpoch);
        if (currentQuality == RTCQualityNone) {
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
            currentQuality = RTCQualityDevice;
        }
        return RTCSetResultSuccess;
    }
#elif defined(RX8130CE_RTC)
    if (rtc_found.address == RX8130CE_RTC) {
        uint32_t now = millis();
#ifdef MUZI_BASE
        ArtronShop_RX8130CE rtc(&Wire1);
#else
        ArtronShop_RX8130CE rtc(&Wire);
#endif
        tm t;
        if (rtc.getTime(&t)) {
            tv.tv_sec = gm_mktime(&t);
            tv.tv_usec = 0;

            uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
            LOG_DEBUG("Read RTC time from RX8130CE getDateTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900,
                      t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
#ifdef BUILD_EPOCH
            if (tv.tv_sec < BUILD_EPOCH) {
                if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                    LOG_WARN("RTC time (%ld) before build epoch (%ld); using for local clock until mesh/GPS/NTP sync",
                             (long)printableEpoch, (long)BUILD_EPOCH);
                    lastTimeValidationWarning = millis();
                }
            }
#endif
            if (currentQuality == RTCQualityNone) {
                timeStartMsec = now;
                zeroOffsetSecs = tv.tv_sec;
                currentQuality = RTCQualityDevice;
            }
            return RTCSetResultSuccess;
        }
    }
#else
    if (!gettimeofday(&tv, NULL)) {
        uint32_t now = millis();
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
        LOG_DEBUG("Read RTC time as %ld", printableEpoch);
        timeStartMsec = now;
        zeroOffsetSecs = tv.tv_sec;
        return RTCSetResultSuccess;
    }
#endif
    return RTCSetResultNotSet;
}

/**
 * Sets the RTC (Real-Time Clock) if the provided time is of higher quality than the current RTC time.
 *
 * @param q The quality of the provided time.
 * @param tv A pointer to a timeval struct containing the time to potentially set the RTC to.
 * @return RTCSetResult
 *
 * If we haven't yet set our RTC this boot, set it from a GPS derived time
 */
RTCSetResult perhapsSetRTC(RTCQuality q, const struct timeval *tv, bool forceUpdate)
{
    static uint32_t lastSetMsec = 0;
    uint32_t now = millis();
    uint32_t printableEpoch = tv->tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
#ifdef BUILD_EPOCH
    if (tv->tv_sec < BUILD_EPOCH) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            LOG_WARN("Ignore time (%ld) before build epoch (%ld)!", printableEpoch, BUILD_EPOCH);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    } else if ((uint64_t)tv->tv_sec > ((uint64_t)BUILD_EPOCH + FORTY_YEARS)) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            // Calculate max allowed time safely to avoid overflow in logging
            uint64_t maxAllowedTime = (uint64_t)BUILD_EPOCH + FORTY_YEARS;
            uint32_t maxAllowedPrintable = (maxAllowedTime > UINT32_MAX) ? UINT32_MAX : (uint32_t)maxAllowedTime;
            LOG_WARN("Ignore time (%ld) too far in the future (build epoch: %ld, max allowed: %ld)!", printableEpoch,
                     (uint32_t)BUILD_EPOCH, maxAllowedPrintable);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    }
#endif

    bool shouldSet;
    if (forceUpdate) {
        shouldSet = true;
        LOG_DEBUG("Override current RTC quality (%s) with incoming time of RTC quality of %s", RtcName(currentQuality),
                  RtcName(q));
    } else if (q > currentQuality) {
        shouldSet = true;
        LOG_DEBUG("Upgrade time to quality %s", RtcName(q));
    } else if (q == RTCQualityGPS) {
        shouldSet = true;
        LOG_DEBUG("Reapply GPS time: %ld secs", printableEpoch);
    } else if (q == RTCQualityNTP && !Throttle::isWithinTimespanMs(lastSetMsec, (12 * 60 * 60 * 1000UL))) {
        // Every 12 hrs we will slam in a new NTP or Phone GPS / NTP time, to correct for local RTC clock drift
        shouldSet = true;
        LOG_DEBUG("Reapply external time to correct clock drift %ld secs", printableEpoch);
    } else {
        shouldSet = false;
        LOG_DEBUG("Current RTC quality: %s. Ignore time of RTC quality of %s", RtcName(currentQuality), RtcName(q));
    }

    if (shouldSet) {
        currentQuality = q;
        lastSetMsec = now;
        if (currentQuality >= RTCQualityNTP) {
            lastSetFromPhoneNtpOrGps = now;
        }

        // This delta value works on all platforms
        timeStartMsec = now;
        zeroOffsetSecs = tv->tv_sec;
        // If this platform has a setable RTC, set it
#ifdef RV3028_RTC
        if (rtc_found.address == RV3028_RTC) {
            Melopero_RV3028 rtc;
#if WIRE_INTERFACES_COUNT == 2
            rtc.initI2C(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
            rtc.initI2C();
#endif
            tm *t = gmtime(&tv->tv_sec);
            rtc.setTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_wday, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            LOG_DEBUG("RV3028_RTC setTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                      t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
        }
#elif defined(PCF8563_RTC)
        if (rtc_found.address == PCF8563_RTC) {
            PCF8563_Class rtc;

#if WIRE_INTERFACES_COUNT == 2
            rtc.begin(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
            rtc.begin();
#endif
            tm *t = gmtime(&tv->tv_sec);
            rtc.setDateTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            LOG_DEBUG("PCF8563_RTC setDateTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                      t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
        }
#elif defined(DS3231_RTC)
        if (rtc_found.address == DS3231_RTC) {
            tm *t = gmtime(&tv->tv_sec);
            if (ds3231WriteTime(t))
                LOG_DEBUG("DS3231_RTC setTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                          t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
            else
                LOG_WARN("DS3231 I2C write failed");
        }
#elif defined(RX8130CE_RTC)
        if (rtc_found.address == RX8130CE_RTC) {
#ifdef MUZI_BASE
            ArtronShop_RX8130CE rtc(&Wire1);
#else
            ArtronShop_RX8130CE rtc(&Wire);
#endif
            tm *t = gmtime(&tv->tv_sec);
            if (rtc.setTime(*t)) {
                LOG_DEBUG("RX8130CE setDateTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
            } else {
                LOG_WARN("Failed to set time for RX8130CE");
            }
        }
#elif defined(ARCH_ESP32)
        settimeofday(tv, NULL);
#endif

        // nrf52 doesn't have a readable RTC (yet - software not written)
#if HAS_RTC
        readFromRTC();
#endif

        return RTCSetResultSuccess;
    } else {
        return RTCSetResultNotSet; // RTC was already set with a higher quality time
    }
}

const char *RtcName(RTCQuality quality)
{
    switch (quality) {
    case RTCQualityNone:
        return "None";
    case RTCQualityDevice:
        return "Device";
    case RTCQualityFromNet:
        return "Net";
    case RTCQualityNTP:
        return "NTP";
    case RTCQualityGPS:
        return "GPS";
    default:
        return "Unknown";
    }
}

/**
 * Sets the RTC time if the provided time is of higher quality than the current RTC time.
 *
 * @param q The quality of the provided time.
 * @param t The time to potentially set the RTC to.
 * @return True if the RTC was set to the provided time, false otherwise.
 */
RTCSetResult perhapsSetRTC(RTCQuality q, struct tm &t)
{
    /* Convert to unix time
    The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
    (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
    */
    // horrible hack to make mktime TZ agnostic - best practise according to
    // https://www.gnu.org/software/libc/manual/html_node/Broken_002ddown-Time.html
    time_t res = gm_mktime(&t);
    struct timeval tv;
    tv.tv_sec = res;
    tv.tv_usec = 0;                      // time.centisecond() * (10 / 1000);
    uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
#ifdef BUILD_EPOCH
    if (tv.tv_sec < BUILD_EPOCH) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            LOG_WARN("Ignore time (%lu) before build epoch (%lu)!", printableEpoch, BUILD_EPOCH);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    } else if ((uint64_t)tv.tv_sec > ((uint64_t)BUILD_EPOCH + FORTY_YEARS)) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            // Calculate max allowed time safely to avoid overflow in logging
            uint64_t maxAllowedTime = (uint64_t)BUILD_EPOCH + FORTY_YEARS;
            uint32_t maxAllowedPrintable = (maxAllowedTime > UINT32_MAX) ? UINT32_MAX : (uint32_t)maxAllowedTime;
            LOG_WARN("Ignore time (%lu) too far in the future (build epoch: %lu, max allowed: %lu)!", printableEpoch,
                     (uint32_t)BUILD_EPOCH, maxAllowedPrintable);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    }
#endif

    // LOG_DEBUG("Got time from GPS month=%d, year=%d, unixtime=%ld", t.tm_mon, t.tm_year, tv.tv_sec);
    if (t.tm_year < 0 || t.tm_year >= 300) {
        // LOG_DEBUG("Ignore invalid GPS month=%d, year=%d, unixtime=%ld", t.tm_mon, t.tm_year, tv.tv_sec);
        return RTCSetResultInvalidTime;
    } else {
        return perhapsSetRTC(q, &tv);
    }
}

/**
 * Returns the timezone offset in seconds.
 *
 * @return The timezone offset in seconds.
 */
int32_t getTZOffset()
{
#if MESHTASTIC_EXCLUDE_TZ
    return 0;
#else
    time_t now = getTime(false);
    struct tm *gmt;
    gmt = gmtime(&now);
    gmt->tm_isdst = -1;
    return (int32_t)difftime(now, mktime(gmt));
#endif
}

/**
 * Returns the current time in seconds since the Unix epoch (January 1, 1970).
 *
 * @return The current time in seconds since the Unix epoch.
 */
uint32_t getTime(bool local)
{
    if (local) {
        return (((uint32_t)millis() - timeStartMsec) / 1000) + zeroOffsetSecs + getTZOffset();
    } else {
        return (((uint32_t)millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
    }
}

/**
 * Returns the current time from the RTC if the quality of the time is at least minQuality.
 *
 * @param minQuality The minimum quality of the RTC time required for it to be considered valid.
 * @return The current time from the RTC if it meets the minimum quality requirement, or 0 if the time is not valid.
 */
uint32_t getValidTime(RTCQuality minQuality, bool local)
{
    return (currentQuality >= minQuality) ? getTime(local) : 0;
}

time_t gm_mktime(struct tm *tm)
{
#if !MESHTASTIC_EXCLUDE_TZ
    time_t result = 0;

    // First, get us to the start of tm->year, by calcuating the number of days since the Unix epoch.
    int year = 1900 + tm->tm_year; // tm_year is years since 1900
    int year_minus_one = year - 1;
    int days_before_this_year = 0;
    days_before_this_year += year_minus_one * 365;
    // leap days: every 4 years, except 100s, but including 400s.
    days_before_this_year += year_minus_one / 4 - year_minus_one / 100 + year_minus_one / 400;
    // subtract from 1970-01-01 to get days since epoch
    days_before_this_year -= 719162; // (1969 * 365 + 1969 / 4 - 1969 / 100 + 1969 / 400);

    // Now, within this tm->year, compute the days *before* this tm->month starts.
    int days_before_month[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334}; // non-leap year
    int days_this_year_before_this_month = days_before_month[tm->tm_mon];                // tm->tm_mon is 0..11

    // If this is a leap year, and we're past February, add a day:
    if (tm->tm_mon >= 2 && (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0)) {
        days_this_year_before_this_month += 1;
    }

    // And within this month:
    int days_this_month_before_today = tm->tm_mday - 1; // tm->tm_mday is 1..31

    // Now combine them all together, and convert days to seconds:
    result += (days_before_this_year + days_this_year_before_this_month + days_this_month_before_today);
    result *= 86400L;

    // Finally, add in the hours, minutes, and seconds of today:
    result += tm->tm_hour * 3600;
    result += tm->tm_min * 60;
    result += tm->tm_sec;

    return result;
#else
    return mktime(tm);
#endif
}
