#include <stdlib.h>          /* strtoul，解析 +HTTPCLIENT:<n>, 里的长度 */

#include "OTA.h"
#include "fw_meta.h"

uint8_t  ota_rx_buf[OTA_RX_BUF_SIZE];   // 大小用 OTA.h 里的宏
uint32_t ota_rx_len = 0;

/**
 * @brief  清空接收缓冲区
 */
void OTA_ClearRxBuf(void)
{
    ota_rx_len = 0;
    ota_rx_buf[0] = 0;
}

uint16_t OTA_Receive(uint32_t timeout)
{
    uint8_t  ch;
    uint8_t  got = 0;

    while (ota_rx_len < (OTA_RX_BUF_SIZE - 1U))
    {
        if (HAL_UART_Receive(&ESP32_UART, &ch, 1U, timeout) != HAL_OK)
        {
            break;
        }

        ota_rx_buf[ota_rx_len] = ch;   /* 用 ota_rx_len，不是 esp32_rx_len */
        ota_rx_len++;
        got++;
    }

    ota_rx_buf[ota_rx_len] = '\0';     /* 补结束符 */

    return got;
}

uint8_t OTA_SendDataTimeout(const char *data, uint32_t ack_timeout_ms)
{
    uint16_t len;
    uint32_t tick_start;

    if (data == NULL)
    {
        return 0;
    }

    len = (uint16_t)strlen(data);
    OTA_ClearRxBuf();                       /* 每次发指令前清空，便于保存本次应答 */

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
        OTA_Receive(OTA_RX_BYTE_TIMEOUT);

        if (strstr((char *)ota_rx_buf, "OK") != NULL)
        {
            return 1;
        }

        /* 模组已经明确回 ERROR 就不必再等到超时了 */
        if (strstr((char *)ota_rx_buf, "ERROR") != NULL)
        {
            return 0;
        }

    } while ((HAL_GetTick() - tick_start) < ack_timeout_ms);   /* 超时强制退出 */

    /* 3. 超时仍未收到 OK */
    printf("AT TIMEOUT\r\n");

    return 0;
}

/**
 * @brief 向 OneNET 查询是否有新任务，只解析、不写 Flash
 * @param task 输出：解析到的 target / tid / size / md5
 * @retval 1: 有新任务且解析成功  0: 没有任务 / 解析失败
 * @note  Metadata 的写入被移到“下载成功之后”（见 task.c 里的 ota_task），
 *        这样下载失败时记录不会被污染，下次上电还能重试
 */
uint8_t OTA_CheckUpdate(fw_meta_t *task)
{
    static const char command[] =
        "AT+HTTPCLIENT=2,0,\"" ONENET_OTA_URL "/check?type=1&version=1.0\","
        "\"\",\"\",1,\"" ONENET_OTA_AUTH "\"\r\n";

    if (task == NULL)
    {
        return 0;
    }

    if (OTA_SendDataTimeout(command, ONENET_OTA_TIMEOUT_MS) == 0U)
    {
        return 0;
    }

    if (FwMeta_ParseJson((const char *)ota_rx_buf, task) == false)
    {
        printf("no json\r\n");
        return 0;
    }

    printf("task %s %lu %lu\r\n",
           task->target, (unsigned long)task->tid, (unsigned long)task->size);

    return 1;
}

/* ===================== AT+HTTPCLIENT 应答解析 ===================== */

typedef enum
{
    OTA_HTTP_UNKNOWN = 0,   /* 数据还没齐，或尾巴还没到 */
    OTA_HTTP_OK,            /* 请求成功，数据有效         */
    OTA_HTTP_ERR,           /* 模组回 ERROR               */
    OTA_HTTP_CLOSED         /* CLOSED / SEND FAIL         */
} ota_http_st_t;

/**
 * @brief  解析 ota_rx_buf 里的 AT+HTTPCLIENT 应答
 * @param  data 输出：数据首地址（失败时为 NULL）
 * @param  len  输出：数据字节数（失败时为 0）
 * @retval 接收状态；OTA_HTTP_OK 时 data/len 有效
 * @note   格式： +HTTPCLIENT:<n>,<n 字节二进制数据>\r\n\r\nOK\r\n
 *           头长度 = 12 + n 的位数 + 1（100→16，92→15，4592→17），不能写死
 *           数据里满是 0x00，所以只在"数据之后"那段纯文本尾巴上用 strstr
 */
ota_http_st_t OTA_GetHttpResult(const uint8_t **data, uint32_t *len)
{
    const char *h;
    const char *tail;
    char       *comma = NULL;
    uint32_t    n;
    uint32_t    off;

    if ((data == NULL) || (len == NULL))
    {
        return OTA_HTTP_UNKNOWN;
    }

    *data = NULL;
    *len  = 0U;

    h = strstr((char *)ota_rx_buf, "+HTTPCLIENT:");

    if (h == NULL)                       /* 没有数据头：可能直接就是失败应答 */
    {
        if (strstr((char *)ota_rx_buf, "ERROR") != NULL)   { return OTA_HTTP_ERR; }
        if (strstr((char *)ota_rx_buf, "CLOSED") != NULL)  { return OTA_HTTP_CLOSED; }
        return OTA_HTTP_UNKNOWN;
    }

    n = (uint32_t)strtoul(h + 12, &comma, 10);          /* 读 n */

    if ((comma == (h + 12)) || (*comma != ','))
    {
        return OTA_HTTP_ERR;                            /* 头格式不对 */
    }

    *data = (const uint8_t *)(comma + 1);               /* 逗号后面就是数据 */
    off   = (uint32_t)(*data - ota_rx_buf);             /* 协议头长度 */

    if (ota_rx_len < (off + n))
    {
        return OTA_HTTP_UNKNOWN;                        /* 数据还没收齐 */
    }

    *len  = n;
    tail  = (const char *)(*data + n);                  /* 尾巴是纯文本 */

    if (strstr(tail, "ERROR")     != NULL) { return OTA_HTTP_ERR; }
    if (strstr(tail, "CLOSED")    != NULL) { return OTA_HTTP_CLOSED; }
    if (strstr(tail, "SEND FAIL") != NULL) { return OTA_HTTP_CLOSED; }
    if (strstr(tail, "OK")        != NULL) { return OTA_HTTP_OK; }

    return OTA_HTTP_UNKNOWN;                            /* 数据齐了，尾巴还没到 */
}

/* ===================== BACKUP 区（0x0800A000）写入 ===================== */

/* 备份区地址统一来自 fw_meta.h（FW_BACKUP_*），这里只做别名，不再单独写死 */
#define BACKUP_ADDR        (FW_BACKUP_START_ADDR)
#define BACKUP_SIZE        (FW_BACKUP_SIZE)
#define BACKUP_PAGE_SIZE   (FW_META_PAGE_SIZE)
#define BACKUP_PAGE_MAX    (BACKUP_SIZE / BACKUP_PAGE_SIZE)   /* 20 页 */

/* DL_CHUNK_SIZE 在 app_config.h */

/* 拼命令用的字符串工具：不用 snprintf，省 Flash */
static char *ota_put_str(char *p, const char *s)
{
    while (*s != '\0')
    {
        *p = *s;
        p++;
        s++;
    }

    return p;
}

static char *ota_put_u32(char *p, uint32_t v)
{
    char     tmp[10];
    uint32_t n = 0U;

    if (v == 0U)
    {
        *p = '0';
        return p + 1;
    }

    while (v != 0U)
    {
        tmp[n] = (char)('0' + (uint8_t)(v % 10U));
        n++;
        v /= 10U;
    }

    while (n > 0U)
    {
        n--;
        *p = tmp[n];
        p++;
    }

    return p;
}

/**
 * @brief  下载一包固件到 ota_rx_buf
 * @param  tid    升级任务号（来自 Metadata）
 * @param  offset 本包在固件里的起始字节偏移
 * @param  chunk  本包请求的字节数（Range: offset ~ offset+chunk-1）
 * @retval 1: 指令发出并且收到了应答（是否成功要用 OTA_GetHttpResult 判断）
 */
uint8_t OTA_Load(uint32_t tid, uint32_t offset, uint32_t chunk)
{
    static char command[320];
    char       *p = command;
    uint16_t    len;
    uint8_t     ch;
    uint32_t    end;
    uint32_t    timeout;

    if ((chunk == 0U) || (offset > BACKUP_SIZE))
    {
        return 0;
    }

    end = offset + chunk - 1U;

    /* 拼：AT+HTTPCLIENT=2,0,"<url>/<tid>/download","","",1,"<auth>","Range: a-b" */
    p = ota_put_str(p, "AT+HTTPCLIENT=2,0,\"");
    p = ota_put_str(p, ONENET_OTA_URL);
    p = ota_put_str(p, "/");
    p = ota_put_u32(p, tid);
    p = ota_put_str(p, "/download\",\"\",\"\",1,\"");
    p = ota_put_str(p, ONENET_OTA_AUTH);
    p = ota_put_str(p, "\",\"Range: ");
    p = ota_put_u32(p, offset);
    p = ota_put_str(p, "-");
    p = ota_put_u32(p, end);
    p = ota_put_str(p, "\"\r\n");
    *p = '\0';

    len = (uint16_t)(p - command);

    OTA_ClearRxBuf();

    if (HAL_UART_Transmit(&ESP32_UART, (uint8_t *)command, len, ESP32_TX_TIMEOUT_MS) != HAL_OK)
    {
        printf("TX FAIL\r\n");
        return 0;
    }

    /* 首字节要等模组把 HTTP 请求做完（建连+服务器响应），给足时间；
       收到第一个字节之后，同一个 TCP 流里的数据是连续的，1 秒足够 */
    timeout = ONENET_OTA_TIMEOUT_MS;

    while (ota_rx_len < (OTA_RX_BUF_SIZE - 1U))
    {
        if (HAL_UART_Receive(&ESP32_UART, &ch, 1U, timeout) != HAL_OK)
        {
            break;                          /* 一帧结束 */
        }

        timeout = OTA_RX_BYTE_TIMEOUT;      /* 后续字节改用短超时 */

        ota_rx_buf[ota_rx_len] = ch;
        ota_rx_len++;

        /* 收到 "OK\r\n" 结尾、并且数据确实收齐了，才提前结束（不用等满超时） */
        if ((ota_rx_len >= 4U) &&
            (ota_rx_buf[ota_rx_len - 4U] == 'O') &&
            (ota_rx_buf[ota_rx_len - 3U] == 'K') &&
            (ota_rx_buf[ota_rx_len - 2U] == '\r') &&
            (ota_rx_buf[ota_rx_len - 1U] == '\n'))
        {
            const uint8_t *dp = NULL;
            uint32_t       dn = 0U;

            if (OTA_GetHttpResult(&dp, &dn) == OTA_HTTP_OK)
            {
                break;
            }
        }
    }

    ota_rx_buf[ota_rx_len] = '\0';

    return 1;
}

/**
 * @brief  按固件大小一次性擦除 BACKUP 区需要的页
 * @param  size 固件字节数
 * @retval 1: 成功
 * @note   4592 字节 -> 5 页（5120 字节）
 */
uint8_t OTA_BackupErase(uint32_t size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t               page_error = 0UL;
    uint32_t               pages;
    uint8_t                ok;

    if ((size == 0U) || (size > BACKUP_SIZE))
    {
        return 0;
    }

    pages = (size + BACKUP_PAGE_SIZE - 1U) / BACKUP_PAGE_SIZE;

    if (pages > BACKUP_PAGE_MAX)
    {
        pages = BACKUP_PAGE_MAX;
    }

    (void)HAL_FLASH_Unlock();

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BACKUP_ADDR;
    erase.NbPages     = pages;

    ok = (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) ? 1U : 0U;

    (void)HAL_FLASH_Lock();

    return ok;
}

/**
 * @brief  把一包数据写到 BACKUP_ADDR + offset
 * @param  offset 偏移，必须是 4 的倍数
 * @param  data   数据
 * @param  len    字节数
 * @retval 1: 成功
 * @note   不足 4 字节的尾部补 0xFF（擦除态）
 */
uint8_t OTA_BackupWrite(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t word;
    uint8_t  b0;
    uint8_t  b1;
    uint8_t  b2;
    uint8_t  b3;
    uint8_t  ok = 1U;

    if ((data == NULL) || (len == 0U))
    {
        return 0;
    }

    if (((offset & 3U) != 0U) || ((offset + len) > BACKUP_SIZE))
    {
        return 0;
    }

    (void)HAL_FLASH_Unlock();

    for (i = 0U; (ok != 0U) && (i < len); i += 4U)
    {
        b0 = data[i];
        b1 = ((i + 1U) < len) ? data[i + 1U] : 0xFFU;
        b2 = ((i + 2U) < len) ? data[i + 2U] : 0xFFU;
        b3 = ((i + 3U) < len) ? data[i + 3U] : 0xFFU;

        word = (uint32_t)b0
             | ((uint32_t)b1 << 8)
             | ((uint32_t)b2 << 16)
             | ((uint32_t)b3 << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              BACKUP_ADDR + offset + i,
                              (uint64_t)word) != HAL_OK)
        {
            ok = 0U;
        }
    }

    (void)HAL_FLASH_Lock();

    return ok;
}

/**
 * @brief  按任务信息把固件下载到备份区
 * @param  task 任务信息（target / tid / size，来自 OTA_CheckUpdate）
 * @retval 1: 全部写完  0: 中途失败
 * @note   这里不读 Metadata —— 记录要等下载成功之后才写（见 task.c）
 */
uint8_t OTA_Download(const fw_meta_t *task)
{
    uint32_t off = 0U;
    uint32_t reported = 0U;          /* 上次上报的进度百分比 */

    if (task == NULL)
    {
        return 0;
    }

    if ((task->size == 0U) || (task->size > BACKUP_SIZE))
    {
        printf("bad size\r\n");
        (void)OTA_Report(task->tid, 102U);          /* 下载失败：空间不足 */
        return 0;
    }

    printf("dl %lu %lu\r\n", (unsigned long)task->tid, (unsigned long)task->size);

    /* 清掉可能残留的自定义 HTTP 头（进度上报用过 AT+HTTPCHEAD）。
       不清的话下载请求会带上 Content-Type: application/json，
       服务器直接回 HTML 错误页 —— 这是实际踩过的坑 */
    (void)OTA_SendDataTimeout("AT+HTTPCHEAD=0\r\n", 3000U);

    if (OTA_BackupErase(task->size) == 0U)
    {
        printf("erase fail\r\n");
        (void)OTA_Report(task->tid, 107U);          /* 下载失败：未知异常 */
        return 0;
    }

    while (off < task->size)
    {
        uint32_t       chunk = task->size - off;
        const uint8_t *p = NULL;
        uint32_t       n = 0U;
        uint32_t       t;
        uint8_t        ok = 0U;

        if (chunk > DL_CHUNK_SIZE)
        {
            chunk = DL_CHUNK_SIZE;
        }

        /* 每包最多试 3 次：HTTP 偶发超时/丢包时，重发同一条 Range 命令
           （上一包失败时数据没写进 Flash，所以同一个 offset 重来是安全的） */
        for (t = 0U; (t < 3U) && (ok == 0U); t++)
        {
            if (OTA_Load(task->tid, off, chunk) == 0U)
            {
                HAL_Delay(50U);
                continue;
            }

            if (OTA_GetHttpResult(&p, &n) == OTA_HTTP_OK)
            {
                ok = 1U;
                break;
            }

            HAL_Delay(100U);
        }

        if (ok == 0U)
        {
            uint8_t step = 207U;                    /* 默认：达到最大重试次数 */

            if (ota_rx_len >= (OTA_RX_BUF_SIZE - 1U))
            {
                step = 103U;                        /* 内存溢出：接收缓冲被灌满 */
            }
            else if (ota_rx_len == 0U)
            {
                step = 104U;                        /* 下载请求超时：一个字节都没收到 */
            }
            else if ((strstr((char *)ota_rx_buf, "ERROR") != NULL) ||
                     (strstr((char *)ota_rx_buf, "CLOSED") != NULL))
            {
                step = 106U;                        /* 信号不良 / 连接断开 */
            }

            printf("rx fail %lu %lu %u\r\n",
                   (unsigned long)off, (unsigned long)ota_rx_len, (unsigned int)step);

            (void)OTA_Report(task->tid, step);
            return 0;
        }

        if ((n == 0U) || (n > chunk))
        {
            printf("len err\r\n");
            (void)OTA_Report(task->tid, 107U);
            return 0;
        }

        if (OTA_BackupWrite(off, p, n) == 0U)
        {
            printf("wr fail\r\n");
            (void)OTA_Report(task->tid, 107U);
            return 0;
        }

        off += n;

        /* ---- 进度上报：每跨过 20% 报一次，100% 留给 101 ---- */
        {
            uint32_t percent = (uint32_t)((off * 100UL) / task->size);

            if ((percent >= (reported + 20U)) && (percent < 100U))
            {
                (void)OTA_Report(task->tid, (uint8_t)percent);
                reported = percent;
            }
        }
    }

    printf("dl ok %lu\r\n", (unsigned long)off);

    (void)OTA_Report(task->tid, 101U);              /* 升级包下载成功 */

    return 1;
}

/* ===================== 上报 OTA 进度（OneNET） =====================
 * 不走 AT+HTTPCHEAD / AT+HTTPCPOST！
 *
 * 原因：AT+HTTPCHEAD 设的自定义头是"粘性"的（存在模组 NVS 里），
 *       之后所有 AT+HTTPCLIENT 请求都会带上它，nginx 直接回 400，
 *       连 AT+RST 都清不掉（AT+HTTPCHEAD=0 也清不干净）。
 *
 * 改法：AT+CIPSTART + AT+CIPSEND 手写整个 HTTP 报文，
 *       头只存在于这一次 TCP 连接里，物理上不可能污染后续请求。
 * ================================================================ */

/* OTA_TCP_TIMEOUT_MS / OTA_CIPSEND_TIMEOUT_MS 在 app_config.h */

/* 等缓冲区里出现 t1 或 t2（t2 可以为 NULL），出现 ERROR 判失败 */
static uint8_t ota_wait_token(const char *t1, const char *t2, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();

    do
    {
        OTA_Receive(OTA_RX_BYTE_TIMEOUT);

        if ((t1 != NULL) && (strstr((char *)ota_rx_buf, t1) != NULL)) { return 1U; }
        if ((t2 != NULL) && (strstr((char *)ota_rx_buf, t2) != NULL)) { return 1U; }
        if (strstr((char *)ota_rx_buf, "ERROR") != NULL)              { return 0U; }
    } while ((HAL_GetTick() - t0) < timeout_ms);

    return 0U;
}

/* 发一条 AT 指令（自带 \r\n），只负责发出去 */
static uint8_t ota_send_at(const char *cmd, uint16_t len)
{
    OTA_ClearRxBuf();

    if (HAL_UART_Transmit(&ESP32_UART, (uint8_t *)cmd, len, ESP32_TX_TIMEOUT_MS) != HAL_OK)
    {
        printf("TX FAIL\r\n");
        return 0U;
    }

    return 1U;
}

/**
 * @brief  向 OneNET 上报 OTA 升级状态（裸 TCP + 手写 HTTP 报文）
 * @param  tid  升级任务号（URL 里的 /<tid>/status）
 * @param  step 进度值：0~100 下载进度，101 下载成功，201 升级成功，2xx 失败…
 * @retval 1: 上报成功  0: 失败
 * @note   POST /fuse-ota/.../<tid>/status HTTP/1.1
 *         Host / Content-Type / Authorization / Content-Length / Connection: close
 */
uint8_t OTA_Report(uint32_t tid, uint8_t step)
{
    static char req[384];
    static char cmd[32];
    char        body[24];
    char       *p;
    uint16_t    body_len;
    uint16_t    req_len;
    uint16_t    cmd_len;
    uint8_t     ok = 0U;
    uint32_t    t0;

    /* ---- 1. body：{"step":N} ---- */
    p        = ota_put_str(body, "{\"step\":");
    p        = ota_put_u32(p, (uint32_t)step);
    p        = ota_put_str(p, "}");
    *p       = '\0';
    body_len = (uint16_t)(p - body);

    /* ---- 2. 整个 HTTP 请求报文 ---- */
    p = ota_put_str(req, "POST ");
    p = ota_put_str(p, ONENET_OTA_PATH);
    p = ota_put_str(p, "/");
    p = ota_put_u32(p, tid);
    p = ota_put_str(p, "/status HTTP/1.1\r\nHost: " ONENET_OTA_HOST "\r\n");
    p = ota_put_str(p, "Content-Type: application/json\r\n");
    p = ota_put_str(p, ONENET_OTA_REPORT_AUTH "\r\n");
    p = ota_put_str(p, "Content-Length: ");
    p = ota_put_u32(p, (uint32_t)body_len);
    p = ota_put_str(p, "\r\nConnection: close\r\n\r\n");
    p = ota_put_str(p, body);
    *p = '\0';
    req_len = (uint16_t)(p - req);

    /* ---- 3. 先关掉可能残留的连接（没连接会回 ERROR，忽略） ---- */
    (void)ota_send_at("AT+CIPCLOSE\r\n", 13U);
    HAL_Delay(100U);

    /* ---- 4. 建立 TCP 连接 ---- */
    if (ota_send_at("AT+CIPSTART=\"TCP\",\"" ONENET_OTA_HOST "\",80\r\n",
                    (uint16_t)strlen("AT+CIPSTART=\"TCP\",\"" ONENET_OTA_HOST "\",80\r\n")) == 0U)
    {
        goto done;
    }

    if (ota_wait_token("CONNECT", "OK", OTA_TCP_TIMEOUT_MS) == 0U)
    {
        printf("tcp fail\r\n");
        goto done;
    }

    /* "CONNECT FAIL" / "DNS Fail" 里也含 CONNECT，要单独判掉 */
    if (strstr((char *)ota_rx_buf, "FAIL") != NULL)
    {
        printf("tcp fail\r\n");
        goto done;
    }

    /* ---- 5. AT+CIPSEND=<报文长度>，等 '>' 提示符 ---- */
    p       = ota_put_str(cmd, "AT+CIPSEND=");
    p       = ota_put_u32(p, (uint32_t)req_len);
    p       = ota_put_str(p, "\r\n");
    cmd_len = (uint16_t)(p - cmd);

    if (ota_send_at(cmd, cmd_len) == 0U)
    {
        goto done;
    }

    if (ota_wait_token(">", NULL, OTA_CIPSEND_TIMEOUT_MS) == 0U)
    {
        printf("no prompt\r\n");
        goto done;
    }

    /* ---- 6. 把 HTTP 报文整段发出去 ---- */
    OTA_ClearRxBuf();

    if (HAL_UART_Transmit(&ESP32_UART, (uint8_t *)req, req_len, ESP32_TX_TIMEOUT_MS) != HAL_OK)
    {
        goto done;
    }

    /* ---- 7. 等服务器应答（+IPD 里带着 HTTP 响应头和 {"code":0}） ---- */
    t0 = HAL_GetTick();

    do
    {
        OTA_Receive(OTA_RX_BYTE_TIMEOUT);

        if (strstr((char *)ota_rx_buf, "\"code\":0") != NULL) { break; }
        if (strstr((char *)ota_rx_buf, "CLOSED") != NULL)     { break; }
    } while ((HAL_GetTick() - t0) < OTA_TCP_TIMEOUT_MS);

    if (strstr((char *)ota_rx_buf, "\"code\":0") != NULL)
    {
        printf("rep ok %u\r\n", (unsigned int)step);
        ok = 1U;
    }
    else
    {
        printf("rep rej\r\n");
    }

done:
    /* ---- 8. 关连接、清缓冲，保证不影响后面的 AT+HTTPCLIENT 下载 ---- */
    (void)ota_send_at("AT+CIPCLOSE\r\n", 13U);
    HAL_Delay(200U);
    OTA_ClearRxBuf();

    return ok;
}
