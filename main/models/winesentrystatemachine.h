#pragma once

#include <string>
#include "models/displaycommunicationsmodels.h"

class WineSentryStateMachine
{
public:
    void ProcessStateChange(const StateChangeMessage& message);

    const std::string& GetCurrentState() const;

private:
    std::string currentState = "UNKNOWN";
};