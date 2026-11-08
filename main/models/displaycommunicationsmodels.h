// displaycommunicationsmodels.h

// so we will create payloads that mainly go from S3 to P4 but occassionally come the other way around
// this contains the needed structures for those payloads.

#pragma once

#include <stdint.h>
#include <string>
#include <cJSON.h>

#define WINESENTRY_PROTOCOL_VERSION 1

class WineSentryMessage
{
public:
    uint8_t protocolVersion = WINESENTRY_PROTOCOL_VERSION;
    uint32_t messageId = 0;
    int64_t dateTime = 0;

    std::string messageType;

    virtual ~WineSentryMessage() = default;

    // Serialize the complete message to JSON
    std::string ToJson() const;

    // Populate this message from JSON
    bool FromJson(const char* json);

protected:
    // Implemented by each specialized message class
    virtual cJSON* PayloadToJson() const = 0;
    virtual bool PayloadFromJson(const cJSON* payload) = 0;
};


class StateChangeMessage : public WineSentryMessage
{
public:
    std::string previousState;
    std::string currentState;

    StateChangeMessage()
    {
        messageType = "STATE_CHANGE";
    }

protected:
    cJSON* PayloadToJson() const override;
    bool PayloadFromJson(const cJSON* payload) override;
};

class PingMessage : public WineSentryMessage
{
public:
    bool goingToSleep = false;
    
    PingMessage()
    {
        messageType = "PING";
    }

protected:
    cJSON* PayloadToJson() const override;
    bool PayloadFromJson(const cJSON* payload) override;
};



///////////////////////////////////////////////////////////////////////////////////////////
// Complete wine reading received from the sensor.
// The JSON is forwarded unchanged to the P4.
//
class WineReadingMessage : public WineSentryMessage
{
public:
    std::string data;

    WineReadingMessage()
        {
        messageType = "WINE_READING";
        }

protected:
    cJSON* PayloadToJson() const override;
    bool PayloadFromJson(const cJSON* payload) override;
};
