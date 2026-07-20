#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "bt_hci_common.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#define MSG_SERVICE_UUID      0xFFF0
#define MSG_RX_CHAR_UUID      0xFFF1
#define MSG_TX_CHAR_UUID      0xFFF2

#define WIFI_SSID      "Mark"
#define WIFI_PASS      "mark360@"

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

int last_conn_state = -1;

static int client_sock = -1;

static int listen_sock = -1;

char received_msg[64] = "";
bool new_message = false;

char wifi_msg[128];
bool wifi_new_message = false;

static bool receive_mode = false;

volatile bool wifi_server_running = true;

#define BTN_UP      5
#define BTN_DOWN    6
#define BTN_SELECT  7
#define BTN_BACK    10

#define BUZZER_GPIO 4

#define NUM_REPLIES 4

static const char *TAG = "BLE_ADV_SCAN";

static bool ble_started = false;
static bool menu_active = true;

static bool ble_msg_running = false;

static bool wifi_msg_running = false;

static bool wifi_connected = false;
static bool wifi_stack_init = false;

static bool wifi_events_registered = false;

static bool wifi_receive_mode = false;

static TaskHandle_t ble_msg_task_handle = NULL;

static TaskHandle_t wifi_server_handle = NULL;
static TaskHandle_t wifi_msg_handle = NULL;

static int selected_menu = 0;

static int selected = 0;
static int last_selected = -1;

int ble_msg_selected = 0;

#define SCAN_LOCAL_NAME_MAX_LEN 32

typedef enum
{
    MENU_BLE_SCAN,
    MENU_WIFI_SCAN,
    MENU_BLE_MSG,
    MENU_WIFI_MSG,
    MENU_SETTINGS
} menu_state_t;

typedef enum
{
    BLE_MSG_RECEIVE = 0,
    BLE_MSG_SEND,
    BLE_MSG_BACK
} ble_msg_menu_t;

const char *ble_msg_items[] =
{
    "Receive Msg",
    "Send Msg",
    "Back"
};

const char *reply_msgs[] =
{
    "ACK",
    "ON MY WAY",
    "CALL ME",
    "OK"
};

const char *wifi_msg_items[] =
{
    "Receive Msg",
    "Send Msg",
    "Back"
};

menu_state_t current_menu = MENU_BLE_SCAN;

typedef struct {
    char scan_local_name[SCAN_LOCAL_NAME_MAX_LEN];
    uint8_t name_len;
} ble_scan_local_name_t;

typedef struct {
    uint8_t *q_data;
    uint16_t q_data_len;
} host_rcv_data_t;

static uint8_t hci_cmd_buf[128];

static uint16_t tx_handle;

static int tx_read_cb(uint16_t conn_handle,
                      uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt,
                      void *arg)
{
    return 0;
}


static uint16_t scanned_count = 0;
static QueueHandle_t adv_queue;

static void ble_app_on_sync(void);
static void ble_app_advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg);
void host_task(void *param);

void start_ble_message(void);
void send_message_menu(void);
void ble_message_menu(void);
void send_message_to_phone(const char *msg);

void wifi_msg_task(void *arg);

void wifi_connect_sta(void);
void start_wifi_message(void);
void wifi_msg_server_task(void *arg);

void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = 4,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0
    };
    ledc_channel_config(&channel);
}

void buzzer_tone(uint32_t freq, uint32_t duration_ms)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE,
                  LEDC_TIMER_0,
                  freq);

    ledc_set_duty(LEDC_LOW_SPEED_MODE,
                  LEDC_CHANNEL_0,
                  512);

    ledc_update_duty(LEDC_LOW_SPEED_MODE,
                     LEDC_CHANNEL_0);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_LOW_SPEED_MODE,
                  LEDC_CHANNEL_0,
                  0);

    ledc_update_duty(LEDC_LOW_SPEED_MODE,
                     LEDC_CHANNEL_0);
}

void display_menu(int selected)
{
    ssd1306_clear_screen();

    ssd1306_print_str(0,0,
        selected==0 ? "> BLE Scan" : "  BLE Scan",
        false);

    ssd1306_print_str(0,12,
        selected==1 ? "> WiFi Scan" : "  WiFi Scan",
        false);

    ssd1306_print_str(0,24,
        selected==2 ? "> BLE Msg" : "  BLE Msg",
        false);

    ssd1306_print_str(0,36,
        selected==3 ? "> WiFi Msg" : "  WiFi Msg",
        false);

    ssd1306_print_str(0,48,
        selected==4 ? "> Settings" : "  Settings",
        false);

    ssd1306_display();
}

void oled_show_device(const char *name, int rssi)
{
    char line[32];

    snprintf(line,sizeof(line),
             "RSSI:%d",rssi);

    ssd1306_clear_screen();

    ssd1306_print_str(0,0,"BLE Device",false);
    ssd1306_print_str(0,16,name,false);
    ssd1306_print_str(0,32,line,false);

    ssd1306_display();
}

void display_ble_msg_menu(int selected)
{
    ssd1306_clear_screen();

    ssd1306_print_str(
        0,
        0,
        "BLE MESSAGE",
        false);

    if(conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ssd1306_print_str(
            0,
            12,
            "Connected",
            false);
    }
    else
    {
        ssd1306_print_str(
            0,
            12,
            "Waiting...",
            false);
    }


    for(int i=0;i<3;i++)
    {
        char line[24];

        if(i == selected)
            snprintf(line,sizeof(line),"> %s",ble_msg_items[i]);
        else
            snprintf(line,sizeof(line),"  %s",ble_msg_items[i]);

        ssd1306_print_str(
            0,
            24 + (i * 12),
            line,
            false);
    }

    ssd1306_display();
}

void ble_message_menu(void)
{
    int selected = 0;
    
    while(!gpio_get_level(BTN_SELECT))
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI("BLE_MSG_MENU","Selected=%d",selected);

    display_ble_msg_menu(selected);

    while(1)
    {
        int current_conn_state =
        (conn_handle != BLE_HS_CONN_HANDLE_NONE);

        if(current_conn_state != last_conn_state)
        {
            display_ble_msg_menu(selected);

            last_conn_state = current_conn_state;
        }

        if(!gpio_get_level(BTN_UP))
        {
            selected--;

            if(selected < 0)
                selected = 2;

            display_ble_msg_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_DOWN))
        {
            selected++;

            if(selected > 2)
                selected = 0;

            display_ble_msg_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_SELECT))
        {
            switch(selected)
            {
                case BLE_MSG_RECEIVE:

                receive_mode = true;

                ssd1306_clear_screen();
                ssd1306_print_str(0,0,"RECEIVE MODE",false);
                ssd1306_print_str(0,16,"Waiting...",false);
                ssd1306_print_str(0,32,"BACK=EXIT",false);
                ssd1306_display();

                while(receive_mode)
                {
                    if(!gpio_get_level(BTN_BACK))
                    {
                        while(!gpio_get_level(BTN_BACK))
                        {
                            vTaskDelay(pdMS_TO_TICKS(20));
                        }

                        vTaskDelay(pdMS_TO_TICKS(200));

                        receive_mode = false;

                        display_ble_msg_menu(selected);

                        break;
                    }

                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                break;

                case BLE_MSG_SEND:
                    send_message_menu();
                    display_ble_msg_menu(selected);
                    break;

                case BLE_MSG_BACK:

                    nimble_port_stop();

                    vTaskDelay(pdMS_TO_TICKS(500));

                    nimble_port_deinit();

                    esp_bt_controller_disable();

                    esp_bt_controller_deinit();

                    {
                        vTaskDelete(ble_msg_task_handle);
                        ble_msg_task_handle = NULL;
                    }

                    conn_handle = BLE_HS_CONN_HANDLE_NONE;

                    ble_msg_running = false;

                    menu_active = true;

                    display_menu(selected_menu);

                    return;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void display_send_menu(int selected)
{
    ssd1306_clear_screen();

    ssd1306_print_str(
        0,
        0,
        "SEND MSG",
        false);

    for(int i=0;i<NUM_REPLIES;i++)
    {
        char line[24];

        if(i == selected)
            snprintf(line,sizeof(line),
                     "> %s",
                     reply_msgs[i]);
        else
            snprintf(line,sizeof(line),
                     "  %s",
                     reply_msgs[i]);

        ssd1306_print_str(
            0,
            16 + (i * 12),
            line,
            false);
    }

    ssd1306_display();
}

void send_message_menu(void)
{
    int selected = 0;

    while(!gpio_get_level(BTN_SELECT))
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    display_send_menu(selected);

    while(1)
    {
        if(!gpio_get_level(BTN_UP))
        {
            selected--;

            if(selected < 0)
                selected = NUM_REPLIES - 1;

            display_send_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_DOWN))
        {
            selected++;

            if(selected >= NUM_REPLIES)
                selected = 0;

            display_send_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_SELECT))
        {
            send_message_to_phone(
                reply_msgs[selected]);

            buzzer_tone(2500,100);

            ssd1306_clear_screen();

            ssd1306_print_str(
                0,
                0,
                "SENT",
                false);

            ssd1306_print_str(
                0,
                16,
                reply_msgs[selected],
                false);

            ssd1306_display();

            vTaskDelay(pdMS_TO_TICKS(1000));

            display_send_menu(selected);
        }

        if(!gpio_get_level(BTN_BACK))
        {
            receive_mode = false;

            while(!gpio_get_level(BTN_BACK))
            {
            vTaskDelay(pdMS_TO_TICKS(20));
            }

            vTaskDelay(pdMS_TO_TICKS(200));

            return;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void send_message_to_phone(const char *msg)
{
    if(conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW("BLE_MSG",
                 "Phone not connected");
        return;
    }

    struct os_mbuf *om =
        ble_hs_mbuf_from_flat(
            msg,
            strlen(msg));

    int rc =
        ble_gatts_notify_custom(
            conn_handle,
            tx_handle,
            om);

    if(rc == 0)
    {
        ESP_LOGI("BLE_MSG",
                 "Sent: %s",
                 msg);
    }
    else
    {
        ESP_LOGE("BLE_MSG",
                 "Notify failed rc=%d",
                 rc);
    }
}


void oled_display_message(const char *msg)
{
    char line[4][18];   // 4 lines, ~21 chars each
    memset(line, 0, sizeof(line));

    int current_line = 0;

    const char *p = msg;

    while(*p && current_line < 4)
    {
        char word[32];
        int w = 0;

        while(*p && *p != ' ')
        {
            word[w++] = *p++;
        }

        word[w] = '\0';

        if(*p == ' ')
            p++;

        int current_len = strlen(line[current_line]);

        if(current_len + w + 1 > 16)
        {
            current_line++;

            if(current_line >= 4)
                break;
        }

        if(strlen(line[current_line]) > 0)
            strcat(line[current_line], " ");

        strcat(line[current_line], word);
    }

    ssd1306_clear_screen();

    ssd1306_print_str(0,0,"BLE MESSAGE",false);

    for(int i=0;i<=current_line;i++)
    {
        ssd1306_print_str(
            0,
            16 + (i * 12),
            line[i],
            false);
    }

    ssd1306_display();
}

/*
 * @brief: BT controller callback function, used to notify the upper layer that
 *         controller is ready to receive command
 */
static void controller_rcv_pkt_ready(void)
{
    ESP_LOGI(TAG, "controller rcv pkt ready");
}

/*
 * @brief: BT controller callback function to transfer data packet to
 *         the host
 */
static int host_rcv_pkt(uint8_t *data, uint16_t len)
{
    host_rcv_data_t send_data;
    uint8_t *data_pkt;
    /* Check second byte for HCI event. If event opcode is 0x0e, the event is
     * HCI Command Complete event. Sice we have received "0x0e" event, we can
     * check for byte 4 for command opcode and byte 6 for it's return status. */
    if (data[1] == 0x0e) {
        if (data[6] == 0) {
            esp_rom_printf("Event opcode 0x%02x success.", data[4]);
        } else {
            esp_rom_printf("Event opcode 0x%02x fail with reason: 0x%02x.", data[4], data[6]);
            return ESP_FAIL;
        }
    }

    data_pkt = (uint8_t *)malloc(sizeof(uint8_t) * len);
    if (data_pkt == NULL) {
        esp_rom_printf("Malloc data_pkt failed!");
        return ESP_FAIL;
    }
    memcpy(data_pkt, data, len);
    send_data.q_data = data_pkt;
    send_data.q_data_len = len;
        
        /* If data sent successfully, then free the pointer in `xQueueReceive'
         * after processing it. Or else if enqueue in not successful, free it
         * here. */
        if(adv_queue==NULL)
        {
        free(data_pkt);
        return ESP_FAIL;
        }

        if (xQueueSend(adv_queue, &send_data, 0) != pdTRUE)
        {
            esp_rom_printf("Failed to enqueue advertising report. Queue full.");
            free(data_pkt);
            return ESP_FAIL;
        }

    
    return ESP_OK;
}

static int rx_write_cb(uint16_t conn_handle,
                       uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg)
{
    memset(received_msg, 0, sizeof(received_msg));

    int len = OS_MBUF_PKTLEN(ctxt->om);

    if(len > sizeof(received_msg)-1)
        len = sizeof(received_msg)-1;

    int rc = ble_hs_mbuf_to_flat(
                    ctxt->om,
                    received_msg,
                    len,
                    NULL);

    if(rc != 0)
    {
        ESP_LOGE("BLE_MSG",
                 "mbuf copy failed");
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI("BLE_MSG",
         "receive_mode=%d",
         receive_mode);

    received_msg[len] = '\0';

    ESP_LOGI("BLE_MSG",
             "RX CALLBACK HIT");

    ESP_LOGI("BLE_MSG",
             "Received: %s",
             received_msg);

    new_message = true;

    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] =
{
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),

        .characteristics =
        (struct ble_gatt_chr_def[])
        {
            {
                .uuid =
                BLE_UUID16_DECLARE(0xFFF1),

                .access_cb = rx_write_cb,

                .flags =
                    BLE_GATT_CHR_F_WRITE |
                    BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF2),
                
                .access_cb = tx_read_cb,

                .val_handle = &tx_handle,

                .flags =
                    BLE_GATT_CHR_F_NOTIFY |
                    BLE_GATT_CHR_F_READ,
            },
            {0}
        }
    },

    {0}
};

static esp_vhci_host_callback_t vhci_host_cb = {
    controller_rcv_pkt_ready,
    host_rcv_pkt
};

static void hci_cmd_send_reset(void)
{
    uint16_t sz = make_cmd_reset (hci_cmd_buf);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_set_evt_mask(void)
{
    /* Set bit 61 in event mask to enable LE Meta events. */
    uint8_t evt_mask[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
    uint16_t sz = make_cmd_set_evt_mask(hci_cmd_buf, evt_mask);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_scan_params(void)
{
    /* Set scan type to 0x01 for active scanning and 0x00 for passive scanning. */
    uint8_t scan_type = 0x01;

    /* Scan window and Scan interval are set in terms of number of slots. Each slot is of 625 microseconds. */
    uint16_t scan_interval = 0x50; /* 50 ms */
    uint16_t scan_window = 0x30; /* 30 ms */

    uint8_t own_addr_type = 0x00; /* Public Device Address (default). */
    uint8_t filter_policy = 0x00; /* Accept all packets except directed advertising packets (default). */
    uint16_t sz = make_cmd_ble_set_scan_params(hci_cmd_buf, scan_type, scan_interval, scan_window, own_addr_type, filter_policy);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_scan_start(void)
{
    uint8_t scan_enable = 0x01; /* Scanning enabled. */
    uint8_t filter_duplicates = 0x00; /* Duplicate filtering disabled. */
    uint16_t sz = make_cmd_ble_set_scan_enable(hci_cmd_buf, scan_enable, filter_duplicates);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    ESP_LOGI(TAG, "BLE Scanning started..");
    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"BLE STATUS",false);
    ssd1306_print_str(0,16,"SCAN Started",false);
    buzzer_tone(2000,100);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_tone(2000,100);
    ssd1306_display();
}

static void hci_cmd_send_ble_adv_start(void)
{
    uint16_t sz = make_cmd_ble_set_adv_enable (hci_cmd_buf, 1);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    ESP_LOGI(TAG, "BLE Advertising started..");
    buzzer_tone(1500,100);
    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"BLE STATUS",false);
    ssd1306_print_str(0,16,"ADV Started",false);
    ssd1306_display();
}

static void hci_cmd_send_ble_set_adv_param(void)
{
    /* Minimum and maximum Advertising interval are set in terms of slots. Each slot is of 625 microseconds. */
    uint16_t adv_intv_min = 0x100;
    uint16_t adv_intv_max = 0x100;

    /* Connectable undirected advertising (ADV_IND). */
    uint8_t adv_type = 0;

    /* Own address is public address. */
    uint8_t own_addr_type = 0;

    /* Public Device Address */
    uint8_t peer_addr_type = 0;
    uint8_t peer_addr[6] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85};

    /* Channel 37, 38 and 39 for advertising. */
    uint8_t adv_chn_map = 0x07;

    /* Process scan and connection requests from all devices (i.e., the White List is not in use). */
    uint8_t adv_filter_policy = 0;

    uint16_t sz = make_cmd_ble_set_adv_param(hci_cmd_buf,
                  adv_intv_min,
                  adv_intv_max,
                  adv_type,
                  own_addr_type,
                  peer_addr_type,
                  peer_addr,
                  adv_chn_map,
                  adv_filter_policy);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_set_adv_data(void)
{
    char *adv_name = "ESP-BLE-1";
    uint8_t name_len = (uint8_t)strlen(adv_name);
    uint8_t adv_data[31] = {0x02, 0x01, 0x06, 0x0, 0x09};
    uint8_t adv_data_len;

    adv_data[3] = name_len + 1;
    for (int i = 0; i < name_len; i++) {
        adv_data[5 + i] = (uint8_t)adv_name[i];
    }
    adv_data_len = 5 + name_len;

    uint16_t sz = make_cmd_ble_set_adv_data(hci_cmd_buf, adv_data_len, (uint8_t *)adv_data);
    esp_vhci_host_send_packet(hci_cmd_buf, sz);
    ESP_LOGI(TAG, "Starting BLE advertising with name \"%s\"", adv_name);
}

static esp_err_t get_local_name(uint8_t *data_msg, uint8_t data_len, ble_scan_local_name_t *scanned_packet)
{
    uint8_t curr_ptr = 0;

    /* Initialize output structure */
    scanned_packet->name_len = 0;
    scanned_packet->scan_local_name[0] = '\0';

    while (curr_ptr < data_len) {
        /* Ensure there is at least 1 byte for length field */
        if (curr_ptr >= data_len) {
            return ESP_FAIL;
        }
        uint8_t curr_len = data_msg[curr_ptr++];

        /* Length of 0 indicates end of AD structures or invalid data */
        if (curr_len == 0) {
            return ESP_FAIL;
        }

        /* Ensure there is at least 1 byte for type field */
        if (curr_ptr >= data_len) {
            return ESP_FAIL;
        }
        uint8_t curr_type = data_msg[curr_ptr++];

        /* Calculate data field length (curr_len includes type byte) */
        uint8_t data_field_len = curr_len - 1;

        /* Verify remaining buffer has enough data */
        if (curr_ptr + data_field_len > data_len) {
            return ESP_FAIL;
        }

        /* Check for Local Name type (0x08: Shortened, 0x09: Complete) */
        if (curr_type == 0x08 || curr_type == 0x09) {
            /* Limit copy length to prevent buffer overflow */
            uint8_t copy_len = data_field_len;
            if (copy_len > SCAN_LOCAL_NAME_MAX_LEN - 1) {
                copy_len = SCAN_LOCAL_NAME_MAX_LEN - 1;
            }

            memcpy(scanned_packet->scan_local_name, &data_msg[curr_ptr], copy_len);
            scanned_packet->scan_local_name[copy_len] = '\0';  /* Ensure null termination */
            scanned_packet->name_len = copy_len;
            return ESP_OK;
        }

        /* Move to next AD structure */
        curr_ptr += data_field_len;
    }

    return ESP_FAIL;
}

void hci_evt_process(void *pvParameters)
{
    host_rcv_data_t *rcv_data = (host_rcv_data_t *)malloc(sizeof(host_rcv_data_t));
    if (rcv_data == NULL) {
        ESP_LOGE(TAG, "Malloc rcv_data failed!");
        return;
    }
    esp_err_t ret;

    while (1) {
        uint8_t sub_event, num_responses, total_data_len, data_msg_ptr, hci_event_opcode;
        uint8_t *queue_data = NULL, *event_type = NULL, *addr_type = NULL, *addr = NULL, *data_len = NULL, *data_msg = NULL;
        short int *rssi = NULL;
        uint16_t data_ptr;
        ble_scan_local_name_t *scanned_name = NULL;
        total_data_len = 0;
        data_msg_ptr = 0;


        if(!gpio_get_level(BTN_BACK))
        {
        ESP_LOGI(TAG, "BACK pressed");

        esp_bt_controller_disable();
        esp_bt_controller_deinit();

        menu_active = true;
        ble_started = false;

        scanned_count = 0;

        selected = selected_menu;
        last_selected = -1;

        ssd1306_clear_screen();
        display_menu(selected_menu);

        vTaskDelay(pdMS_TO_TICKS(300));

        if(adv_queue)
        {
         vQueueDelete(adv_queue);
         adv_queue = NULL;
        }

        vTaskDelete(NULL);
        }


        if (xQueueReceive(adv_queue, rcv_data, pdMS_TO_TICKS(100)) != pdPASS) {
            continue;
        } else {
            /* `data_ptr' keeps track of current position in the received data. */
            data_ptr = 0;
            queue_data = rcv_data->q_data;

            /* Parsing `data' and copying in various fields. */
            hci_event_opcode = queue_data[++data_ptr];
            if (hci_event_opcode == LE_META_EVENTS) {
                /* Set `data_ptr' to 4th entry, which will point to sub event. */
                data_ptr += 2;
                sub_event = queue_data[data_ptr++];
                /* Check if sub event is LE advertising report event. */
                if (sub_event == HCI_LE_ADV_REPORT) {

                    scanned_count += 1;

                    /* Get number of advertising reports. */
                    num_responses = queue_data[data_ptr++];
                    event_type = (uint8_t *)malloc(sizeof(uint8_t) * num_responses);
                    if (event_type == NULL) {
                        ESP_LOGE(TAG, "Malloc event_type failed!");
                        goto reset;
                    }
                    for (uint8_t i = 0; i < num_responses; i += 1) {
                        event_type[i] = queue_data[data_ptr++];
                    }

                    /* Get advertising type for every report. */
                    addr_type = (uint8_t *)malloc(sizeof(uint8_t) * num_responses);
                    if (addr_type == NULL) {
                        ESP_LOGE(TAG, "Malloc addr_type failed!");
                        goto reset;
                    }
                    for (uint8_t i = 0; i < num_responses; i += 1) {
                        addr_type[i] = queue_data[data_ptr++];
                    }

                    /* Get BD address in every advetising report and store in
                     * single array of length `6 * num_responses' as each address
                     * will take 6 spaces. */
                    addr = (uint8_t *)malloc(sizeof(uint8_t) * 6 * num_responses);
                    if (addr == NULL) {
                        ESP_LOGE(TAG, "Malloc addr failed!");
                        goto reset;
                    }
                    for (int i = 0; i < num_responses; i += 1) {
                        for (int j = 0; j < 6; j += 1) {
                            addr[(6 * i) + j] = queue_data[data_ptr++];
                        }
                    }

                    /* Get length of data for each advertising report. */
                    data_len = (uint8_t *)malloc(sizeof(uint8_t) * num_responses);
                    if (data_len == NULL) {
                        ESP_LOGE(TAG, "Malloc data_len failed!");
                        goto reset;
                    }
                    for (uint8_t i = 0; i < num_responses; i += 1) {
                        data_len[i] = queue_data[data_ptr];
                        total_data_len += queue_data[data_ptr++];
                    }

                    if (total_data_len != 0) {
                        /* Get all data packets. */
                        data_msg = (uint8_t *)malloc(sizeof(uint8_t) * total_data_len);
                        if (data_msg == NULL) {
                            ESP_LOGE(TAG, "Malloc data_msg failed!");
                            goto reset;
                        }
                        for (uint8_t i = 0; i < num_responses; i += 1) {
                            for (uint8_t j = 0; j < data_len[i]; j += 1) {
                                data_msg[data_msg_ptr++] = queue_data[data_ptr++];
                            }
                        }
                    }

                    /* Counts of advertisements done. This count is set in advertising data every time before advertising. */
                    rssi = (short int *)malloc(sizeof(short int) * num_responses);
                    if (rssi == NULL) {
                        ESP_LOGE(TAG, "Malloc rssi failed!");
                        goto reset;
                    }
                    for (uint8_t i = 0; i < num_responses; i += 1) {
                        rssi[i] = -(0xFF - queue_data[data_ptr++]);
                    }

                    /* Extracting advertiser's name. */
                    data_msg_ptr = 0;
                    scanned_name = (ble_scan_local_name_t *)malloc(num_responses * sizeof(ble_scan_local_name_t));
                    if (scanned_name == NULL) {
                        ESP_LOGE(TAG, "Malloc scanned_name failed!");
                        goto reset;
                    }
                    for (uint8_t i = 0; i < num_responses; i += 1) {
                        ret = get_local_name(&data_msg[data_msg_ptr], data_len[i], &scanned_name[i]);

                        /* Print the data if adv report has a valid name. */
                        if (ret == ESP_OK) {
                            printf("******** Response %d/%d ********\n", i + 1, num_responses);
                            printf("Event type: %02x\nAddress type: %02x\nAddress: ", event_type[i], addr_type[i]);
                            for (int j = 5; j >= 0; j -= 1) {
                                printf("%02x", addr[(6 * i) + j]);
                                if (j > 0) {
                                    printf(":");
                                }
                            }

                            printf("\nData length: %d", data_len[i]);
                            data_msg_ptr += data_len[i];
                            printf("\nAdvertisement Name: ");
                            for (int k = 0; k < scanned_name[i].name_len; k += 1 ) {
                                printf("%c", scanned_name[i].scan_local_name[k]);
                            }
                            printf("\nRSSI: %ddB\n", rssi[i]);

                            char dev_name[32];

                            memset(dev_name, 0, sizeof(dev_name));

                            memcpy(dev_name,
                                    scanned_name[i].scan_local_name,
                                    scanned_name[i].name_len);

                            dev_name[scanned_name[i].name_len] = '\0';

                            oled_show_device(dev_name, rssi[i]);
                            buzzer_tone(2500,50);

                            char count_str[20];

                            sprintf(count_str,
                                    "Found:%d",
                                    scanned_count);

                            ssd1306_print_str(
                                    0,
                                    48,
                                    count_str,
                                    false);

ssd1306_display();
                            
                        }
                    }

                    /* Freeing all spaces allocated. */
reset:
                    free(scanned_name);
                    free(rssi);
                    free(data_msg);
                    free(data_len);
                    free(addr);
                    free(addr_type);
                    free(event_type);
                }
            }
#if (CONFIG_LOG_DEFAULT_LEVEL_DEBUG || CONFIG_LOG_DEFAULT_LEVEL_VERBOSE)
            printf("Raw Data:");
            for (uint8_t j = 0; j < rcv_data->q_data_len; j += 1) {
                printf(" %02x", queue_data[j]);
            }
            printf("\nQueue free size: %d\n", uxQueueSpacesAvailable(adv_queue));
#endif
            free(queue_data);
        }
        memset(rcv_data, 0, sizeof(host_rcv_data_t));
    }
}


void start_ble_scanner(void)
{
     if(ble_started)
        return;

    ble_started = true;
    
    bool continue_commands = 1;
    int cmd_cnt = 0;

    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"BLE Scanner",false);
    ssd1306_print_str(0,16,"Starting...",false);
    ssd1306_display();

    esp_bt_controller_config_t bt_cfg =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_bt_controller_mem_release(
            ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(
        esp_bt_controller_init(&bt_cfg));

    ESP_ERROR_CHECK(
        esp_bt_controller_enable(
            ESP_BT_MODE_BLE));

    adv_queue = xQueueCreate(
        15,
        sizeof(host_rcv_data_t));

    esp_vhci_host_register_callback(
        &vhci_host_cb);

    while (continue_commands)
    {
        if (esp_vhci_host_check_send_available())
        {
            switch (cmd_cnt)
            {
                case 0:
                    hci_cmd_send_reset();
                    break;

                case 1:
                    hci_cmd_send_set_evt_mask();
                    break;

                case 2:
                    hci_cmd_send_ble_set_adv_param();
                    break;

                case 3:
                    hci_cmd_send_ble_set_adv_data();
                    break;

                case 4:
                    hci_cmd_send_ble_adv_start();
                    break;

                case 5:
                    hci_cmd_send_ble_scan_params();
                    break;

                case 6:
                    hci_cmd_send_ble_scan_start();
                    break;

                default:
                    continue_commands = 0;
                    break;
            }

            cmd_cnt++;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    xTaskCreatePinnedToCore(
        hci_evt_process,
        "hci_evt_process",
        4096,
        NULL,
        6,
        NULL,
        0);
}


void wifi_scan_task(void *pvParameters)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    while(1)
    {
        if(!gpio_get_level(BTN_BACK))
        {
            ESP_LOGI(TAG,"Leaving WiFi Scan");

            menu_active = true;

            selected = selected_menu;
            last_selected = -1;

            buzzer_tone(1000,50);

            esp_wifi_scan_stop();

            esp_wifi_disconnect();

            display_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(300));

            vTaskDelete(NULL);
        }

        ssd1306_clear_screen();

        ssd1306_print_str(0,0,
                          "WiFi Scanning",
                          false);
             
        ssd1306_display();

        vTaskDelay(pdMS_TO_TICKS(100));

        esp_wifi_scan_start(
            &scan_config,
            true);

        uint16_t ap_count = 0;

        esp_wifi_scan_get_ap_num(
            &ap_count);

        wifi_ap_record_t ap_records[5];

        if(ap_count > 5)
            ap_count = 5;

        esp_wifi_scan_get_ap_records(
            &ap_count,
            ap_records);

        ssd1306_clear_screen();

        for(int i=0;i<ap_count;i++)
        {
            char line[32];

            snprintf(
                line,
                sizeof(line),
                "%.8s %d",
                (char*)ap_records[i].ssid,
                ap_records[i].rssi);

            ssd1306_print_str(
                0,
                i*12,
                line,
                false);
        }

        ssd1306_display();

        buzzer_tone(1500,50);

        for(int i=0;i<50;i++)
        {
            if(!gpio_get_level(BTN_BACK))
            {
                ESP_LOGI(TAG,"Leaving WiFi Scan");

                menu_active = true;
                selected = selected_menu;
                last_selected = -1;

                esp_wifi_scan_stop();
                esp_wifi_disconnect();
                display_menu(selected);

                vTaskDelete(NULL);
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


void start_wifi_scanner(void)
{

    if(!wifi_stack_init)
    {
        wifi_connect_sta();
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    menu_active = false;

    vTaskDelay(pdMS_TO_TICKS(500));   // Give Wi-Fi time to start

    xTaskCreate(
        wifi_scan_task,
        "wifi_scan_task",
        4096,
        NULL,
        5,
        NULL);
}

void return_to_ble_msg_menu(void)
{
    display_ble_msg_menu(0);
}

void ble_message_task(void *arg)
{
    while(1)
    {
        if(new_message && receive_mode)
        {
            new_message = false;

            oled_display_message(received_msg);

            buzzer_tone(2000,100);
            buzzer_tone(2500,100);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ble_message_init(void)
{
    ESP_LOGI(TAG,
             "Starting BLE Messaging");

    ESP_LOGI("BLE_MSG",
         "rx_write_cb addr=%p",
         rx_write_cb);

    nimble_port_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_svc_gap_device_name_set(
        "Pager");

    int rc;

    rc = ble_gatts_count_cfg(gatt_svcs);
    ESP_LOGI("BLE_MSG","count_cfg rc=%d", rc);

    if(rc != 0)
    {
        ESP_LOGE("BLE_MSG","count_cfg failed");
        return;
    }

    ESP_LOGI("BLE_MSG",
         "Before add svcs");

    ESP_ERROR_CHECK(
    ble_gatts_add_svcs(gatt_svcs));

    ESP_LOGI("BLE_MSG",
         "After add svcs");

    ESP_LOGI("BLE_MSG",
         "Services Registered");

    ble_hs_cfg.sync_cb =
        ble_app_on_sync;

    nimble_port_freertos_init(
        host_task);

    ssd1306_clear_screen();

    ssd1306_print_str(
        0,
        0,
        "BLE MESSAGE",
        false);

    ssd1306_print_str(
        0,
        16,
        "Waiting...",
        false);

    ssd1306_display();
}

void start_ble_message(void)
{

    if(ble_msg_running)
    {
        ESP_LOGI("BLE_MSG",
                 "BLE already running");
        return;
    }

    ble_msg_running = true;

    menu_active = false;

    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"BLE MESSAGE",false);
    ssd1306_print_str(0,16,"Waiting...",false);
    ssd1306_display();

    if(ble_started)
    {
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    ble_started = false;
    }

    ble_message_init();

    xTaskCreate(
        ble_message_task,
        "ble_msg",
        4096,
        NULL,
        5,
        &ble_msg_task_handle);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch(event->type)
    {
        case BLE_GAP_EVENT_CONNECT:

            if(event->connect.status == 0)
            {
                conn_handle = event->connect.conn_handle;
                ESP_LOGI("BLE_MSG",
                         "PHONE CONNECTED");
            }
            else
            {
                ESP_LOGI("BLE_MSG",
                         "CONNECT FAILED");
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI("BLE_MSG",
                     "PHONE DISCONNECTED");

            ble_app_advertise();
            break;
    }

    return 0;
}


static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    uint8_t addr_type;

    memset(&fields,0,sizeof(fields));

    fields.flags =
        BLE_HS_ADV_F_DISC_GEN |
        BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name = "Pager";

    fields.name =
        (uint8_t *)name;

    fields.name_len =
        strlen(name);

    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params,0,sizeof(adv_params));

    adv_params.conn_mode =
        BLE_GAP_CONN_MODE_UND;

    adv_params.disc_mode =
        BLE_GAP_DISC_MODE_GEN;

    ble_hs_id_infer_auto(0, &addr_type);

    ble_gap_adv_start(
        addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        gap_event,
        NULL);
}

static void ble_app_on_sync(void)
{
    ESP_LOGI("BLE_MSG",
         "BLE SYNC COMPLETE");
    ble_app_advertise();
}

void host_task(void *param)
{
    nimble_port_run();

    nimble_port_freertos_deinit();
}

static void wifi_event_handler(
        void *arg,
        esp_event_base_t event_base,
        int32_t event_id,
        void *event_data)
{
    if(event_base == IP_EVENT &&
       event_id == IP_EVENT_STA_GOT_IP)
    {
        wifi_connected = true;

        ESP_LOGI("WIFI_MSG",
                 "GOT IP");
    }

    if(event_base == WIFI_EVENT &&
       event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGE("WIFI_MSG",
             "Disconnected reason=%d",
             disc->reason);

        wifi_connected = false;
    }
}

void wifi_connect_sta(void)
{

    if(!wifi_stack_init)
    {
        ESP_LOGI("WIFI","1");
        ESP_ERROR_CHECK(esp_netif_init());

        ESP_LOGI("WIFI","2");
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        ESP_LOGI("WIFI","3");
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

        ESP_LOGI("WIFI","4");
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_LOGI("WIFI","5");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_stack_init = true;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    if(!wifi_events_registered)
    {
        ESP_ERROR_CHECK(
                esp_event_handler_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &wifi_event_handler,
                NULL));

        ESP_ERROR_CHECK(
                esp_event_handler_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &wifi_event_handler,
                NULL));

    wifi_events_registered = true;
                }
    ESP_ERROR_CHECK(
    esp_wifi_set_config(
        WIFI_IF_STA,
        &wifi_config));

    ESP_LOGI("WIFI","6");    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI","7");
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI("WIFI","8");
}

void wifi_msg_server_task(void *arg)
{
    struct sockaddr_in server_addr;
    struct timeval tv;

    listen_sock =
    socket(AF_INET,
           SOCK_STREAM,
           IPPROTO_IP);
    
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    setsockopt(listen_sock,
           SOL_SOCKET,
           SO_RCVTIMEO,
           &tv,
           sizeof(tv));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(listen_sock,
         (struct sockaddr *)&server_addr,
         sizeof(server_addr));

    listen(listen_sock, 1);

    while(wifi_server_running)
    {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        client_sock =
            accept(listen_sock,
                   (struct sockaddr *)&client_addr,
                   &len);

        if(!wifi_server_running)
        {
            break;
        }

        if(client_sock < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI("WIFI_MSG",
                 "PHONE CONNECTED");

        while(1)
        {
            int rx =
                recv(client_sock,
                     wifi_msg,
                     sizeof(wifi_msg)-1,
                     0);

            if(rx <= 0)
            {
                close(client_sock);
                client_sock = -1;

                ESP_LOGI("WIFI_MSG",
                         "PHONE DISCONNECTED");

                break;
            }

            wifi_msg[rx] = 0;

            wifi_new_message = true;

            ESP_LOGI("WIFI_MSG",
                     "RX=%s",
                     wifi_msg);
        }
       
    }
    ESP_LOGI("WIFI_MSG", "Server Task Exit");

    wifi_server_handle = NULL;

    vTaskDelete(NULL);
}

void start_wifi_message(void)
{

    if(wifi_server_handle != NULL)
    {
        return;
    }

    if(wifi_msg_running)
    {
        return;
    }

    wifi_server_running = true;

    wifi_msg_running = true;

    wifi_connected = false;

    esp_wifi_disconnect();

    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_connect_sta();

    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"WIFI MSG",false);
    ssd1306_print_str(0,16,"Connecting...",false);
    ssd1306_display();

    while(!wifi_connected)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    char ip_str[20];

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(
        esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
        &ip);

    sprintf(ip_str,
            IPSTR,
            IP2STR(&ip.ip));

    ssd1306_print_str(
        0,
        48,
        ip_str,
        false);

    ssd1306_clear_screen();
    ssd1306_print_str(0,0,"WIFI MSG",false);
    ssd1306_print_str(0,16,"Connected",false);
    ssd1306_print_str(0,32,"Port:5000",false);
    ssd1306_display();

    xTaskCreate(
        wifi_msg_server_task,
        "wifi_server",
        4096,
        NULL,
        5,
        &wifi_server_handle);

    xTaskCreate(
        wifi_msg_task,
        "wifi_msg",
        4096,
        NULL,
        5,
        &wifi_msg_handle);
}

void wifi_msg_task(void *arg)
{
    while(1)
    {
        if(wifi_new_message && wifi_receive_mode)
        {
            wifi_new_message = false;

            oled_display_message(
                wifi_msg);

            buzzer_tone(2000,100);
            buzzer_tone(2500,100);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void wifi_send_message(const char *msg)
{
    if(client_sock < 0)
    {
        ESP_LOGW("WIFI_MSG",
                 "Phone not connected");
        return;
    }

    int ret = send(client_sock,
                   msg,
                   strlen(msg),
                   0);

    if(ret < 0)
    {
        ESP_LOGE("WIFI_MSG",
                 "Send failed");
    }
    else
    {
        ESP_LOGI("WIFI_MSG",
                 "Sent: %s",
                 msg);
    }
}

void wifi_send_menu(void)
{
    int selected = 0;

    while(!gpio_get_level(BTN_SELECT))
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    display_send_menu(selected);

    while(1)
    {
        if(!gpio_get_level(BTN_UP))
        {
            selected--;

            if(selected < 0)
                selected = NUM_REPLIES-1;

            display_send_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_DOWN))
        {
            selected++;

            if(selected >= NUM_REPLIES)
                selected = 0;

            display_send_menu(selected);

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_SELECT))
        {
            wifi_send_message(reply_msgs[selected]);

            buzzer_tone(2500,100);

            ssd1306_clear_screen();

            ssd1306_print_str(
                0,
                0,
                "WIFI SENT",
                false);

            ssd1306_print_str(
                0,
                16,
                reply_msgs[selected],
                false);

            ssd1306_display();

            vTaskDelay(pdMS_TO_TICKS(1000));

            display_send_menu(selected);
        }

        if(!gpio_get_level(BTN_BACK))
        {
            while(!gpio_get_level(BTN_BACK))
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            vTaskDelay(pdMS_TO_TICKS(200));

            return;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void display_wifi_msg_menu(int selected)
{
    ssd1306_clear_screen();

    ssd1306_print_str(
        0,
        0,
        "WIFI MESSAGE",
        false);

    if(client_sock >= 0)
    {
        ssd1306_print_str(
            0,
            12,
            "Connected",
            false);
    }
    else
    {
        ssd1306_print_str(
            0,
            12,
            "Waiting...",
            false);
    }

    for(int i=0;i<3;i++)
    {
        char line[24];

        if(i==selected)
            sprintf(line,"> %s",wifi_msg_items[i]);
        else
            sprintf(line,"  %s",wifi_msg_items[i]);

        ssd1306_print_str(
            0,
            24+i*12,
            line,
            false);
    }

    ssd1306_display();
}

void wifi_message_menu(void)
{
    int selected = 0;
    int last_state = -1;

    while(!gpio_get_level(BTN_SELECT))
        vTaskDelay(pdMS_TO_TICKS(20));

    display_wifi_msg_menu(selected);

    while(1)
    {
        int state = (client_sock >= 0);

        if(state != last_state)
        {
            display_wifi_msg_menu(selected);

            last_state = state;
        }


        if(!gpio_get_level(BTN_UP))
        {
            selected--;
            if(selected < 0)
                selected = 2;

            display_wifi_msg_menu(selected);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_DOWN))
        {
            selected++;
            if(selected > 2)
                selected = 0;

            display_wifi_msg_menu(selected);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if(!gpio_get_level(BTN_SELECT))
        {
            switch(selected)
            {
                case 0:
                    wifi_receive_mode = true;

                    ssd1306_clear_screen();

                    ssd1306_print_str(0,0,"RECEIVE",false);
                    ssd1306_print_str(0,16,"Waiting...",false);
                    ssd1306_print_str(0,32,"BACK = EXIT",false);

                    ssd1306_display();

                    while(wifi_receive_mode)
                    {
                        if(!gpio_get_level(BTN_BACK))
                        {
                            while(!gpio_get_level(BTN_BACK))
                            {
                                vTaskDelay(pdMS_TO_TICKS(20));
                            }

                            vTaskDelay(pdMS_TO_TICKS(200));

                            wifi_receive_mode = false;

                            display_wifi_msg_menu(selected);

                            break;
                        }

                    vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    break;

                case 1:
                    wifi_send_menu();
                    display_wifi_msg_menu(selected);
                    break;

                case 2:

                wifi_server_running = false;

                if(client_sock >= 0)
                {
                    shutdown(client_sock, SHUT_RDWR);
                    close(client_sock);
                    client_sock = -1;
                }

                if(listen_sock >= 0)
                {
                    shutdown(listen_sock, SHUT_RDWR);
                    close(listen_sock);
                    listen_sock = -1;
                }

                if(wifi_msg_handle)
                {
                    vTaskDelete(wifi_msg_handle);
                    wifi_msg_handle = NULL;
                }

                esp_wifi_stop();

                wifi_msg_running = false;
                menu_active = true;

                display_menu(selected_menu);

                return;
            }

            vTaskDelay(pdMS_TO_TICKS(200));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
   
   init_ssd1306();
   buzzer_init();

   gpio_config_t io_conf = {
    .pin_bit_mask =
        (1ULL << BTN_UP) |
        (1ULL << BTN_DOWN) |
        (1ULL << BTN_SELECT) |
         (1ULL << BTN_BACK),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    /* Welcome Screen */
    ssd1306_print_str(15, 0, "WELCOME", false);
    ssd1306_print_str(5, 16, "Mark Gerald", false);
    buzzer_tone(1000,100);
    ssd1306_display();

    vTaskDelay(pdMS_TO_TICKS(2000));
    ssd1306_clear_screen();

    /* Loading Screen */
    for(int i = 0; i <= 100; i += 20)
    {
        ssd1306_clear_screen();
        char percent[16];

        snprintf(percent, sizeof(percent), "%d%%", i);

        ssd1306_print_str(0, 0, "Initializing", false);
        ssd1306_print_str(0, 16, "OLED  OK", false);

        if(i >= 20)
            ssd1306_print_str(0, 24, "BLE   OK", false);

        if(i >= 40)
            ssd1306_print_str(0, 32, "SCAN  OK", false);

        if(i >= 60)
            ssd1306_print_str(0, 40, "READY OK", false);

        ssd1306_print_str(90, 56, percent, false);

        ssd1306_display();

        buzzer_tone(5000,100);

        vTaskDelay(pdMS_TO_TICKS(400));
    }

    ssd1306_clear_screen();
    ssd1306_display();

    display_menu(0);


    while(1)
    {
        if(menu_active)
        {
            //  ESP_LOGI(TAG,"UP=%d DOWN=%d SEL=%d BACK=%d",
            //  gpio_get_level(BTN_UP),
            //  gpio_get_level(BTN_DOWN),
            //  gpio_get_level(BTN_SELECT),
            //  gpio_get_level(BTN_BACK));


            if(last_selected != selected)
            {
                display_menu(selected);
                last_selected = selected;
            }

            if(!gpio_get_level(BTN_UP))
            {
                if(selected > 0)
                    selected--;

                buzzer_tone(2000,50);
                vTaskDelay(pdMS_TO_TICKS(200));
            }

            if(!gpio_get_level(BTN_DOWN))
            {
                if(selected < 4)
                    selected++;

                buzzer_tone(2000,50);
                vTaskDelay(pdMS_TO_TICKS(200));
            }

            if(!gpio_get_level(BTN_SELECT))
            {
                selected_menu = selected;

                switch(selected)
                {
                    case MENU_BLE_SCAN:

                    if(wifi_stack_init)    
                    {
                        esp_wifi_stop();
                    }

                        menu_active = false;
                        start_ble_scanner();
                    
                        break;

                    case MENU_WIFI_SCAN:
                        start_wifi_scanner();
                        break;

                    case MENU_BLE_MSG:

                        if(!ble_msg_running)
                        {
                            start_ble_message();
                        }

                        display_ble_msg_menu(0);
                        ble_message_menu();
                        break;
                
                    case MENU_WIFI_MSG:
                        ssd1306_clear_screen();
                        ssd1306_print_str(0,0,"WIFI MESSAGE",false);
                        ssd1306_print_str(0,16,"Waiting...",false);
                        ssd1306_display();
                        start_wifi_message();
                        display_wifi_msg_menu(0);
                        wifi_message_menu();
                        break;

                    case MENU_SETTINGS:
                        ESP_LOGI(TAG,"Settings TBD");
                        break;
                }

                vTaskDelay(pdMS_TO_TICKS(300));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
}
