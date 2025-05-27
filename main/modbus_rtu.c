

#ifndef _MODBUS_RTU_H
#define _MODBUS_RTU_H

#include "modbus_rtu.h"
#include <mbcontroller.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "connect.h"


#define samples 128
#define TAG "MODBUS RTU"

#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) + 1))
#define COIL_OFFSET(field) ((uint16_t)(offsetof(coil_reg_params_t, field) + 1))
#define INPUT_REG_SIZE(field) (sizeof(((input_reg_params_t *)0)->field) >> 1)
#define STR(fieldname) ((const char*)( fieldname ))
#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }


//For displaying error messages on SSD1306.
extern QueueHandle_t messenger;

typedef struct {
  char text[16];
  bool warning;
}messages;

enum{
    MB_SLAVE_ADD1 = 1,              //We have one slave.
    MB_SLAVE_COUNT
};

enum{
    CID_INPUT_X_SKEW = 0,                   //Floating point.
    CID_INPUT_Y_SKEW,                       //Floating point.      
    CID_INPUT_Z_SKEW,                       //Floating point.
    CID_INPUT_CURRENT_DATA,                 //Floating point.
    CID_INPUT_FLOW_RATE_DATA,               //Floating point.
    CID_INPUT_TOTAL_FLOW_DATA,              //Floating point.
    CID_COIL_PUMP                           //Pump ON or OFF.
};

typedef struct{
      float input_x;
      float input_y;
      float input_z;
      float input_current;
      float input_flow_rate;
      float input_total_flow;
}input_reg_params_t;

typedef struct{
    uint8_t coils_pump;
}coil_reg_params_t;


coil_reg_params_t coil_reg_params= {0};
input_reg_params_t input_reg_params = {0};

const mb_parameter_descriptor_t device_parameters[] = {
    //X-axis input data (128 samples).
    {
        CID_INPUT_X_SKEW,                //CID: Unique identifier for this parameter.
        STR("X_Axis_skew"),              //Param Name: Descriptive name of the parameter.
        STR("g"),                        //Units: Data unit for the parameter.
        MB_SLAVE_ADD1,                   //Modbus Slave Addr: Modbus slave address.
        MB_PARAM_INPUT,                  //Modbus Reg Type: Register type (input register).
        0,                               //Reg Start: Start register address (customize as needed).
        2,                               //Reg Size: Number of registers each is 16 bits.
        INPUT_OFFSET(input_x),          //Instance Offset: Offset within input_reg_params_t structure.                                 
        PARAM_TYPE_FLOAT,                //Data Type: float.
        sizeof(float),                   //Data Size: Size of data type (4 bytes for float).
        OPTS( 0, 1.0, 1 ),               //Parameter Options: Min, max, and step values. OPTS(0, 1.0, 0.001)
        PAR_PERMS_READ                   //Access Mode: Read-only.
    },
    //Y-axis input data.
    {
        CID_INPUT_Y_SKEW,
        STR("Y_Axis_skew"),
        STR("g"),
        MB_SLAVE_ADD1,
        MB_PARAM_INPUT,
        2,                      
        2,
        INPUT_OFFSET(input_y),
        PARAM_TYPE_FLOAT,
        sizeof(float),
        OPTS( 0.0, 1.0, 0.001 ),
        PAR_PERMS_READ
    },
    //Z-axis input data.
    {
        CID_INPUT_Z_SKEW,
        STR("Z_Axis_skew"),
        STR("g"),
        MB_SLAVE_ADD1,
        MB_PARAM_INPUT,
        4,                      
        2,
        INPUT_OFFSET(input_z),
        PARAM_TYPE_FLOAT,
        sizeof(float),
        OPTS( 0.0, 1.0, 0.001),
        PAR_PERMS_READ
    },
    //Current input data (floating point).
    {
        CID_INPUT_CURRENT_DATA,
        STR("Current_Data"),
        STR("A"),
        MB_SLAVE_ADD1,
        MB_PARAM_INPUT,
        6,                      
        2,
        INPUT_OFFSET(input_current),
        PARAM_TYPE_FLOAT,
        sizeof(float),
        OPTS(-5.00, 5.00, 0.01),
        PAR_PERMS_READ
    },
    //Flow rate input data (floating point).
    {
        CID_INPUT_FLOW_RATE_DATA,
        STR("Flow_Rate"),
        STR("L/sec"),
        MB_SLAVE_ADD1,
        MB_PARAM_INPUT,
        8,                      
        2,
        INPUT_OFFSET(input_flow_rate),
        PARAM_TYPE_FLOAT,
        sizeof(float),
        OPTS(0.00, 6.00, 0.01),
        PAR_PERMS_READ
    },
    //Total flow input data (floating point).
    {
        CID_INPUT_TOTAL_FLOW_DATA,
        STR("Total_Flow_Data"),
        STR("Lit"),
        MB_SLAVE_ADD1,
        MB_PARAM_INPUT,
        10,                      
        2,
        INPUT_OFFSET(input_total_flow),
        PARAM_TYPE_FLOAT,
        sizeof(float),
        OPTS(0.00, 1000.00, 0.01),
        PAR_PERMS_READ
    },
    {
        CID_COIL_PUMP,               
        STR("Pump"),              
        STR("ON/OFF"),                        
        MB_SLAVE_ADD1,                   
        MB_PARAM_COIL,                  
        0,                               
        1,                         
        COIL_OFFSET(coils_pump),           
        PARAM_TYPE_U8,               
        1,                 
        OPTS(0x00, 0x01, 1),          
        PAR_PERMS_READ_WRITE_TRIGGER                    
    }
};

const uint16_t num_device_parameters = (sizeof(device_parameters)/sizeof(device_parameters[0]));

void* master_get_param_data(const mb_parameter_descriptor_t* param_descriptor)
{
    assert(param_descriptor != NULL);
    void* instance_ptr = NULL;
    if (param_descriptor->param_offset != 0) {
       switch(param_descriptor->mb_param_type)
       {
           case MB_PARAM_INPUT:
               instance_ptr = ((void*)&input_reg_params + param_descriptor->param_offset - 1);
               break;
            case MB_PARAM_COIL:
               instance_ptr = ((void*)&coil_reg_params + param_descriptor->param_offset - 1);
               break;
           default:
               instance_ptr = NULL;
               break;
       }
    } else {
        ESP_LOGE(TAG, "Wrong parameter offset for CID #%u", (unsigned)param_descriptor->cid);
        assert(instance_ptr != NULL);
    }
    return instance_ptr;
}

esp_err_t modbusRTU_init(uint32_t baudrate){
    mb_communication_info_t comm = {
            .port = UART_NUM_2,
            .mode = MB_MODE_RTU,
            .baudrate = baudrate,
            .parity = UART_PARITY_DISABLE
    };
    void* master_handler = NULL;
    esp_err_t err;
    err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    if(err != ESP_OK){
        send_to_oled("MB init err",true);
        return err;
    }
    send_to_oled("MB init succ",false);
    err = mbc_master_setup((void*)&comm);
    if(err != ESP_OK){
        send_to_oled("MB setup err",true);
        return err;
    }
    send_to_oled("MB setup succ",false);
    err = uart_set_pin(UART_NUM_2, GPIO_NUM_17, GPIO_NUM_16, GPIO_NUM_23, UART_PIN_NO_CHANGE);
    if(err != ESP_OK){
        send_to_oled("UART set err",true);
        return err;
    }
    send_to_oled("UART set succ",false);
    err = mbc_master_start();
    if(err != ESP_OK){
        send_to_oled("MB start err",true);
        return err;
    }
    send_to_oled("MB start succ",false);
    err = uart_set_mode(UART_NUM_2, UART_MODE_RS485_HALF_DUPLEX);
    if(err != ESP_OK){
        send_to_oled("UART mode err",true);
        return err;
    }
    send_to_oled("UART mode succ",false);
    vTaskDelay(10);
    err = mbc_master_set_descriptor(&device_parameters[0], num_device_parameters);
    if(err != ESP_OK){
        send_to_oled("MB dscptr err",true);
        return err;
    }
    send_to_oled("MB dscptr succ",false);
    return err;
}

void* read_modbus_data(uint16_t cid_) {
    esp_err_t err;
    mb_parameter_descriptor_t* param_descriptor = NULL;
    uint8_t type = 0;
    void* temp_data_ptr = NULL;
    err = mbc_master_get_cid_info(cid_, &param_descriptor);
    if (err != ESP_OK || param_descriptor == NULL) {
        printf("Failed to get CID info, error: %s\n", esp_err_to_name(err));
        return NULL; 
    }
    temp_data_ptr = master_get_param_data(param_descriptor);
    if (temp_data_ptr == NULL) {
        printf("Failed to get parameter data\n");
        return NULL;
    }
    err = mbc_master_get_parameter(cid_, (char*)param_descriptor->param_key, (uint8_t*)temp_data_ptr, &type);
    if (err != ESP_OK) {
        printf("Failed to get parameter, error: %s\n", esp_err_to_name(err));
        return NULL; 
    }
    return temp_data_ptr;
}

esp_err_t write_modbus_data(uint16_t cid_, void * data){
    esp_err_t err;
    mb_parameter_descriptor_t* param_descriptor = NULL;
    uint8_t type = 0;
    void * temp_data_ptr = data;
    
    err = mbc_master_get_cid_info(cid_, &param_descriptor);
    if (err != ESP_OK || param_descriptor == NULL) {
        printf("Failed to get CID info, error: %s\n", esp_err_to_name(err));
        return err; 
    }
    err = mbc_master_set_parameter(cid_,(char *)param_descriptor->param_key,(uint8_t *)temp_data_ptr,&type);
    if (err != ESP_OK) {
        printf("Failed to set parameter data\n");
        return err;
    }
    return ESP_OK;
}

float modbus_data_to_float(void *data){
    return *(float *) data;
}

int16_t modbus_data_to_int(void *data){
    return *(int16_t *) data;
}

bool modbus_data_to_bool(void *data){
    bool value = !!(*(uint8_t * ) data);
    return value;
}




#endif