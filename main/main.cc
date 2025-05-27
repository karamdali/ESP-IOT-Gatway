#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "connect.h"
#include "ssd1306.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "modbus_rtu.h"
#include "cJSON.h"
#include <math.h>
#include "main_functions.h"

#define WIFI_SSID      "change it"
#define WIFI_PASS      "change it"

#define MQTT_ID         "change it"
#define MQTT_PASSWORD   "change it"

//RS485 pin configuration.
#define DE_RE_PIN       GPIO_NUM_23
#define TXD_PIN         GPIO_NUM_17 
#define RXD_PIN         GPIO_NUM_16

#define UART_NUM        UART_NUM_2
#define BUF_SIZE        128           //MPU data length for each axis.

enum{
    CID_INPUT_X_SKEW = 0,                   //Floating point.
    CID_INPUT_Y_SKEW,                       //Floating point.      
    CID_INPUT_Z_SKEW,                       //Floating point.
    CID_INPUT_CURRENT_DATA,                 //Floating point.
    CID_INPUT_FLOW_RATE_DATA,               //Floating point.
    CID_INPUT_TOTAL_FLOW_DATA,              //Floating point.
    CID_COIL_PUMP                           //Pump ON or OFF.
};


//Default time interval to read data from modbus slave and send it to mqtt server.
int interval = 5000;     
int timer_id = 1;          

extern esp_mqtt_client_handle_t client;

/* FreeRTOS event group to signal events*/
EventGroupHandle_t events_group;

/* The event group allows multiple bits for each events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries 
 * -
 * 
 * */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


/*
 * I2C is configured using menuconfig for ssd1306

 * CONFIG_SDA_GPIO    GPIO_NUM_21
 * CONFIG_SCL_GPIO    GPIO_NUM_23
 * CONFIG_RESET_GPIO  -1
*/


/*
 * This variabe will be used to queue messages from other tasks on the SSD1306.
*/
typedef struct {
  char text[16];
  bool warning;
}messages_t;


/*
 * This variabe will be used to queue data needed for json messeges from get_data_from_MODBUS_slave to MQTT_sender
*/
typedef struct {
  bool pump;
  float current;
  float flow_rate;
  float total_flow;
}JSON_DATA_t;

/*
 * We will get MPU sensor reading on each asix using modbus.
*/

typedef struct{
  float x;
  float y;
  float z;
}MPU_skew_t;



/*
 * This Queue will be used by the "display" task to show messeges from other tasks on the oled screen.
*/
QueueHandle_t messenger;


/*
 * This Queue will be used by the "MQTT_sender" task to get the data to send it to MQTT server as a JSON object.
*/
QueueHandle_t JSON_msg;


/*
 * This Queue will be used in the loop() in main_function.cc queueing the input data to the autoencoder.
*/
QueueHandle_t skew_queue;


/*
 * This Queue will be used in the loop() in main_function.cc queueing the result of the autoencoder.
*/
QueueHandle_t autoencoder;


TaskHandle_t get_data_from_MODBUS_slave_handle;

TimerHandle_t modbus_read_timer_handle;

void data_timer_cb( TimerHandle_t xTimer ){
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(get_data_from_MODBUS_slave_handle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*
 * Display task to display message from other tasks on SSD1306.
*/
void display(void *parameter){
  SSD1306_t dev;
  i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
  ssd1306_init(&dev, 128, 64);        //Panel is 128x64.
  ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);
  ssd1306_display_text(&dev, 0, "   --  LOG  --  ", 16, true);
  ssd1306_software_scroll(&dev, 1, (dev._pages - 1) );
  
  messages_t message;
  
  while(1){
    if(xQueueReceive(messenger,&message,portMAX_DELAY) == pdPASS){
      ssd1306_scroll_text(&dev, message.text, 16, message.warning);
    }
  }
}


void MQTT_sender(void *parameter){
  JSON_DATA_t data;
  bool result;
  while(1){
    if(xQueueReceive(JSON_msg,&data,portMAX_DELAY) == pdPASS){
      cJSON *root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "pump", data.pump?"on":"off");
      cJSON_AddNumberToObject(root, "current", data.current);
      cJSON_AddNumberToObject(root, "flow_rate",data.flow_rate);
      cJSON_AddNumberToObject(root, "total_flow", data.total_flow);
      xQueueReceive(autoencoder,&result,portMAX_DELAY);
      if (result){
        cJSON_AddStringToObject(root, "status", "normal");
      }else{
        cJSON_AddStringToObject(root, "status", "Anomaly");
      }
      char *json_string = cJSON_PrintUnformatted(root);
      if (json_string) {
        esp_mqtt_client_publish(client, "pump/data", json_string, 0, 1, 0); //Publish JSON string to MQTT.
        free(json_string); 
      }
      cJSON_Delete(root);
    }
  }
}

/*
 * Read data from modbus slave periodiclly.
*/
void get_data_from_MODBUS_slave(void *parameter){
  error_t err;
  err = modbusRTU_init(9600);     //Using UART2.
  if(err !=ESP_OK){
    printf("Initializing Modbus RTU Error : %d\n",err);
    return;
  }
  send_to_oled("Modbus OK", false);
  JSON_DATA_t JSON_data;
  MPU_skew_t axis;
  while(1){
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    printf("Timer expired. Performing Modbus data acquisition.\n");
    void* data = NULL;
    data = read_modbus_data(CID_COIL_PUMP);
    
    if(data == NULL) { //Failed to get parameter.
      send_to_oled("MB ERROR",true);
      continue;                       
    }


    //Get PUMP status.
    bool value = modbus_data_to_bool(data);
    char str [16];
    sprintf(str, "PUMP : %s", value? "ON":"OFF");
    
    send_to_oled(str,false);
    JSON_data.pump = modbus_data_to_bool(data);

    //If the pump is off Stop getting data. 
    if(!value){ 
      xTimerStop(modbus_read_timer_handle,portMAX_DELAY);
      continue;
    }

    //Get pump current.
    data = read_modbus_data(CID_INPUT_CURRENT_DATA);
    sprintf(str, "Cur : %.2f A", modbus_data_to_float(data));
    send_to_oled(str,false);
    JSON_data.current = modbus_data_to_float(data);

    //Get flow rate.
    data = read_modbus_data(CID_INPUT_FLOW_RATE_DATA);
    sprintf(str, "Q : %.2f mil/s", modbus_data_to_float(data));
    send_to_oled(str,false);
    
    JSON_data.flow_rate = modbus_data_to_float(data);

    //Get total flow.
    data = read_modbus_data(CID_INPUT_TOTAL_FLOW_DATA);
    sprintf(str, "V : %.2f mil", modbus_data_to_float(data));
    send_to_oled(str,false);
    JSON_data.total_flow = modbus_data_to_float(data);
    

    xQueueSendToBack(JSON_msg,(void *)&JSON_data,portMAX_DELAY);

    //Get MPU data.
    void * skew_data;
    skew_data = read_modbus_data(CID_INPUT_X_SKEW);
    axis.x = modbus_data_to_float(skew_data);
    skew_data = read_modbus_data(CID_INPUT_Y_SKEW);
    axis.y = modbus_data_to_float(skew_data);
    skew_data = read_modbus_data(CID_INPUT_Z_SKEW);
    axis.z = modbus_data_to_float(skew_data);
    xQueueSend(skew_queue,(void *)&axis,portMAX_DELAY);
  }
}


extern "C" {void app_main(void)
{
    /*
    Initialize NVS
    The Wifi library uses NVS flash to store access point name and password.
    The NVS flash is particularly useful for storing settings, configurations,
    and other small amounts of data that need to survive resets or power loss.
    */
    esp_err_t ret = nvs_flash_init(); 
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_log_level_set("wifi", ESP_LOG_ERROR);
    
    events_group = xEventGroupCreate();

    autoencoder = xQueueCreate(1,sizeof(bool));
    messenger = xQueueCreate(10,sizeof(messages_t));
    skew_queue = xQueueCreate(1,sizeof(MPU_skew_t));
    JSON_msg = xQueueCreate(2,sizeof(JSON_DATA_t));
 
    modbus_read_timer_handle = xTimerCreate("data_timer",pdMS_TO_TICKS(interval),pdTRUE,NULL, data_timer_cb );
    setup();

    xTimerStop(modbus_read_timer_handle,portMAX_DELAY);

    xTaskCreatePinnedToCore(MQTT_sender,"mqtt_sender",6144,NULL,1,NULL,1);
    xTaskCreatePinnedToCore(display,"oled_display",5120,NULL,1,NULL,1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    wifi_connect(WIFI_SSID,WIFI_PASS);
    vTaskDelay(pdMS_TO_TICKS(2000));
    mqtt_connect(MQTT_ID,MQTT_PASSWORD);

    xTaskCreatePinnedToCore(get_data_from_MODBUS_slave,"mode_bus",6144,NULL,1,&get_data_from_MODBUS_slave_handle,1);

    while (1){
      loop();
    }

      
    /*
    if (modbus_read_timer_handle != NULL) {
      xTimerStart(modbus_read_timer_handle, 0);
    }
    */
}
}
