#pragma once

// Called with the COMPLETE, newline-free WineSentry UART JSON envelope.
// This first implementation only logs and validates. It does not alter LVGL.
void WinePresentationHandle(const char* json);
