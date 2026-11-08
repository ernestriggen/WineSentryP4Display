#pragma once

#include <esp_err.h>

#define DISPLAY_UART_TX_PIN    21
#define DISPLAY_UART_RX_PIN    20
#define DISPLAY_UART_BAUD      115200

esp_err_t InitializeDisplayCommunications();
void DisplayCommunicationsLoop(void* parameter);