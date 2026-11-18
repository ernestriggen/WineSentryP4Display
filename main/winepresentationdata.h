// winepresentationdata.h

#pragma once

#include <string>
#include <array>
#include <vector>
#include <cstdint>

// behold the data we have gathered from the S3

struct WinePresentationData
{
    std::string producer;
    std::string wineName;
    std::string vintage;
    std::string region;
    std::string cuvee;

    double ratingScore = -1.0;
    int ratingReviewCount = 0;

    std::string ratingSource;
    std::string ratingLwin;

    int drinkingWindowStart = 0;
    int drinkingWindowEnd = 0;

    double retailPrice = 0.0;
    std::string reportedValue;

    std::array<std::string, 5> aromas = {};
    std::array<std::string, 5> palate = {};

    std::vector<uint8_t> labelJpeg;
};

// Receives and processes a WINE_PRESENTATION UART message.
void WinePresentationHandle(const char* json);