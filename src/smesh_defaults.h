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
#define USERPREFS_LORACONFIG_FREQUENCY_OFFSET 0.05f
#define USERPREFS_LORACONFIG_HOP_LIMIT 3

#define USERPREFS_TZ_STRING "tzplaceholder                                         "

// useful for identifying build. This will get printed upon clicking RST on the Meshtastic
#define USERPREFS_FIRMWARE_EDITION_SMESH 1
#define USERPREFS_BUILD_NUMBER 1

// Bluetooth
#define USERPREFS_BLUETOOTH_ENABLED 1
#define USERPREFS_FIXED_BLUETOOTH 336710

// Device
#define USERPREFS_CONFIG_DEVICE_NODE_INFO_BROADCAST_SECS 10800
#define USERPREFS_CONFIG_DEVICE_SERIAL_ENABLED 1

// Display
#define USERPREFS_DISPLAY_SCREEN_ON_SECS 600

// Telemetry (seconds)
#define USERPREFS_AIR_QUALITY_ENABLED 1
#define USERPREFS_AIR_QUALITY_INTERVAL 60
#define USERPREFS_CONFIG_DEVICE_TELEM_UPDATE_INTERVAL 600
#define USERPREFS_ENVIRONMENT_MEASUREMENT_ENABLED 1
#define USERPREFS_ENVIRONMENT_UPDATE_INTERVAL 180
#define USERPREFS_ENVIRONMENT_SCREEN_ENABLED 1
#define USERPREFS_POWER_MEASUREMENT_ENABLED 1
#define USERPREFS_POWER_UPDATE_INTERVAL 300

