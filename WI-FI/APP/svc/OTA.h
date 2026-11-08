#ifndef WI_FI_OTA_H
#define WI_FI_OTA_H

#include "wifi.h"        /* 项目内头文件用 ""，不要用 <>（Linux/CI 区分大小写） */
#include "esp32.h"
#include "fw_meta.h"      /* fw_meta_t：check 的输出 / download 的输入 */
#include "app_config.h"   /* OneNET 地址/签名、缓冲大小、超时全在这里 */

/* 平台地址、Authorization 签名、缓冲大小、超时等
   全部集中在 app_config.h，这里不再重复定义 */

/* 接收缓冲区：一次“发指令 — 收应答”过程中 ESP32 返回的全部数据 */
extern uint8_t  ota_rx_buf[OTA_RX_BUF_SIZE];
extern uint32_t ota_rx_len;

uint16_t OTA_Receive(uint32_t timeout);
uint8_t OTA_SendDataTimeout(const char *data, uint32_t ack_timeout_ms);

/* 查平台：只解析出 task，不写 Flash */
uint8_t OTA_CheckUpdate(fw_meta_t *task);

/* 下载：OTA_Load 发一包，OTA_Download 擦备份区 + 循环收 + 写 Flash */
uint8_t OTA_Load(uint32_t tid, uint32_t offset, uint32_t chunk);
uint8_t OTA_BackupErase(uint32_t size);
uint8_t OTA_BackupWrite(uint32_t offset, const uint8_t *data, uint32_t len);
uint8_t OTA_Download(const fw_meta_t *task);

/* 向 OneNET 上报升级进度：AT+HTTPCHEAD -> 头 -> AT+HTTPCPOST -> {"step":N} */
uint8_t OTA_Report(uint32_t tid, uint8_t step);

#endif //WI_FI_OTA_H