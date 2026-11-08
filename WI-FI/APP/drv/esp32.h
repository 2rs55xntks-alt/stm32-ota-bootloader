#ifndef __ESP32_H
#define __ESP32_H

#ifdef __cplusplus
extern "C" {
#endif

    /* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"      /* huart1 / huart2 的声明 */
#include <stdio.h>
#include <string.h>

    /* 通信口说明：
     *   USART2 (PA2/PA3)   -> 与 PC 调试通信，printf 输出到这里
     *   USART1 (PA9/PA10)  -> 与 ESP32-C3 通信，下面的宏指向 huart1
     */
#define ESP32_UART              huart1

#include "app_config.h"     /* 超时、缓冲大小等所有可调参数都在这里 */

    /* 接收缓冲区：保存一次“发指令—收应答”过程中 ESP32 返回的全部数据 */
    extern uint8_t  esp32_rx_buf[ESP32_RX_BUF_SIZE];
    extern uint16_t esp32_rx_len;

    /* 对外接口 ----------------------------------------------------------------*/

    /**
     * @brief  ESP32 初始化：发送 AT+RST=0 复位模组，延时 500ms 后打印模组返回的数据
     * @retval 1: 收到 OK 复位成功   0: 发送/等待失败
     */
    uint8_t ESP32_Init(void);

    /**
     * @brief  同 ESP32_SendData，但可以自定义等待 "OK" 的超时时间
     * @note   像 AT+CWJAP（连接路由器）这类慢指令需要 10s 以上
     * @param  data           待发送的字符串（需自带 \r\n 结尾）
     * @param  ack_timeout_ms 等待 OK 的超时（ms）
     * @retval 1: 收到 OK   0: 发送失败或超时
     */
    uint8_t ESP32_SendDataTimeout(const char *data, uint32_t ack_timeout_ms);

    /**
     * @brief  接收 ESP32 发来的数据（通过 UART2，查询方式）
     *         逐字节接收并追加到 esp32_rx_buf，直到单字节超时（一帧结束）
     * @param  timeout 接收单个字节的超时时间（ms），一般传 ESP32_RX_BYTE_TIMEOUT
     * @retval 本次调用收到的字节数
     */
    uint16_t ESP32_Receive(uint32_t timeout);

    /**
     * @brief  清空接收缓冲区
     */
    void ESP32_ClearRxBuf(void);

    /**
     * @brief  等待 ESP32 AT 固件启动完成（反复发 "AT" 直到 "OK"）
     * @param  timeout_ms 总等待时间（ms）
     * @retval 1: 已就绪   0: 超时
     */
    uint8_t ESP32_WaitReady(uint32_t timeout_ms);

    /* OneNET OTA */
    uint8_t  ESP32_OTA_Check(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP32_H */
