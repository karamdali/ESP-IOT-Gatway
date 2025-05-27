#ifdef __cplusplus
extern "C" {
#endif

#ifndef _MODBUS_RTU_H
#define _MODBUS_RTU_H

#include <mbcontroller.h>
#include <inttypes.h>


#define samples 128

typedef struct{
      int16_t input_x[samples];
      int16_t input_y[samples];
      int16_t input_z[samples];
      float input_current;
      float input_flow_rate;
      float input_total_flow;
}input_reg_params_t;


//Initialise modbus RTU connection with the following settings:
/*
 * PARITY DISABLE, 
 * HALF DUPLEX,
 * UART_NUM_2, 
 * tx = GPIO_NUM_17
 * rx = GPIO_NUM_16
 * rts = GPIO_NUM_23
 * 
*/ 

esp_err_t modbusRTU_init(uint32_t baudrate);

//The below function from ESP32 modbus serial master example.
void* master_get_param_data(const mb_parameter_descriptor_t* param_descriptor);

void* read_modbus_data(uint16_t cid_);

esp_err_t write_modbus_data(uint16_t cid_, void* data);

float modbus_data_to_float(void *data);   //For registers.

int16_t modbus_data_to_int(void *data);   //For registers.

bool modbus_data_to_bool(void *data);     //For coils.



#endif

#ifdef __cplusplus
}
#endif