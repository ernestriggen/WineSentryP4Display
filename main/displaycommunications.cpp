// displaycommunications.cpp

// ============================================================================
// displaycommunications.cpp
//
// P4 UART TRANSPORT LAYER
//
// Responsibilities:
//   - Initialize UART communication with the ESP32-S3
//   - Send messages from the P4 to the S3
//   - Serialize outgoing WineSentry messages
//   - Generate unique outgoing message IDs
//
// Not responsible for:
//   - Receiving or dispatching incoming messages
//   - Interpreting STATUSBAR, LINE, or STATE_CHANGE payloads
//   - Managing LVGL screens or widgets
//
// Incoming messages are processed by DisplayCommunicationsLoop().
// ============================================================================

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <string>
#include <cstring>
#include <cJSON.h>

#include "DisplayCommunications.h"
#include "models/displaycommunicationsmodels.h"
#include "models/winesentrystatemachine.h"
#include "ping.h"
#include "statusbar.h"

enum class DisplayMessageType
{
    STATE_CHANGE,
    STATUSBAR,
    LINE,
    PING, 
    UNKNOWN
};

static DisplayMessageType GetDisplayMessageType(const char* type)
{
    if (type == nullptr)
        return DisplayMessageType::UNKNOWN;

    if (strcmp(type, "STATE_CHANGE") == 0)
        return DisplayMessageType::STATE_CHANGE;

    if (strcmp(type, "STATUSBAR") == 0)
        return DisplayMessageType::STATUSBAR;

    if (strcmp(type, "LINE") == 0)
        return DisplayMessageType::LINE;

    if (strcmp(type, "PING") == 0)
        return DisplayMessageType::PING;

    return DisplayMessageType::UNKNOWN;
}


static const char* TAG = "P4DisComm";

static constexpr uart_port_t DISPLAY_UART = UART_NUM_1;

esp_err_t InitializeDisplayCommunications()
{
    uart_config_t config = {};

    config.baud_rate = DISPLAY_UART_BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(DISPLAY_UART, &config);

    if (err != ESP_OK)
        return err;

    err = uart_set_pin(
        DISPLAY_UART,
        DISPLAY_UART_TX_PIN,
        DISPLAY_UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (err != ESP_OK)
        return err;

    err = uart_driver_install(
        DISPLAY_UART,
        2048,
        1024,
        0,
        nullptr,
        0
    );

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "UART1 initialized: TX=%d RX=%d baud=%d",
                 DISPLAY_UART_TX_PIN,
                 DISPLAY_UART_RX_PIN,
                 DISPLAY_UART_BAUD);
    }

    return err;
}

static WineSentryStateMachine wineStateMachine;

void DisplayCommunicationsLoop(void* parameter)
{
    uint8_t buffer[256];
    std::string receivedJson;

    constexpr size_t MAX_JSON_SIZE = 16384;
    bool discarding = false;

    while (true)
    {
        int length = uart_read_bytes(
            DISPLAY_UART,
            buffer,
            sizeof(buffer),
            pdMS_TO_TICKS(100)
        );

        if (length <= 0)
            continue;

        for (int i = 0; i < length; i++)
        {
            char c = (char)buffer[i];

            if (c == '\n')
            {
                if (!discarding && !receivedJson.empty())
                {
                    cJSON* root = cJSON_Parse(receivedJson.c_str());
                    
                    if (root != nullptr)
                    {
                        const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "messageType");

                        const char* typeString = cJSON_GetStringValue(type);

                        switch (GetDisplayMessageType(typeString))
                        {
                            case DisplayMessageType::STATE_CHANGE:
                                {
                                StateChangeMessage message;

                                if (message.FromJson(receivedJson.c_str()))
                                {
                                    wineStateMachine.ProcessStateChange(message);
                                }
                                else
                                {
                                    ESP_LOGW(TAG, "Invalid state-change message");
                                }
                                break;
                                }

                            case DisplayMessageType::STATUSBAR:
                                {
                                StatusBarHandle(receivedJson.c_str());
                                break;
                                }

                            case DisplayMessageType::LINE:
                                {
                                // LINE handler will go here.
                                ESP_LOGI(TAG, "LINE message received");
                                break;
                                }

case DisplayMessageType::PING:
  {
  const cJSON* payload =
      cJSON_GetObjectItemCaseSensitive(root, "payload");

  const cJSON* sleep =
      cJSON_GetObjectItemCaseSensitive(payload, "goingToSleep");

  PingHandle(cJSON_IsTrue(sleep));

  break;
  }
                            default:
                                {
                                ESP_LOGW(TAG, "Unknown message type: %s", typeString ? typeString : "<missing>");

                                ESP_LOGI(TAG, "Full UART JSON: %s", receivedJson.c_str());
                                break;
                                }
                        }
                        cJSON_Delete(root);
                    }
                    else
                    {
                       ESP_LOGW(TAG, "Invalid JSON received (%u bytes)", (unsigned)receivedJson.size());
                    }
                }

                receivedJson.clear();
                discarding = false;
            }
            else if (!discarding && c != '\r')
            {
                if (receivedJson.size() < MAX_JSON_SIZE)
                {
                    receivedJson += c;
                }
                else
                {
                    ESP_LOGW("DisplayCommunications",
                             "JSON message too large; discarding");

                    receivedJson.clear();
                    discarding = true;
                }
            }
        }
    }
}