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


///////////////////////////////////////////////////////////////////////////////////////////
// Serialize the wine reading into the message payload.
//
cJSON* WineReadingMessage::PayloadToJson() const
    {
    cJSON* payload = cJSON_CreateObject();

    if (payload == nullptr)
        return nullptr;

    if (cJSON_AddStringToObject(payload, "data", data.c_str()) == nullptr)
        {
        cJSON_Delete(payload);
        return nullptr;
        }

    return payload;
    }


///////////////////////////////////////////////////////////////////////////////////////////
// Extract the wine reading JSON from the message payload.
//
bool WineReadingMessage::PayloadFromJson(const cJSON* payload)
    {
    if (!cJSON_IsObject(payload))
        return false;

    const cJSON* item = cJSON_GetObjectItemCaseSensitive(payload, "data");

    if (!cJSON_IsString(item) || item->valuestring == nullptr)
        return false;

    data = item->valuestring;

    return true;
    }

cJSON* WinePresentationMessage::PayloadToJson() const
{
    cJSON* payload = cJSON_CreateObject();

    if (payload == nullptr)
    {
        return nullptr;
    }

    bool valid =
        cJSON_AddStringToObject(payload, "wineName", wineName.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "winery", winery.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "vintage", vintage.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "region", region.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "cuvee", cuvee.c_str()) != nullptr &&
        cJSON_AddNumberToObject(payload, "price", price) != nullptr &&
        cJSON_AddStringToObject(payload, "aromaKeywords", aromaKeywords.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "tasteKeywords", tasteKeywords.c_str()) != nullptr &&
        cJSON_AddStringToObject(payload, "labelJpegBase64", labelJpegBase64.c_str()) != nullptr;

    if (!valid)
    {
        cJSON_Delete(payload);
        return nullptr;
    }

    return payload;
}

bool WinePresentationMessage::PayloadFromJson(const cJSON* payload)
{
    if (!cJSON_IsObject(payload))
    {
        return false;
    }

    auto readString = [payload](const char* key) -> std::string
    {
        const char* value = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(payload, key)
        );

        return value != nullptr ? value : "";
    };

    wineName = readString("wineName");
    winery = readString("winery");
    vintage = readString("vintage");
    region = readString("region");
    cuvee = readString("cuvee");
    aromaKeywords = readString("aromaKeywords");
    tasteKeywords = readString("tasteKeywords");
    labelJpegBase64 = readString("labelJpegBase64");

    const cJSON* priceField =
        cJSON_GetObjectItemCaseSensitive(payload, "price");

    price = cJSON_IsNumber(priceField)
        ? priceField->valuedouble
        : 0.0;

    return !wineName.empty();
}    