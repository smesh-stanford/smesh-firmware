//
// User preference defaults for SMesh builds that are automatically
// baked into the firmware at compile time. 
//
// This inserts the below parameters into `NodeDB.cpp` during 
// initializtation such that we don't need to manually set the 
// preferences in the meshtastic app/through meshtastic's API with a designated config
//


// Build-time user preferences defaults
#pragma once

// Basic LoRa settings
#define USERPREFS_LORACONFIG_CHANNEL_NUM 104
#define USERPREFS_CONFIG_LORA_REGION meshtastic_Config_LoRaConfig_RegionCode_US

// Ringtone and timezone string (from userPrefs.jsonc)
// #define USERPREFS_RINGTONE_RTTTL "24:d=32,o=5,b=565:f6,p,f6,4p,p,f6,p,f6,2p,p,b6,p,b6,p,b6,p,b6,p,b,p,b,p,b,p,b,p,b,p,b,p,b,1p.,2p.,p"
#define USERPREFS_TZ_STRING "tzplaceholder                                         "

// Optional: define a build identifier for SMESH builds. This does NOT change
// the meshtastic enum used by the stack — that requires editing the
// protobuf/enum definitions. Instead these macros provide a lightweight
// build-time tag you can display from the firmware UI.
#define USERPREFS_FIRMWARE_EDITION_SMESH 1
#define USERPREFS_BUILD_NUMBER 1

/* Compile-time confirmation message to show whether these userprefs
 * are being pulled into the build. This emits a message during compile
 * with the macro values so you can confirm inclusion without flashing.
 */
// #define _UP_STR_HELPER(x) #x
// #define _UP_STR(x) _UP_STR_HELPER(x)
// #pragma message ("userprefs_defaults: USERPREFS_FIRMWARE_EDITION_SMESH=" _UP_STR(USERPREFS_FIRMWARE_EDITION_SMESH) " USERPREFS_BUILD_NUMBER=" _UP_STR(USERPREFS_BUILD_NUMBER))
// #undef _UP_STR
// #undef _UP_STR_HELPER

// Bluetooth
#define USERPREFS_BLUETOOTH_ENABLED 1
#define USERPREFS_FIXED_BLUETOOTH 336710

// Device
#define USERPREFS_CONFIG_DEVICE_NODE_INFO_BROADCAST_SECS 10800
#define USERPREFS_CONFIG_DEVICE_SERIAL_ENABLED 1

// Display
#define USERPREFS_DISPLAY_SCREEN_ON_SECS 600

// LoRa
// Channel is already defined above. Add frequency offset and hop limit.
#define USERPREFS_LORACONFIG_FREQUENCY_OFFSET 0.05f
#define USERPREFS_LORACONFIG_HOP_LIMIT 3

// Telemetry (seconds)
#define USERPREFS_AIR_QUALITY_ENABLED 1
#define USERPREFS_AIR_QUALITY_INTERVAL 60
#define USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL 600
#define USERPREFS_ENVIRONMENT_MEASUREMENT_ENABLED 1
#define USERPREFS_ENVIRONMENT_UPDATE_INTERVAL 180
#define USERPREFS_POWER_MEASUREMENT_ENABLED 1
#define USERPREFS_POWER_UPDATE_INTERVAL 300

