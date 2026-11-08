// models/displaycommunicationsmodels.cpp

#include "displaycommunicationsmodels.h"

#include <stdlib.h>
#include <limits.h>

// -------------------------------------------------------
// WineSentryMessage::ToJson
// -------------------------------------------------------

std::string WineSentryMessage::ToJson() const
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr)
        return "";

    cJSON* payload = PayloadToJson();

    if (payload == nullptr)
    {
        cJSON_Delete(root);
        return "";
    }

    cJSON_AddNumberToObject(root, "protocolVersion", protocolVersion);
    cJSON_AddNumberToObject(root, "messageId", messageId);

    // Store timestamp as a decimal string to preserve
    // full int64_t precision through cJSON.
    cJSON_AddStringToObject(
        root, "dateTime", std::to_string(dateTime).c_str());

    cJSON_AddStringToObject(root, "messageType", messageType.c_str());

    cJSON_AddItemToObject(root, "payload", payload);

    char* serialized = cJSON_PrintUnformatted(root);

    std::string result;

    if (serialized != nullptr)
    {
        result = serialized;
        free(serialized);
    }

    cJSON_Delete(root);

    return result;
}

// -------------------------------------------------------
// WineSentryMessage::FromJson
// -------------------------------------------------------

bool WineSentryMessage::FromJson(const char* json)
{
    if (json == nullptr)
        return false;

    cJSON* root = cJSON_Parse(json);

    if (root == nullptr)
        return false;

    const cJSON* version =
        cJSON_GetObjectItemCaseSensitive(root, "protocolVersion");

    const cJSON* id =
        cJSON_GetObjectItemCaseSensitive(root, "messageId");

    const cJSON* timestamp =
        cJSON_GetObjectItemCaseSensitive(root, "dateTime");

    const cJSON* type =
        cJSON_GetObjectItemCaseSensitive(root, "messageType");

    const cJSON* payload =
        cJSON_GetObjectItemCaseSensitive(root, "payload");

    bool valid =
        cJSON_IsObject(root) &&
        cJSON_IsNumber(version) &&
        cJSON_IsNumber(id) &&
        cJSON_IsString(timestamp) &&
        cJSON_IsString(type) &&
        cJSON_IsObject(payload);

    if (valid)
    {
        valid =
            version->valuedouble == WINESENTRY_PROTOCOL_VERSION &&
            id->valuedouble >= 0 &&
            id->valuedouble <= UINT32_MAX &&
            id->valuedouble == (uint32_t)id->valuedouble &&
            type->valuestring != nullptr &&
            messageType == type->valuestring;
    }

    int64_t parsedDateTime = 0;

    if (valid)
    {
        const char* value = timestamp->valuestring;

        if (value == nullptr || *value == '\0')
        {
            valid = false;
        }
        else
        {
            char* end = nullptr;
            parsedDateTime = strtoll(value, &end, 10);

            if (*end != '\0')
                valid = false;
        }
    }

    if (valid)
        valid = PayloadFromJson(payload);

    if (valid)
    {
        protocolVersion = (uint8_t)version->valueint;
        messageId = (uint32_t)id->valuedouble;
        dateTime = parsedDateTime;
    }

    cJSON_Delete(root);

    return valid;
}

// -------------------------------------------------------
// StateChangeMessage::PayloadToJson
// -------------------------------------------------------

cJSON* StateChangeMessage::PayloadToJson() const
{
    cJSON* payload = cJSON_CreateObject();

    if (payload == nullptr)
        return nullptr;

    if (cJSON_AddStringToObject(
            payload, "previousState", previousState.c_str()) == nullptr ||
        cJSON_AddStringToObject(
            payload, "currentState", currentState.c_str()) == nullptr)
    {
        cJSON_Delete(payload);
        return nullptr;
    }

    return payload;
}

// -------------------------------------------------------
// StateChangeMessage::PayloadFromJson
// -------------------------------------------------------

bool StateChangeMessage::PayloadFromJson(const cJSON* payload)
{
    if (!cJSON_IsObject(payload))
        return false;

    const cJSON* previous =
        cJSON_GetObjectItemCaseSensitive(payload, "previousState");

    const cJSON* current =
        cJSON_GetObjectItemCaseSensitive(payload, "currentState");

    if (!cJSON_IsString(previous) ||
        !cJSON_IsString(current))
        return false;

    if (previous->valuestring == nullptr ||
        current->valuestring == nullptr)
        return false;

    previousState = previous->valuestring;
    currentState = current->valuestring;

    return true;
}

cJSON* PingMessage::PayloadToJson() const
{
    cJSON* payload = cJSON_CreateObject();

    cJSON_AddBoolToObject(payload, "goingToSleep", goingToSleep);

    return payload;
}

bool PingMessage::PayloadFromJson(const cJSON* payload)
{
    if (!cJSON_IsObject(payload))
        return false;

    const cJSON* sleep = cJSON_GetObjectItemCaseSensitive(payload, "goingToSleep");

    goingToSleep = cJSON_IsTrue(sleep);

    return true;
}