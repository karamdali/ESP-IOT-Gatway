/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/


#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "main_functions.h"
#include "model.h"
#include "constants.h"
//#include "output_handler.h"
#include "freertos/FreeRTOS.h"


#define AXIS  3

extern QueueHandle_t skew_queue;
extern QueueHandle_t autoencoder;

typedef struct{
  float x;
  float y;
  float z;
}MPU_skew_t;

MPU_skew_t skew;

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;


constexpr int kTensorArenaSize = 2000;
uint8_t tensor_arena[kTensorArenaSize];
}  // namespace

// The name of this function is important for Arduino compatibility.
void setup() {
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  static tflite::MicroMutableOpResolver<2> resolver;
  if (resolver.AddFullyConnected() != kTfLiteOk) {
    return;
  }

  if (resolver.AddLogistic() != kTfLiteOk) {
    return;
  }

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Obtain pointers to the model's input and output tensors.
  input = interpreter->input(0);
  output = interpreter->output(0);
}

int i = 0;
// The name of this function is important for Arduino compatibility.
void loop() {
  float input_data[AXIS];
  float output_data[AXIS];
  bool result;
  if(xQueueReceive(skew_queue,&skew,portMAX_DELAY) == pdPASS){
    //Copy Normalized data to the input buffer/tensor
    input_data[0] = skew.x;
    input_data[1] = skew.y;
    input_data[2] = skew.z;

    for (int axis = 0; axis < AXIS; axis++) {
      input->data.f[axis] = input_data[axis];
    }
    
    printf("\ninput 1 : %f",input_data[0]);
    printf("input 2 : %f",input_data[1]);
    printf("input 3 : %f\n",input_data[2]);

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
     MicroPrintf("Invoke failed");
      return;
    }
    
    // Read predicted y value from output buffer (tensor)
    for (int axis = 0; axis < AXIS; axis++) {
      output_data[axis] = output->data.f[axis];
    }
    
    printf("\noutput 1 : %f",output_data[0]);
    printf("output 2 : %f",output_data[1]);
    printf("output 3 : %f\n",output_data[2]);
    //compute Mean Absolute Error (MAE)
    float mae =0.0;
    for (int i = 0; i < AXIS; i++) {
      mae += fabs(input_data[i] - output_data[i]);
    }
    mae = mae/AXIS;

    printf("MAE of the resulted data : %f",mae);

    if (mae > threshold) {
      result = true;
      
    }
      /* 
    if ((i==4) || (i==6)){
      result = false;
    }
      */
    xQueueSend(autoencoder,(void *)&result,portMAX_DELAY);
    i++;
  }
}
