#pragma once

#include <stdbool.h>

// Create the temporary STATUSBAR display.
void StatusBarInit();

// Receive and display a STATUSBAR JSON payload.
void StatusBarHandle(const char* json);

// Enable or suppress STATUSBAR presentation.
void StatusBarSetVisible(bool visible);