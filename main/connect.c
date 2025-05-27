#include "connect.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "modbus_rtu.h"

#define MAXIMUM_RETRY  10

//Last will messege configuration.
char lwt_topic[] =  "esp32/status";
char lwt_message[] = "disconnected"; 
#define LWT_QOS 2
#define LWT_RETAIN true

enum{
    CID_INPUT_X_SKEW = 0,                   //Floating point.
    CID_INPUT_Y_SKEW,                       //Floating point.      
    CID_INPUT_Z_SKEW,                       //Floating point.
    CID_INPUT_CURRENT_DATA,                 //Floating point.
    CID_INPUT_FLOW_RATE_DATA,               //Floating point.
    CID_INPUT_TOTAL_FLOW_DATA,              //Floating point.
    CID_COIL_PUMP                           //Pump ON or OFF.
};


esp_mqtt_client_handle_t client;

extern const uint8_t server_cert_pem_start[] asm("_binary_fullchain_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_fullchain_pem_end");

const char *TAG = "Connect.c";

extern EventGroupHandle_t events_group;
extern QueueHandle_t messenger;
extern TimerHandle_t modbus_read_timer_handle;

typedef struct {
  char text[16];
  bool warning;
}messages;

static int s_retry_num = 0;

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define MQTT_CONNECTED_BIT      BIT2
#define MQTT_DISCONNECT_BIT     BIT3
#define MQTT_PUBLISH_BIT        BIT4
#define MQTT_SUBSCRIBE_BIT      BIT5

void send_to_oled(char *text,bool warning){
    messages note;
    note.warning = warning;
    strncpy(note.text,text,sizeof(note.text));
    xQueueSendToBack(messenger,(void *)&note,0);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data){
    char str [20];
    switch(event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();

            //Oled
            send_to_oled("Wifi start",false);
        
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");

            } else {
                xEventGroupSetBits(events_group, WIFI_FAIL_BIT);
            }
            ESP_LOGI(TAG,"connect to the AP fail");
            //Oled
            send_to_oled("Wifi failed",true);
        
            break;

        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(events_group, WIFI_CONNECTED_BIT);

            //Oled
            
            sprintf(str,IPSTR,IP2STR(&event->ip_info.ip));
            send_to_oled(str,true);

            break;

        default:
            break;
    }
}

void wifi_connect(const char * wifi_ssid, const char * wifi_password){
    /*
    Initialize the underlying TCP/IP stack.
    This function should be called exactly once from application code, when the application starts up.
    ESP-NETIF serves as an intermediary between an IO driver and a network stack.
    */
    ESP_ERROR_CHECK(esp_netif_init());

    /*
    The default event loop is a special type of loop used for system events (Wi-Fi events, for example).
    The handle for this loop is hidden from the user.
    */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*
    The below function creates esp_netif object with default WiFi station config,
    attaches the netif to wifi and registers wifi handlers to the default event loop.
    This API uses assert() to check for potential errors, so it could abort the program.
    (Note that the default event loop needs to be created prior to calling this API)
    */
    esp_netif_create_default_wifi_sta();//This function return the esp_netif handle, i.e., a pointer to a network interface object allocated and configured with default settings

    /*
    Using WIFI_INIT_CONFIG_DEFAULT macro to initialize the configuration to default values,
    this can guarantee all the fields get correct value when more fields are added into wifi_init_config_t in future release.
    */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); 
    
    /*
    Initialize WiFi Allocate resource for WiFi driver,
    such as WiFi control structure, RX/TX buffer, WiFi NVS structure etc.
    This WiFi also starts WiFi task.
    */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    //Register an instance of event handler (wifi_event_handler()) to the default loop created above.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

    //Register an instance of event handler (wifi_event_handler()) to the default loop created above.                                             
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    //Wifi Credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* 
    Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
    number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) 
    */
    EventBits_t bits = xEventGroupWaitBits(events_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* 
    xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
    happened. 
    */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",wifi_ssid, wifi_password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",wifi_ssid, wifi_password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void mqtt_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data){
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    char str[20],mqtt_str[10];
    char on[]="on";
    char off[]="off";
    switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            //esp_mqtt_client_subscribe(client, "Temp", 2);
            esp_mqtt_client_subscribe(client,"pump",2);
            esp_mqtt_client_publish(client, lwt_topic, "connected" , 0, 1, 1);
            xEventGroupSetBits(events_group, MQTT_CONNECTED_BIT);

            //Oled
            send_to_oled("MQTT connected",false);

            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupSetBits(events_group, MQTT_DISCONNECT_BIT);

            //Oled
            send_to_oled("MQTT dis-connect",true);

            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");
            xEventGroupSetBits(events_group, MQTT_SUBSCRIBE_BIT);

            //Oled
            send_to_oled("MQTT SUB",false);

            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED");

            //Oled
            send_to_oled("MQTT UN-SUB",true);

            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
            xEventGroupSetBits(events_group, MQTT_PUBLISH_BIT);

            //Oled
            send_to_oled("MQTT PUP",false);

            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            sprintf(mqtt_str,"%.*s",event->data_len,event->data);
            //Oled

            sprintf(str,"%.*s : %.*s",event->topic_len,event->topic,event->data_len,event->data);
            send_to_oled(str,true);
            //We are subscribing to only one topic and it is "pump" sent by node-red dashboard and it sets the pump on/off.
            if(strcmp(mqtt_str,on)==0){
                xTimerStart(modbus_read_timer_handle,portMAX_DELAY);
                bool value = true;
                bool *ptr = &value;
                write_modbus_data(CID_COIL_PUMP, (void *)ptr);
            }else{
                xTimerStop(modbus_read_timer_handle,portMAX_DELAY);
                bool value = false;
                bool *ptr = &value;
                write_modbus_data(CID_COIL_PUMP, (void *)ptr);
            }

            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

            //Oled
            send_to_oled("MQTT Error",false);

            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            
            //Oled
            sprintf(str,"MQTT Event:%d",event->event_id);
            send_to_oled(str,true);

            break;
    }
}

void mqtt_connect(const char * mqtt_id,const char * mqtt_password){
    esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
        .address.uri = "change it",                                         //Secure MQTT broker URL mqtts://example.com:8883 
        .verification.certificate = (const char *)server_cert_pem_start,    //CA cert (fullchain.pem)
    },
    .credentials = {
        .username = mqtt_id,                                                //Username for authentication (if needed)
        .authentication.password = mqtt_password,                           //Password for authentication (if needed)
    },
    .session = {
        .last_will = { //This last will message is used to detect if the pump is connected or not.
            .topic = lwt_topic,
            .msg = "disconnected",
            .msg_len = 13,
            .retain = true
        }
    },
  
    .buffer = {
        .size = 1024,                                                       //Adjust buffer size if needed
    },
    .network = {
        .reconnect_timeout_ms = 10000,                                      //Optional reconnect time
    },
};  
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
