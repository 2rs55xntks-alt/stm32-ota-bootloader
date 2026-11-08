#include "wifi.h"
#include "esp32.h"
#include "app_config.h"     /* WiFi 账号 / 超时都在这里 */
#include <stdbool.h>

/* 置 1：把 TCP 每次收到的原始帧打印出来（排查收不到数据时非常有用） */
#define WIFI_RX_DUMP            0

static void STA_MODE(void);
static void AP_MODE(void);

/**
 * @brief 发一条 AT 指令并打印结果（详细应答由 ESP32_SendDataTimeout 打印）
 * @note  cmd 里的双引号必须用 ASCII 的 \" ，不能用中文全角引号
 */
static uint8_t WIFI_AT(const char *cmd, uint32_t timeout_ms)
{
    uint8_t ok = ESP32_SendDataTimeout(cmd, timeout_ms);

    printf("wifi %s\r\n", (ok != 0U) ? "ok" : "fail");

    return ok;
}

/* 原来这里有个 static 的 WIFI_Find()，只有被注释掉的 WIFI_CheckConnected 在用，
   现在已经没人调用（task.c 用自己的 strstr 检查），删掉以消除 unused-function 告警 */

/**
 * @brief  检查 ESP32 是否已连上局域网（即是否从路由器拿到了 IP）
 * @note   用 AT+CIPSTATUS 判断：STATUS:2 = 已获取 IP（连接成功）
 *                                        3 = 正在连接
 *                                        4 = 连接断开
 *                                        5 = 未连上 AP
 * @retval 1: 已连上并拿到 IP   0: 未连上
 */
// uint8_t WIFI_CheckConnected(void)
// {
//     const char *p;
//
//     printf("[WIFI] 查询连接状态...\r\n");
//
//     if (ESP32_SendDataTimeout("AT+CIPSTATUS\r\n", WIFI_CMD_TIMEOUT_MS) == 0U)
//     {
//         printf("[WIFI] 状态查询失败（模组无应答）\r\n");
//         return 0;
//     }
//
//     p = WIFI_Find("STATUS:");
//
//     if (p == NULL)
//     {
//         printf("[WIFI] 没解析到 STATUS 字段\r\n");
//         return 0;
//     }
//
//     printf("[WIFI] 模组状态 STATUS:%c\r\n", p[7]);
//
//     if (p[7] != '2')
//     {
//         printf("[WIFI] 未连上局域网\r\n");
//         return 0;
//     }
//
//     printf("[WIFI] 已连上局域网，查询 IP：\r\n");
//
//     if (ESP32_SendDataTimeout("AT+CIPSTA?\r\n", WIFI_CMD_TIMEOUT_MS) != 0U)
//     {
//         printf("%s\r\n", (char *)esp32_rx_buf);   /* 打印 +CIPSTA:ip:"x.x.x.x" 等 */
//     }
//
//     return 1;
// }

void WIFI_Init(WIFI_MODE mode)
{
    ESP32_Init();              /* 复位模组并等待 OK */
    HAL_Delay(1000U);

    if (mode == STA)
    {
        STA_MODE();
    }
    else
    {
        AP_MODE();
    }
}

static void STA_MODE(void)
{
    printf("STA\r\n");
    WIFI_AT("AT+CWMODE=1\r\n", WIFI_CMD_TIMEOUT_MS);

    /* 开启 Station DHCP：按 ESP-AT 文档，DHCP 与静态 IP 互斥，
       开启 DHCP 会自动取消之前 AT+CIPSTA 设的静态 IP，
       否则会一直沿用 192.168.8.1（和网关撞车，网络不通） */
    WIFI_AT("AT+CWDHCP=1,1\r\n", WIFI_CMD_TIMEOUT_MS);

    printf("join AP\r\n");
    WIFI_AT("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASSWORD "\"\r\n", WIFI_JOIN_TIMEOUT_MS);

    WIFI_AT("AT+CIPSTA?\r\n", WIFI_CMD_TIMEOUT_MS);
    printf("%s\r\n", (char *)esp32_rx_buf);
}

static void AP_MODE(void)
{
    printf("AP\r\n");
    WIFI_AT("AT+CWMODE=2\r\n", WIFI_CMD_TIMEOUT_MS);

    /* AP 模式下配置热点用 CWSAP：SSID, 密码, 信道, 加密方式
       （0=OPEN 1=WEP 2=WPA_PSK 3=WPA2_PSK 4=WPA_WPA2_PSK）
       CWJAP 是"连接别的路由器"，AP 模式下发它必然返回 ERROR */
    WIFI_AT("AT+CWSAP=\"ESP32\",\"12345678\",5,3\r\n", WIFI_CMD_TIMEOUT_MS);

    /* 热点自身的 IP 用 CIPAP 设置（CIPSTA 设置的是 STA 模式的 IP） */
    WIFI_AT("AT+CIPAP=\"192.168.4.1\"\r\n", WIFI_CMD_TIMEOUT_MS);
    printf("%s\r\n", (char *)esp32_rx_buf);

}

// void WIFI_TCP_Start(void) {
//
//     WIFI_AT("AT+CIPMUX=1\r\n", WIFI_CMD_TIMEOUT_MS);
//     WIFI_AT("AT+CIPSERVER=1,8080\r\n", WIFI_CMD_TIMEOUT_MS);
//     WIFI_AT("AT+CIPDINFO=1\r\n", WIFI_CMD_TIMEOUT_MS);
// }

// void WIFI_TCP_Send(uint8_t id, uint8_t *data, uint16_t len) {
//
//     uint8_t sendCmd[50] = {0};
//
//     /* len=0 时模组会直接回 ERROR，白白把 AT 应答弄乱 */
//     if ((data == NULL) || (len == 0U))
//     {
//         return;
//     }
//
//     sprintf((char*)sendCmd, "AT+CIPSEND=%d,%d\r\n", (int)id, (int)len);
//
//     /* 只有 CIPSEND 被模组接受（回 OK 并给出 ">"）后，发出去的数据才算数 */
//     WIFI_AT(sendCmd, WIFI_CMD_TIMEOUT_MS);
//
//     HAL_UART_Transmit(&ESP32_UART,data,len,1000);
// }

// uint8_t tempBuff[1024] = {0};
// uint16_t tempBuffLen = 0;
// uint8_t tempid[20] = {0};
/**
 * @brief  从 ESP32 上报的 "+IPD" 帧里取出真正的 TCP 负载
 * @note   帧格式（AT+CIPDINFO=1）：
 *           +IPD,<link_id>,<len>,"<remote_ip>",<remote_port>:<data>
 *         两个必须自己处理的点：
 *           1) HAL_UARTEx_ReceiveToIdle 不会补 '\0'，也不清空缓冲区；
 *           2) 帧前面不一定有 \r\n，不能靠 sscanf 去"跳过"它。
 */

// void WIFI_TCP_Receive(uint8_t rxbuff[],
//                       uint16_t *rxlen,
//                       uint8_t *id,
//                       uint8_t *ip,
//                       uint16_t *port)
// {
//     // 注意：tempBuff 和 tempBuffLen 应是已定义的全局变量或外部变量
//     memset(tempBuff, 0, sizeof(tempBuff));
//     tempBuffLen = 0;
//
//     if (HAL_UARTEx_ReceiveToIdle(&ESP32_UART,
//                                  tempBuff,
//                                  (uint16_t)(sizeof(tempBuff) - 1U),
//                                  &tempBuffLen,
//                                  1000) != HAL_OK)
//     {
//         return;
//     }
//
//     // 确保字符串结束，但后续仍需注意实际接收长度
//     tempBuff[tempBuffLen] = '\0';
//
//     char *pHead = strstr((char *)tempBuff, "+IPD,");
//     if (pHead == NULL)
//         return;
//
//     pHead += 5;  // 连接 ID 起始位置
//
//     // 1. 解析连接 ID
//     char *pId = strchr(pHead, ',');
//     if (pId == NULL)
//         return;
//
//     size_t idLen = (size_t)(pId - pHead);
//     if (idLen == 0 || idLen >= 8)
//         return;
//
//     memcpy(id, pHead, idLen);
//     id[idLen] = '\0';
//
//     // 2. 解析数据长度
//     char *pRxlen = pId + 1;
//     char *pIpStart = strchr(pRxlen, ',');
//     if (pIpStart == NULL)
//         return;
//
//     size_t lenLen = (size_t)(pIpStart - pRxlen);
//     if (lenLen == 0 || lenLen >= 6)
//         return;
//
//     char lenStr[6];
//     memcpy(lenStr, pRxlen, lenLen);
//     lenStr[lenLen] = '\0';
//
//     unsigned long dataLen = strtoul(lenStr, NULL, 10);
//     if (dataLen > UINT16_MAX)
//         return;
//
//     // 3. 解析 IP：跳过逗号和双引号
//     pIpStart++;  // 跳过长度后面的逗号
//
//     if (*pIpStart != '"')
//         return;
//
//     pIpStart++;  // 指向 IP 第一个字符
//
//     char *pIpEnd = strchr(pIpStart, '"');
//     if (pIpEnd == NULL)
//         return;
//
//     size_t ipLen = (size_t)(pIpEnd - pIpStart);
//     if (ipLen == 0 || ipLen >= 16)
//         return;
//
//     memcpy(ip, pIpStart, ipLen);
//     ip[ipLen] = '\0';
//
//     // 4. 解析端口
//     char *pPortStart = pIpEnd + 1;
//
//     if (*pPortStart != ',')
//         return;
//
//     pPortStart++;
//
//     char *pColon = strchr(pPortStart, ':');
//     if (pColon == NULL)
//         return;
//
//     size_t portLen = (size_t)(pColon - pPortStart);
//     if (portLen == 0 || portLen >= 6)
//         return;
//
//     char portStr[6];
//     memcpy(portStr, pPortStart, portLen);
//     portStr[portLen] = '\0';
//
//     unsigned long portValue = strtoul(portStr, NULL, 10);
//     if (portValue > 65535)
//         return;
//
//     *port = (uint16_t)portValue;
//
//     // 5. 解析实际数据
//     char *pData = pColon + 1;
//
//     // 检查接收缓冲区中实际数据是否足够
//     size_t remainLen = (size_t)((char *)tempBuff + tempBuffLen - pData);
//     if (remainLen < dataLen || dataLen > sizeof(tempBuff) - 1U)
//         return;
//
//     memcpy(rxbuff, pData, dataLen);
//     *rxlen = (uint16_t)dataLen;
// }


