/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   全工程唯一配置入口
  *
  *          ★ 所有可调参数都放在这里，各 .c/.h 里不要再写 #define
  *          ★ 改 WiFi 账号、OneNET 地址签名、超时、缓冲大小、分块大小 —— 只改这里
  *
  *          注意：OneNET 的 Authorization 里带签名和时间戳，值改了平台就会拒绝，
  *                改之前先确认平台侧的有效期。
  ******************************************************************************
  */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "secrets.h"

/* ============================ 1. WiFi ============================ */

#define WIFI_CMD_TIMEOUT_MS     3000U    /* 普通 AT 指令（CWMODE/CWDHCP/CIPSTATUS） */
#define WIFI_JOIN_TIMEOUT_MS    15000U   /* CWJAP 连路由器（要 DHCP，给足时间）    */
#define WIFI_CHECK_MS           3000U    /* 查 WiFi 连接状态                        */

/* ======================= 2. OneNET OTA 平台 ======================= */
#define ONENET_OTA_HOST         "iot-api.heclouds.com"
#define ONENET_OTA_PATH         "/fuse-ota/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID
#define ONENET_OTA_URL          "http://" ONENET_OTA_HOST ONENET_OTA_PATH

#define ONENET_OTA_AUTH         "Authorization:" ONENET_OTA_AUTH_VALUE
#define ONENET_OTA_REPORT_AUTH  "Authorization: " ONENET_OTA_AUTH_VALUE

/* 上报状态用的 HTTP 头：OneNET 的 status 接口只认 application/json */
#define ONENET_OTA_REPORT_HEAD \
    "Content-Type: application/json\r\n" ONENET_OTA_REPORT_AUTH

#define ONENET_OTA_TIMEOUT_MS   15000U   /* 查任务 / 下载单包的等待时间 */

/* ===================== 3. ESP32 模组（AT 指令） ===================== */
#define ESP32_AT_RST            "AT+RST=0\r\n"   /* 末尾必须是 \r\n */
#define ESP32_RX_BUF_SIZE       256U             /* AT 应答缓冲    */
#define ESP32_TX_TIMEOUT_MS     1000U            /* HAL 发送超时   */
#define ESP32_ACK_TIMEOUT_MS    2000U            /* 等 "OK" 的总超时 */
#define ESP32_RX_BYTE_TIMEOUT   100U             /* 收单字节超时   */

/* ======================== 4. OTA 下载 / 上报 ======================== */
#define OTA_RX_BUF_SIZE         1024U   /* 下载包(400B) + 上报时完整 HTTP 响应都装得下 */
#define OTA_RX_BYTE_TIMEOUT     1000   /* 收单字节超时 */
#define DL_CHUNK_SIZE           400U    /* 每包请求字节数（Range 跨度），必须是 4 的倍数 */
#define OTA_TCP_TIMEOUT_MS      10000U  /* 上报时 TCP 建连/等响应的超时 */
#define OTA_CIPSEND_TIMEOUT_MS  3000U   /* 等 CIPSEND '>' 提示符        */

/* ============================= 5. 应用 ============================= */
#define LED_BLINK_MS            500U    /* 任务 1：LED 闪烁周期 */

#endif /* APP_CONFIG_H */
