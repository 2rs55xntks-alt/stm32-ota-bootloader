//
// Created by g3501 on 2026/10/26.
//

#ifndef WI_FI_WIFI_H
#define WI_FI_WIFI_H

#include "esp32.h"
#include <stdint.h>


typedef enum{

    STA = 1,
    AP = 2
}WIFI_MODE;

#define CLIENT_IP_LEN  16
#define PAYLOAD_MAX    256


void WIFI_Init(WIFI_MODE mode);

/* 注意：WIFI_CheckConnected() 的实现已在 wifi.c 里被注释掉，
   这里不再保留声明，避免以后调用时出现链接错误（task.c 里有等价的检查） */

void WIFI_TCP_Start(void);
void WIFI_TCP_Send(uint8_t id, uint8_t *data, uint16_t len) ;
void WIFI_TCP_Receive(uint8_t rxbuff[], uint16_t *rxlen, uint8_t *id, uint8_t *ip, uint16_t *port);

#endif //WI_FI_WIFI_H