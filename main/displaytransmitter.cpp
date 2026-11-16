// displaytransmitter.cpp


#include "displaytransmitter.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>

static const char* TAG = "P4Transmitter";

esp_err_t DisplaySendStateChange(
    uart_port_t uartPort,
    const char* state
)
{
    if (state == nullptr || !uart_is_driver_installed(uartPort))
    {
        return ESP_ERR_INVALID_STATE;
    }

    char message[192];

    int length = snprintf(
        message,
        sizeof(message),
        "{\"ws\":\"WS1\",\"type\":\"STATECHANGE\",\"state\":\"%s\"}\n",
        state
    );

    if (length <= 0 || length >= sizeof(message))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    int written = uart_write_bytes(
        uartPort,
        message,
        length
    );

    if (written != length)
    {
        ESP_LOGE(TAG, "UART transmission failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "State change queued for S3: %s", state);

    return ESP_OK;
}
