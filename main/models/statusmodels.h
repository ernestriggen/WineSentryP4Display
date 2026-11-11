// statusmodels.h

#pragma once

#include <cstdint>
#include <string>

// ------------------------------------------------------------
// Sensor operating state
// ------------------------------------------------------------

enum class WineSentrySensorState
{
    UNKNOWN,
    STARTING,
    ACTIVE,
    SLEEPING,
    OFFLINE
};

// ------------------------------------------------------------
// Identity and configuration
// ------------------------------------------------------------

struct WineSentryIdentity
{
    std::string userDefinedName;
    std::string luxSenseID;
    std::string sensorModel;

    int sensitivity = -1;
};

// ------------------------------------------------------------
// Firmware versions
// ------------------------------------------------------------

struct WineSentryFirmware
{
    std::string sensorVersion;
    std::string s3Version;
    std::string p4Version;
};

// ------------------------------------------------------------
// Network connectivity
// ------------------------------------------------------------

struct WineSentryConnectivity
{
    bool wifiAvailable = false;
    bool lteAvailable = false;
    bool mqttConnected = false;

    int wifiRSSI = 0;

    std::string connectionType;

    // These indicate whether the sensor has reported
    // its current connectivity state.
    bool statusReceived = false;
};

// ------------------------------------------------------------
// Live sensor status
// ------------------------------------------------------------

struct WineSentryLiveStatus
{
    WineSentrySensorState state =
        WineSentrySensorState::UNKNOWN;

    int batteryPercent = -1;
    int alertCount = -1;

    std::string powerSource;

    // Sensor-reported uptime in seconds.
    uint64_t sensorUptimeSeconds = 0;

    // Local P4 timestamps, in milliseconds since boot.
    int64_t lastStatusReceivedMs = 0;
    int64_t lastPingReceivedMs = 0;
    int64_t lastWineReadingReceivedMs = 0;

    // Most recent reported sensor sleep intention.
    bool goingToSleep = false;
};

// ------------------------------------------------------------
// Complete WineSentry status model
// ------------------------------------------------------------

struct WineSentryStatusModel
{
    WineSentryIdentity identity;
    WineSentryFirmware firmware;
    WineSentryConnectivity connectivity;
    WineSentryLiveStatus live;
};
