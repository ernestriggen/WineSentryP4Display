// winepresentationdata.cpp 

// This code assembles our data as needed and passes it to displaywinedetailscreen to be presented

#include <bsp/esp-bsp.h>
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <cJSON.h>
#include <cstdio>
#include <string>
#include <vector>
#include <array>
#include <cstring>

#include "displaywinedetailscreen.h"
#include "models/displaycommunicationsmodels.h"
#include "winepresentationdata.h"

static const char* TAG = "P4WineRx";
static constexpr size_t MAX_LABEL_JPEG_BYTES = 64 * 1024;

static bool IsCompleteJpeg(const std::vector<uint8_t>& image)
{
    return image.size() >= 4 &&
           image[0] == 0xFF &&
           image[1] == 0xD8 &&
           image[image.size() - 2] == 0xFF &&
           image[image.size() - 1] == 0xD9;
}

static std::string GetDisplayWineName(
    const std::string& fullWineName,
    const std::string& winery
)
{
    if (winery.empty())
    {
        return fullWineName;
    }

    // Remove the winery prefix only when it is followed by a space.
    if (fullWineName.size() > winery.size() &&
        fullWineName.compare(0, winery.size(), winery) == 0 &&
        fullWineName[winery.size()] == ' ')
    {
        return fullWineName.substr(winery.size() + 1);
    }

    return fullWineName;
}

// The S3 transports keyword arrays as JSON text, e.g.
// ["cherry", "blackcurrant", "leather"]. Up to five
// valid string descriptors are displayed on this screen.
// Storage belongs to the caller and survives WineDetailsScreenShow().

static void ParseWineDescriptors(
    const std::string& json,
    std::array<std::string, 5>& labels,
    std::array<std::string, 5>& icons)
{
    if (json.empty())
    {
        return;
    }

    cJSON* root = cJSON_Parse(json.c_str());

    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return;
    }

    for (int i = 0; i < 5; i++)
    {
        const cJSON* item = cJSON_GetArrayItem(root, i);

        if (!cJSON_IsObject(item))
        {
            continue;
        }

        const char* label = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(item, "label")
        );

        const char* icon = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(item, "icon")
        );

        if (label != nullptr)
        {
            labels[i] = label;
        }

        if (icon != nullptr)
        {
            icons[i] = icon;
        }
    }

    cJSON_Delete(root);
}

static void ParseKeywords(
    const std::string& json,
    std::array<std::string, 5>& output)
{
    output.fill(std::string());

    if (json.empty() || json.size() > 2048)
    {
        return;
    }

    cJSON* root = cJSON_Parse(json.c_str());

    if (!cJSON_IsArray(root))
    {
        ESP_LOGW(TAG, "Invalid keyword array: %.80s", json.c_str());
        cJSON_Delete(root);
        return;
    }

    size_t count = 0;
    const cJSON* item = nullptr;

    cJSON_ArrayForEach(item, root)
    {
        if (count >= output.size())
        {
            break;
        }

        const char* text = cJSON_GetStringValue(item);

        if (text == nullptr)
        {
            continue;
        }

        const size_t length = strlen(text);

        if (length == 0 || length > 96)
        {
            continue;
        }

        // Allow spaces, punctuation and UTF-8 text.
        // Reject control characters.
        bool safe = true;

        for (const unsigned char* p =
                 reinterpret_cast<const unsigned char*>(text);
             *p != 0; ++p)
        {
            if (*p < 0x20 || *p == 0x7F)
            {
                safe = false;
                break;
            }
        }

        if (!safe)
        {
            continue;
        }

        // Keep the ORIGINAL description from OpenAI.
        output[count++] = text;
    }

    cJSON_Delete(root);

    ESP_LOGI(
        TAG,
        "Stored %u original wine descriptors",
        static_cast<unsigned>(count)
    );
}


///////////////////////////////////////////////////////////////////////////////////////////
// Assemble the complete wine presentation from the S3 message.
// No display operations occur here.
//
static WinePresentationData AssembleWinePresentationData(
    const WinePresentationMessage& message)
{
    WinePresentationData wine;

    // Wine identification
    wine.producer = message.winery;
    wine.wineName = GetDisplayWineName(
        message.wineName,
        message.winery
    );

    wine.vintage = message.vintage;
    wine.region = message.region;
    wine.cuvee = message.cuvee;

    // Wine Labs information
    wine.ratingScore = message.ratingScore;
    wine.ratingReviewCount = message.ratingReviewCount;
    wine.ratingSource = message.ratingSource;
    wine.ratingLwin = message.ratingLwin;

    wine.drinkingWindowStart = message.drinkingWindowStart;
    wine.drinkingWindowEnd = message.drinkingWindowEnd;

    // Retail value
    wine.retailPrice = message.price;

    if (message.price > 0.0)
    {
        char value[32] = {};

        const char *f = (message.price < 1000.00) ? "~$%.2f" : "~$%.0f";

        snprintf(
            value,
            sizeof(value),
            f,
            message.price
        );

        wine.reportedValue = value;
    }

    // Aromas and tasting characteristics
ParseWineDescriptors(
    message.aromaKeywords,
    wine.aromas,
    wine.aromaIcons
);

ParseWineDescriptors(
    message.tasteKeywords,
    wine.palate,
    wine.palateIcons
);
    return wine;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Receive the complete WINE_PRESENTATION message from the S3,
// assemble the wine information, and pass it to the display.
//
void WinePresentationHandle(const char* json)
{
    if (json == nullptr)
    {
        return;
    }

    // Deserialize the incoming S3 message.
    WinePresentationMessage message;

    if (!message.FromJson(json))
    {
        ESP_LOGW(TAG, "Invalid WINE_PRESENTATION envelope");
        return;
    }

    // Assemble our complete presentation object.
    WinePresentationData wine =
        AssembleWinePresentationData(message);

    ESP_LOGI(TAG, "Wine: %s", wine.wineName.c_str());
    ESP_LOGI(TAG, "Producer: %s", wine.producer.c_str());
    ESP_LOGI(TAG, "Vintage: %s", wine.vintage.c_str());
    ESP_LOGI(TAG, "Region: %s", wine.region.c_str());
    ESP_LOGI(TAG, "Rating: %.1f", wine.ratingScore);
    ESP_LOGI(TAG, "Reviews: %d", wine.ratingReviewCount);
    ESP_LOGI(TAG, "Drinking window: %d - %d",
             wine.drinkingWindowStart,
             wine.drinkingWindowEnd);

    // Decode the wine-label JPEG into our presentation object.
    // The display will subsequently handle hardware JPEG decoding.
    const std::string& encoded = message.labelJpegBase64;

    if (!encoded.empty())
    {
        const size_t maxBase64Length =
            4 * ((MAX_LABEL_JPEG_BYTES + 2) / 3);

        if (encoded.size() <= maxBase64Length)
        {
            size_t decodedLength = 0;

            const unsigned char* input =
                reinterpret_cast<const unsigned char*>(
                    encoded.data()
                );

            int result = mbedtls_base64_decode(
                nullptr,
                0,
                &decodedLength,
                input,
                encoded.size()
            );

            if (result == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL &&
                decodedLength > 0 &&
                decodedLength <= MAX_LABEL_JPEG_BYTES)
            {
                wine.labelJpeg.resize(decodedLength);

                result = mbedtls_base64_decode(
                    wine.labelJpeg.data(),
                    wine.labelJpeg.size(),
                    &decodedLength,
                    input,
                    encoded.size()
                );

                if (result == 0)
                {
                    wine.labelJpeg.resize(decodedLength);

                    if (!IsCompleteJpeg(wine.labelJpeg))
                    {
                        ESP_LOGW(TAG, "Wine label JPEG invalid");
                        wine.labelJpeg.clear();
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Wine label Base64 decode failed");
                    wine.labelJpeg.clear();
                }
            }
        }
        else
        {
            ESP_LOGW(TAG, "Wine label exceeds maximum size");
        }
    }

    ESP_LOGI(TAG, "Wine presentation assembled: %s",
             wine.wineName.c_str());

    // Present the complete wine information.
    if (bsp_display_lock(1000) == ESP_OK)
    {
        WineDetailsScreenShow(wine);

        bsp_display_unlock();

        ESP_LOGI(TAG, "Wine details displayed: %s",
                 wine.wineName.c_str());
    }
    else
    {
        ESP_LOGE(TAG,
                 "Unable to acquire LVGL lock for wine details");
    }}
