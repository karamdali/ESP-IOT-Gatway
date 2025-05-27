# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS C:/Users/ASUS/MQTTclient/components/ssd1306)

set(EXTRA_COMPONENT_DIRS C:/Users/ASUS/MQTTclient/managed_components/espressif__esp-modbus)

set(EXTRA_COMPONENT_DIRS C:/Users/ASUS/MQTTclient/managed_components/espressif__esp-nn)

set(EXTRA_COMPONENT_DIRS C:/Users/ASUS/MQTTclient/managed_components/espressif__esp-tflite-micro)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(MQTTclient)
target_add_binary_data(MQTTclient.elf "main/fullchain.pem" TEXT)
