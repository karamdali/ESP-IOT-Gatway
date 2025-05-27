#ifdef __cplusplus
extern "C" {
#endif

#ifndef _CONNECT_H_
#define _CONNECT_H_
#include <stdbool.h>

void wifi_connect(const char * wifi_ssid,const char * wifi_password);
void mqtt_connect(const char * mqtt_id,const char * mqtt_password);
void send_to_oled(char *text,bool warning);


#endif

#ifdef __cplusplus
}
#endif