// processwinestatechange.cpp

#include <cstring>
#include <esp_log.h>
#include <cstring>
#include <string>

#include "bsp/esp-bsp.h"
#include "esp_log.h"

#include "models/winesentrystatemachine.h"
#include "models/displaycommunicationsmodels.h"
#include "wineglass.h"

static const char* TAG = "WINESTATECHANGE";

// methods called but defined later
bool WineStateFromString(const char* text, WINESTATE& state);
const char* WineStateToString(WINESTATE state);

void ProcessStateChange(const StateChangeMessage& message)
{
    static WINESTATE currentState = UNKNOWN;

    if (message.currentState.empty())
    {
        ESP_LOGW(TAG, "Ignoring empty state");
        return;
    }

    ESP_LOGI(TAG, "State change: %s -> %s",
             message.previousState.c_str(),
             message.currentState.c_str());
if (WineStateFromString(message.currentState.c_str(), currentState))
{
    if (bsp_display_lock(200) == ESP_OK)
    {
        ESP_LOGI(TAG, "State converted, LVGL locked. Updating wineglass.");

        WineGlassShow(currentState);

        ESP_LOGI(TAG, "WineGlassShow returned successfully.");

        bsp_display_unlock();
    }
    else
    {
        ESP_LOGE(TAG, "FAILED to acquire LVGL display lock!");
    }
}
else
{
    ESP_LOGE(TAG, "FAILED to convert wine state: %s",
             message.currentState.c_str());
}

    ESP_LOGI(TAG, "Current P4 wine state: %s",
             message.currentState.c_str());
}


////////////////////////////////////////////////////////////////

bool WineStateFromString(const char* text, WINESTATE& state)
{
    if (text == nullptr)
        return false;

    struct WineStateEntry
    {
        const char* name;
        WINESTATE value;
    };

    static constexpr WineStateEntry states[] =
    {
        {"WAITINGFORAUTOFOCUS",       WAITINGFORAUTOFOCUS},
        {"AUTOFOCUSCALIBRATED",       AUTOFOCUSCALIBRATED},
        {"BEGINPHOTOPROCESS",         BEGINPHOTOPROCESS},
        {"WAITINGFORBOTTLE",          WAITINGFORBOTTLE},
        {"WAITINGFORBOTTLETOBEGONE",  WAITINGFORBOTTLETOBEGONE},
        {"BOTTLETOOCLOSE",            BOTTLETOOCLOSE},
        {"BOTTLETOOFAR",              BOTTLETOOFAR},
        {"BOTTLEJUSTRIGHT_STEP1",     BOTTLEJUSTRIGHT_STEP1},
        {"BOTTLEJUSTRIGHT_STEP2",     BOTTLEJUSTRIGHT_STEP2},
        {"BOTTLEJUSTRIGHT_STEP3",     BOTTLEJUSTRIGHT_STEP3},
        {"BOTTLEJUSTRIGHT_ANDSTILL",  BOTTLEJUSTRIGHT_ANDSTILL},
        {"PHOTOTAKEN",                PHOTOTAKEN},
        {"SYSTEMIDLE",                SYSTEMIDLE},
        {"OTAUPDATEINPROGRESS",       OTAUPDATEINPROGRESS},
        {"IDENTIFICATIONINPROG",      IDENTIFICATIONINPROG},
        {"WINENOTFOUND",              WINENOTFOUND},
        {"WINEVINTAGEUNCERTAIN",      WINEVINTAGEUNCERTAIN},
        {"WINEFOUND",                 WINEFOUND},
        {"UNKNOWN",                   UNKNOWN},
    };

    for (const auto& entry : states)
    {
        if (std::strcmp(text, entry.name) == 0)
        {
            state = entry.value;
            return true;
        }
    }

    return false;
}

const char* WineStateToString(WINESTATE state)
{
    switch (state)
    {
        case WAITINGFORAUTOFOCUS:        return "WAITINGFORAUTOFOCUS";
        case AUTOFOCUSCALIBRATED:        return "AUTOFOCUSCALIBRATED";
        case BEGINPHOTOPROCESS:          return "BEGINPHOTOPROCESS";
        case WAITINGFORBOTTLE:           return "WAITINGFORBOTTLE";
        case WAITINGFORBOTTLETOBEGONE:   return "WAITINGFORBOTTLETOBEGONE";
        case BOTTLETOOCLOSE:             return "BOTTLETOOCLOSE";
        case BOTTLETOOFAR:               return "BOTTLETOOFAR";
        case BOTTLEJUSTRIGHT_STEP1:      return "BOTTLEJUSTRIGHT_STEP1";
        case BOTTLEJUSTRIGHT_STEP2:      return "BOTTLEJUSTRIGHT_STEP2";
        case BOTTLEJUSTRIGHT_STEP3:      return "BOTTLEJUSTRIGHT_STEP3";
        case BOTTLEJUSTRIGHT_ANDSTILL:   return "BOTTLEJUSTRIGHT_ANDSTILL";
        case PHOTOTAKEN:                 return "PHOTOTAKEN";
        case SYSTEMIDLE:                 return "SYSTEMIDLE";
        case OTAUPDATEINPROGRESS:        return "OTAUPDATEINPROGRESS";
        case IDENTIFICATIONINPROG:       return "IDENTIFICATIONINPROG";
        case WINENOTFOUND:               return "WINENOTFOUND";
        case WINEVINTAGEUNCERTAIN:       return "WINEVINTAGEUNCERTAIN";
        case WINEFOUND:                  return "WINEFOUND";
        case UNKNOWN:                    return "UNKNOWN";

        default:
            return "UNKNOWN";
    }
}