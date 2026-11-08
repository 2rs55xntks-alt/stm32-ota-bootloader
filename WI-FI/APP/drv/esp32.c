#include "esp32.h"
#include <ctype.h>

/* ==========================================================================
 * 私有变量
 * ========================================================================== */

/* 接收缓冲区：一次“发指令 — 收应答”过程中 ESP32 返回的全部数据 */
uint8_t  esp32_rx_buf[ESP32_RX_BUF_SIZE];
uint16_t esp32_rx_len = 0;

/* ==========================================================================
 * 对外函数
 * ========================================================================== */

/**
 * @brief  清空接收缓冲区
 */
void ESP32_ClearRxBuf(void)
{
  esp32_rx_len = 0;
  esp32_rx_buf[0] = 0;
}

/**
 * @brief  接收 ESP32 发来的数据（UART2，查询方式）
 * @note   逐字节接收并追加到 esp32_rx_buf；某一字节超时未到即认为一帧结束。
 *         每次只“追加”，不清空，所以可以在 do-while 中反复调用来拼接完整应答。
 * @param  timeout 接收单个字节的超时时间（ms）
 * @retval 本次调用收到的字节数
 */
uint16_t ESP32_Receive(uint32_t timeout)
{
  uint8_t  ch;
  uint8_t  got = 0;

  while (esp32_rx_len < (ESP32_RX_BUF_SIZE - 1U))
  {
    /* 调用 HAL 库 UART2 的接收函数：收到 1 字节或超时返回 */
    if (HAL_UART_Receive(&ESP32_UART, &ch, 1U, timeout) != HAL_OK)
    {
      break;
    }

    /* 丢掉复位瞬间 TX 拉低产生的假 0x00，否则 strstr 从这个 0 开始就认为字符串结束了 */
    if (ch != 0x00U)
    {
      esp32_rx_buf[esp32_rx_len] = ch;
      esp32_rx_len++;
      got++;
    }
  }

  esp32_rx_buf[esp32_rx_len] = '\0';   /* strstr 必须有结束符 */

  return got;

}

/**
 * @brief  STM32 向 ESP32 发送数据（UART2），并等待 ESP32 返回 "OK"
 * @note   流程：发送 -> do-while 循环调用接收函数 -> 收到 OK 立即返回；
 *         超时则强制退出并 printf 打印“发送错误”。
 * @param  data           待发送的字符串（需自带 \r\n 结尾）
 * @param  ack_timeout_ms 等待 OK 的超时（ms）
 * @retval 1: 收到 OK   0: 发送失败或等待 OK 超时
 */
uint8_t ESP32_SendDataTimeout(const char *data, uint32_t ack_timeout_ms)
{
  uint16_t len;
  uint32_t tick_start;

  if (data == NULL)
  {
    return 0;
  }

  len = (uint16_t)strlen(data);
  ESP32_ClearRxBuf();                       /* 每次发指令前清空，便于保存本次应答 */

  /* 1. 调用 HAL 库 UART2 发送函数 */
  if (HAL_UART_Transmit(&ESP32_UART, (uint8_t *)data, len, ESP32_TX_TIMEOUT_MS) != HAL_OK)
  {
    printf("TX FAIL\r\n");
    return 0;
  }

  /* 2. do-while 循环接收，只有收到 "OK" 才退出 */
  tick_start = HAL_GetTick();

  do
  {
    ESP32_Receive(ESP32_RX_BYTE_TIMEOUT);

    if (strstr((char *)esp32_rx_buf, "OK") != NULL)
    {
      return 1;                             /* 收到 OK，正常退出 */
    }

    /* 模组已经明确回 ERROR 就不必再等到超时了 */
    if (strstr((char *)esp32_rx_buf, "ERROR") != NULL)
    {
      return 0;
    }

  } while ((HAL_GetTick() - tick_start) < ack_timeout_ms);   /* 超时强制退出 */

  /* 3. 超时仍未收到 OK */
  printf("AT TIMEOUT\r\n");

  return 0;
}

/**
 * @brief  ESP32 初始化：发送 AT+RST=0 复位模组
 * @note   发送（内部含等待 OK）-> 延时 500ms 等模组复位 -> 打印模组返回的数据
 * @retval 1: 复位指令收到 OK   0: 发送失败或超时
 */
uint8_t ESP32_Init(void)
{
  uint8_t ret;

  printf("\r\nESP32 init\r\n");

  /* 调用发送函数发送 AT+RST=0\r\n（发送函数内部会调用接收函数等 OK） */
  ret = ESP32_SendDataTimeout(ESP32_AT_RST, ESP32_ACK_TIMEOUT_MS);

  /* 延时 500ms，等待 ESP32 完成复位 */
  HAL_Delay(500U);

  /* 注意：ATE0 必须在 AT+RST 之后发！复位会把回显重新打开，
     放在复位前面等于没关，之后所有 AT 应答里都会混着发出去的指令 */
  (void)ESP32_SendDataTimeout("ATE0\r\n", 2000U);

  /* 清掉自定义 HTTP 头。AT+HTTPCHEAD 设的头会一直带到后续所有请求
     （连 AT+RST 都清不掉，模组存在 NVS 里），必须在第一条 HTTP 请求前清，
     否则 check 的 GET 也会带上 Content-Type: application/json，
     nginx 直接回 400 Bad Request */
  (void)ESP32_SendDataTimeout("AT+HTTPCHEAD=0\r\n", 2000U);

  return ret;
}

