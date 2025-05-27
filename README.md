# ESP32 IOT Gatway - Modbus RTU to MQTTS with Anomaly Detection


## Overview

This ESP32 firmware monitors industrial pump systems by:
- Collecting operational data via Modbus RTU
- Analyzing 3-axis vibration data
- Displaying real-time status on OLED
- Publishing JSON payloads to MQTT
- Detecting operational anomalies

## Hardware Requirements

- ESP32 development board
- RS485 transceiver module (MAX485 recommended)
- SSD1306 OLED display (128x64)
- Modbus-enabled pump controller ( https://github.com/karamdali/STM32-Modbus-Pump-Controller )

## Features

### Data Acquisition
- Modbus RTU communication via UART2
- RS485 DE/RE pin control
- Configurable polling interval (default: 5s)
- Reads:
  - Pump ON/OFF status
  - Current draw
  - Flow rate
  - Total flow
  - 3-axis vibration data

### Connectivity
- WiFi station mode
- MQTT client with TLS support
- Change `fullchain.pem` with your server public certificate 
- JSON payload structure:
  ```json
  {
    "pump": "on/off",
    "current": 0.00,
    "flow_rate": 0.00,
    "total_flow": 0.00,
    "status": "normal/anomaly"
  }

## User Interface

### OLED Display System
- **Scrolling text display**  
  Continuously scrolls status messages across the OLED
- **Multi-line message queue**  
  Supports queuing of messages from different system components
- **Visual alert system**  
  - Normal messages: Standard display  
  - Warning/errors: Highlighted display for immediate visibility

## Message Structure Example

```c
typedef struct {
  char text[16];     // Display content
  bool warning;      // Alert flag
} messages_t;
```

## Anomaly Detection

### Vibration Monitoring System

- **3-axis vibration analysis**  
  Continuous monitoring of X/Y/Z skew data via Modbus

- **Machine learning detection**  
  Autoencoder-based anomaly detection model using tensorflow lite

- **Real-time status reporting**  
  Immediate MQTT alerts when anomalies detected

```c
// Vibration data structure
typedef struct {
  float x;          // X-axis skew
  float y;          // Y-axis skew 
  float z;          // Z-axis skew
} MPU_skew_t;
```

## Usage

The system automatically performs these operations:

1. **WiFi Connection**  
   - Establishes secure connection to configured WiFi network
2. **MQTT Setup**  
   - Connects to MQTT broker with TLS encryption
3. **Modbus Operations**  
   - Begins periodic polling of Modbus registers (default: 5s interval)
4. **OLED Display**  
   - Shows real-time status messages and warnings
5. **Data Publishing**  
   - Publishes JSON payloads to MQTT topic `pump/data`:
     ```json
     {
       "pump": "on/off",
       "current": 0.00,
       "flow_rate": 0.00,
       "total_flow": 0.00,
       "status": "normal/anomaly"
     }
     ```
## Configuration

### WiFi Credentials in `connect.c`
```c
#define WIFI_SSID      "your_ssid"       // Your WiFi network name
#define WIFI_PASS      "your_password"   // Your WiFi password
```
### MQTT Credentials `connect.c`
```c
#define MQTT_ID         "client_id"      // MQTT client identifier
#define MQTT_PASSWORD   "client_password"// MQTT authentication password
```
### Add your server public key 
Update `fullchaine.pem` with your server public key.

### Hardware Pins
```c
// RS485 Interface
#define DE_RE_PIN       GPIO_NUM_23      // RS485 Direction control pin
#define TXD_PIN         GPIO_NUM_17      // UART Transmission pin
#define RXD_PIN         GPIO_NUM_16      // UART Receive pin

// I2C OLED Configuration (set in menuconfig)
// CONFIG_SDA_GPIO    GPIO_NUM_21
// CONFIG_SCL_GPIO    GPIO_NUM_22
```
## Troubleshooting

| Symptom               | Solution                                                                 |
|-----------------------|--------------------------------------------------------------------------|
| No OLED display       | Check I2C connections (SDA/SCL) and power to SSD1306                    |
| Modbus timeout        | Verify RS485 wiring (A/B lines), DE/RE pin configuration, and baud rate |
| WiFi fails to connect | Verify SSID/password in `connect.h`, check WiFi signal strength         |
| MQTT disconnect       | Verify broker settings in `connect.h`, server certificate, and keepalive settings       |
| Data not updating     | Check Modbus slave device is responding to register requests            |

## License
This code is provided AS IS without warranty. Feel free to use and modify it for any purpose.

## Author
Karam Dali

Damascus - November 2024
