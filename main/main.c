#include <string.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

static const int RX_BUF_SIZE = 1024;
#define LED_PIN 2

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)
#define RTS_PIN (UART_PIN_NO_CHANGE) //(GPIO_NUM_4)
#define CTS_PIN (UART_PIN_NO_CHANGE) //(GPIO_NUM_5)

#define BAUD_RATE 9600
// #define BAUD_RATE 1200

#define OK "\r\nOK\r\n"
#define ERR "\r\nERROR\r\n"
#define CONN ("\r\nCONNECT 9600\r\n")

#define SSID ""
#define PASSPHARSE ""
#define TCPServerIP ""
#define TCPServerPort 6666

#define WIFI_MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT        BIT0
#define WIFI_FAIL_BIT             BIT1
#define MODEM_CONNECTED_BIT       BIT2
#define SOCKET_CONNECTED_BIT      BIT3

static int s_retry_num = 0;

static const char *WIFI_TAG = "WIFI";
static const char *SOCKET_TAG = "SOCKET";
static const char *MODEM_TAG = "MODEM";
static const char *TCP_TAG = "TCP";

static int sock;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        gpio_set_level(LED_PIN, 0);
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(WIFI_TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        gpio_set_level(LED_PIN, 1);
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void set_socket(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    struct sockaddr_in tcpServerAddr;
    bzero(&tcpServerAddr, sizeof(tcpServerAddr));
    tcpServerAddr.sin_addr.s_addr = inet_addr(TCPServerIP);
    tcpServerAddr.sin_family = AF_INET;
    tcpServerAddr.sin_port = htons(TCPServerPort);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (bits & WIFI_CONNECTED_BIT) {
        if(sock < 0) {
            ESP_LOGE(SOCKET_TAG, "Failed to allocate");
        }
        ESP_LOGI(SOCKET_TAG, "Created");
        if(connect(sock, (struct sockaddr *)&tcpServerAddr, sizeof(tcpServerAddr)) != 0) {
            ESP_LOGE(SOCKET_TAG, "Connect failed errno=%d", errno);
            close(sock);
        }
        ESP_LOGI(SOCKET_TAG, "Connected");
        xEventGroupSetBits(s_wifi_event_group, SOCKET_CONNECTED_BIT);
    }
    if (bits & WIFI_FAIL_BIT) {
        if(sock < 0) {
            close(sock);
            xEventGroupClearBits(s_wifi_event_group, SOCKET_CONNECTED_BIT);
        }
    }
}

esp_err_t wifi_init_sta(void)
{
    EventBits_t bits;
    bits = xEventGroupGetBits(s_wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Already connected");
        return ESP_OK;
    }

    esp_log_level_set("wifi", ESP_LOG_NONE); // disable wifi driver logging
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &set_socket,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASSPHARSE,
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(WIFI_TAG, "wifi_init_sta finished.");

    bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Connected");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(WIFI_TAG, "Connection failed");
        return ESP_ERR_WIFI_NOT_CONNECT;
    } else {
        ESP_LOGI(WIFI_TAG, "UNEXPECTED EVENT");
        return ESP_ERR_WIFI_NOT_INIT;
    }

    // /* The event will not be processed after unregister */
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    // vEventGroupDelete(s_wifi_event_group);
}

void init_serial(void) {
    const uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 3, RX_BUF_SIZE * 3, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, RTS_PIN, CTS_PIN);
}

int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    // vTaskDelay(100 / portTICK_PERIOD_MS);
    // const int txBytes = uart_write_bytes_with_break(UART_NUM_1, data, len, 10);
    // const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    const int txBytes = uart_tx_chars(UART_NUM_1, data, len);
    ESP_ERROR_CHECK(uart_wait_tx_done(UART_NUM_1, 20));
    // ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    ESP_LOG_BUFFER_HEXDUMP(logName, data, txBytes, ESP_LOG_WARN);
    return txBytes;
}

// static void tx_task(void *arg)
// {
//     static const char *TX_TASK_TAG = "TX_TASK";
//     esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
//     while (1) {
//         if(!QUIET){
//             sendData(TX_TASK_TAG, "OK\n");
//         }
//         vTaskDelay(100 / portTICK_PERIOD_MS);
//     }
// }

static void tcp_tx_task(void *arg)
{
    char rx_buffer[128] = {0};
    int rxBytes;
    int txBytes;
    // uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        SOCKET_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);
    if (bits & SOCKET_CONNECTED_BIT) {
        txBytes = 0;
        for(;;){
            rxBytes = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_WAITALL);
            // const int rxBytes = read(sock, rx_buffer, sizeof(rx_buffer)-1);
            while (rxBytes > txBytes) {
                // ESP_LOGI(TCP_TAG, "rxbytes: %d txBytes: %d", rxBytes, txBytes);
                txBytes = uart_write_bytes(UART_NUM_1, rx_buffer, rxBytes);
                // ESP_LOGI(TCP_TAG, "rxbytes: %d txBytes: %d", rxBytes, txBytes);
                rxBytes = rxBytes - txBytes;
                // ESP_LOGI(TCP_TAG, "rxbytes: %d txBytes: %d", rxBytes, txBytes);
                ESP_LOG_BUFFER_HEXDUMP(TCP_TAG, rx_buffer, txBytes, ESP_LOG_WARN);
            }
            bzero(rx_buffer, sizeof(rx_buffer));
            txBytes = 0;
            vTaskDelay(10);
        }
    }
}

static void serial_rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    static const char *REPLY_TAG = "TX_REPL";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    // uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    char data[128] = {0};
    EventBits_t bits;
    int length = 0;
    int rxBytes;
    while (1) {
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_1, (size_t*)&length));
        bits = xEventGroupGetBits(s_wifi_event_group);
        rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 50 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            if (!(bits & MODEM_CONNECTED_BIT) != 0) {
                data[rxBytes] = 0;
                // ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
                ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
                int start;
                if ( data[0] == 'A' && data[1] == 'T' ) {
                    start = 2;
                }
                if ( data[1] == 0x00 && data[1] == 'A' && data[2] == 'T' ) {
                    start = 3;
                }
                if ( data[1] == '+' && data[1] == '+' && data[2] == '+' ) {
                    vTaskDelay(50);
                }

                for (int i = start; i < rxBytes; i++) {
                    if ( data[i] >= 'A' && data[i] <= 'Z' ) {
                        if ( data[i] == 'D'){
                            ESP_ERROR_CHECK(wifi_init_sta());
                            vTaskDelay(1000);
                            sendData(REPLY_TAG, CONN);
                            xEventGroupSetBits(s_wifi_event_group, MODEM_CONNECTED_BIT);
                            i++;
                            while (data[i] >= '0' && data[i] <= '9' ) {
                                i++;
                            }
                        }
                        if ( data[i] == 'V' || data[i] == 'H' || data[i] == 'Q' || data[i] == 'E' || data[i] == 'Z' || data[i] == 'L' ) {
                            sendData(REPLY_TAG, OK);
                            if ( data[i+1] >= '0' && data[i+1] <= '9') {
                                i++;
                            }
                        }
                        if ( data[i] == 'S'){
                            sendData(REPLY_TAG, OK);
                            while ( (data[i+1] >= '0' && data[i+1] <= '9' ) ||  data[i+1] == '=') {
                                i++;
                            }
                        }
                    }

                    if (data[i] == '&') {
                        sendData(REPLY_TAG, OK);
                        i = (i + 2);
                    }
                }
            } else {
                ESP_LOG_BUFFER_HEXDUMP(MODEM_TAG, data, rxBytes, ESP_LOG_INFO);
                write(sock , data , rxBytes);
                bzero(data, sizeof(data));
            }
        }
    }
    free(data);
}

void app_main(void)
{
    esp_rom_gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    s_wifi_event_group = xEventGroupCreate();

    init_serial();
    xTaskCreate(serial_rx_task, "uart_rx_task", 1024*3, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(tcp_tx_task, "tcp_tx_task", 1024*3, NULL, configMAX_PRIORITIES, NULL);
}

// AT
// Q0 quiet disable
// E0 Echo disable
// &D0 DTR drop is interpreted according to the current &Qn setting as follows
// &C1 RLSD follows the state of the carrier. (Default.)
// V1 Verbose Enable
// S7=70 Wait Time for Carrier, Silence, or Dial Tone (50 Seconds)
// S6=4  Wait Time before Blind Dialing or for Dial Tone (2 Seconds)
// L3   Speaker Volume (0,1 low, 2 med, 3 high)
// DT00 Dial and Select tone dialing: tone dial the numbers that follow until a "P" is encountered. Affects current and subsequent dialing