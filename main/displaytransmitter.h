// displaytransmitter.h

#pragma once

#include <driver/uart.h>
#include <esp_err.h>

// Use the UART port already configured for S3 communication.
esp_err_t DisplaySendStateChange(
    uart_port_t uartPort,
    const char* state
);
