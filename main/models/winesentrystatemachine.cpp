#include "winesentrystatemachine.h"

#include <esp_log.h>

static const char* TAG = "WineStateMachine";

void WineSentryStateMachine::ProcessStateChange(
    const StateChangeMessage& message)
{
    if (message.currentState.empty())
    {
        ESP_LOGW(TAG, "Ignoring empty state");
        return;
    }

    ESP_LOGI(TAG, "State change: %s -> %s",
             message.previousState.c_str(),
             message.currentState.c_str());

    currentState = message.currentState;

    ESP_LOGI(TAG, "Current P4 wine state: %s",
             currentState.c_str());
}

const std::string& WineSentryStateMachine::GetCurrentState() const
{
    return currentState;
}