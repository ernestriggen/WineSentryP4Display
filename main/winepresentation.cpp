// winepresentation.cpp -- ESP32-P4
// Existing wine presentation + live aroma/tasting icon keyword arrays.

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
#include "winepresentation.h"

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
static void ParseKeywords(
    const std::string& json,
    std::array<std::string, 5>& output
)
{
    if (json.empty() || json.size() > 2048)
    {
        return;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Invalid keyword array: %.80s", json.c_str());
        return;
    }

    int count = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        const char* text = cJSON_GetStringValue(item);
        if (text == nullptr || text[0] == '\0')
        {
            continue;
        }

        // Strict keyword characters; no paths, whitespace or punctuation.
        const size_t length = strlen(text);
        if (length == 0 || length > 32)
        {
            continue;
        }

        bool valid = true;
        for (const char* p = text; *p != '\0'; ++p)
        {
            if (!(*p >= 'a' && *p <= 'z') && *p != '_')
            {
                valid = false;
                break;
            }
        }

        if (!valid)
        {
            continue;
        }

        output[count++] = text;
        if (count == static_cast<int>(output.size()))
        {
            break;
        }
    }

    cJSON_Delete(root);
}

void WinePresentationHandle(const char* json)
{
    if (json == nullptr)
    {
        return;
    }

    WinePresentationMessage message;
    if (!message.FromJson(json))
    {
        ESP_LOGW(TAG, "Invalid WINE_PRESENTATION envelope");
        return;
    }

    ESP_LOGI(TAG, "Wine: %s", message.wineName.c_str());
    ESP_LOGI(TAG, "Winery: %s", message.winery.c_str());
    ESP_LOGI(TAG, "Vintage: %s", message.vintage.c_str());
    ESP_LOGI(TAG, "Region: %s", message.region.c_str());
    ESP_LOGI(TAG, "Cuvee: %s", message.cuvee.c_str());
    ESP_LOGI(TAG, "Price USD: %.2f", message.price);
    ESP_LOGI(TAG, "Aromas: %s", message.aromaKeywords.c_str());
    ESP_LOGI(TAG, "Taste: %s", message.tasteKeywords.c_str());

    // Populate wine detail screen with actual identification data.
    std::string displayWineName = GetDisplayWineName(
        message.wineName, message.winery);

    char retailValue[32] = {};
    if (message.price > 0.0)
    {
        snprintf(retailValue, sizeof(retailValue), "$%.2f", message.price);
    }

    // These strings stay alive until WineDetailsScreenShow has copied them
    // into its LVGL objects and completed the SPIFFS icon lookups.
    std::array<std::string, 5> aromas = {};
    std::array<std::string, 5> palate = {};
    ParseKeywords(message.aromaKeywords, aromas);
    ParseKeywords(message.tasteKeywords, palate);

    WineDetailsView wine{};
    wine.producer = message.winery.c_str();
    wine.wineName = displayWineName.c_str();
    wine.vintage = message.vintage.c_str();
    wine.cuvee = message.cuvee.c_str();
    wine.rating = "";
    wine.reportedValue = retailValue;

    for (int i = 0; i < 5; ++i)
    {
        wine.aromas[i] = aromas[i].c_str();
        wine.palate[i] = palate[i].c_str();
    }

    // Only this block changes LVGL objects; acquire the BSP lock.
    if (bsp_display_lock(1000) == ESP_OK)
    {
        ESP_LOGI(TAG, "About to show WineDetailsScreen");
        WineDetailsScreenShow(wine);
        ESP_LOGI(TAG, "WineDetailsScreenShow returned");
        bsp_display_unlock();

        ESP_LOGI(TAG, "Wine details displayed: %s / %s / %s",
                 wine.producer, wine.wineName, wine.vintage);
    }
    else
    {
        ESP_LOGE(TAG, "Unable to acquire LVGL lock for wine details");
    }

    // Preserve the original Base64 label verification/decoding path.
    const std::string& encoded = message.labelJpegBase64;
    if (encoded.empty())
    {
        ESP_LOGW(TAG, "Wine presentation received WITHOUT a label JPEG");
        return;
    }

    const size_t maxBase64Length = 4 * ((MAX_LABEL_JPEG_BYTES + 2) / 3);
    if (encoded.size() > maxBase64Length)
    {
        ESP_LOGE(TAG, "JPEG Base64 exceeds limit: %u characters",
                 static_cast<unsigned>(encoded.size()));
        return;
    }

    size_t decodedLength = 0;
    const unsigned char* input =
        reinterpret_cast<const unsigned char*>(encoded.data());

    int result = mbedtls_base64_decode(
        nullptr, 0, &decodedLength, input, encoded.size());

    if (result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL ||
        decodedLength == 0 || decodedLength > MAX_LABEL_JPEG_BYTES)
    {
        ESP_LOGE(TAG, "Invalid JPEG Base64 length (rc=%d, bytes=%u)",
                 result, static_cast<unsigned>(decodedLength));
        return;
    }

    std::vector<uint8_t> jpeg(decodedLength);
    result = mbedtls_base64_decode(
        jpeg.data(), jpeg.size(), &decodedLength, input, encoded.size());

    if (result != 0)
    {
        ESP_LOGE(TAG, "JPEG Base64 decode failed (rc=%d)", result);
        return;
    }

    jpeg.resize(decodedLength);
    if (!IsCompleteJpeg(jpeg))
    {
        ESP_LOGE(TAG, "JPEG invalid: missing SOI/EOI markers (%u bytes)",
                 static_cast<unsigned>(jpeg.size()));
        return;
    }

    ESP_LOGI(TAG, "WINE_PRESENTATION received, JPEG VALID: %u bytes",
             static_cast<unsigned>(jpeg.size()));

    if (!WineDetailsScreenPrepareLabel(jpeg.data(), jpeg.size()))
    {
        ESP_LOGE(TAG, "Unable to prepare wine label");
        return;
    }

    if (bsp_display_lock(1000) == ESP_OK)
    {
        WineDetailsScreenApplyLabel();
        bsp_display_unlock();
        ESP_LOGI(TAG, "Wine label displayed");
    }
    else
    {
        ESP_LOGE(TAG, "Unable to acquire LVGL lock for label");
    }
}
