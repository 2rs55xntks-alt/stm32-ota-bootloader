/**
  ******************************************************************************
  * @file    task.c
  * @brief   应用层任务
  *            1) led_task() —— 其他业务（这里用 LED 闪烁代替）
  *            2) ota_task() —— 升级检查：查 WiFi → 查 OneNET → 对比 → 下载 → 跳转
  ******************************************************************************
  */

#include <string.h>

#include "task.h"
#include "esp32.h"
#include "wifi.h"
#include "OTA.h"
#include "fw_meta.h"
#include "boot_jump.h"

/* LED_BLINK_MS / WIFI_CHECK_MS 见 app_config.h */

/* 检查模组是否已连上路由器并拿到 IP：
 * 发 AT+CIPSTATUS，应答里有 "STATUS:2" 表示已获取 IP
 * （wifi.c 里的 WIFI_CheckConnected 被注释掉了，这里用公开接口自己实现一份）
 */
static uint8_t wifi_check_connected(void)
{
    printf("chk wifi\r\n");

    if (ESP32_SendDataTimeout("AT+CIPSTATUS\r\n", WIFI_CHECK_MS) == 0U)
    {
        printf("no resp\r\n");
        return 0U;
    }

    if (strstr((char *)esp32_rx_buf, "STATUS:2") == NULL)
    {
        printf("wifi no ip\r\n");
        return 0U;
    }

    printf("wifi ok\r\n");

    return 1U;
}

/* ==================== 任务 1：其他程序（LED 闪烁） ====================
 * 非阻塞：靠 HAL_GetTick 计时，主循环里反复调用即可
 */
void led_task(void)
{
    static uint32_t last = 0U;

    if ((HAL_GetTick() - last) >= LED_BLINK_MS)
    {
        last = HAL_GetTick();
        HAL_GPIO_TogglePin(REMIND_LED_GPIO_Port, REMIND_LED_Pin);
    }
}

/* ==================== 任务 2：升级检查 ====================
 *   2.1 检查 WiFi 连接状态
 *   2.2 查询 OneNET 是否有新任务（只解析，不写 Flash）
 *   2.3 版本 / 大小有任意一个不同 → 下载到备份区
 *   2.4 下载成功后才写 Metadata
 *   2.5 跳回 bootloader 搬运
 *
 * 关键：Metadata 只在“下载成功之后”才写。
 *       如果下载失败，记录还是旧的，下次上电会重新比对、重新下载。
 */
void ota_task(void)
{
    fw_meta_t old_meta;
    fw_meta_t task;
    uint8_t   old_ok;

    /* ---- 升级前先读一次：这是“上一次下载成功过的版本” ---- */
    old_ok = (FwMeta_Load(&old_meta) != false) ? 1U : 0U;

    if (old_ok != 0U)
    {
        printf("meta %s %lu\r\n", old_meta.target, (unsigned long)old_meta.size);
    }
    else
    {
        printf("no meta\r\n");
    }

    /* ---- 2.1 检查 WiFi 连接状态 ---- */
    if (wifi_check_connected() == 0U)
    {
        printf("wifi down, skip\r\n");
        return;
    }

    /* ---- 2.2 上次是不是刚被 bootloader 装好？是就补报 201 ---- */
    if (Boot_TakeInstalledFlag() != 0U)
    {
        fw_meta_t installed;

        if (FwMeta_Load(&installed) != false)
        {
            printf("rep 201\r\n");
            (void)OTA_Report(installed.tid, 201U);
        }
    }

    /* ---- 2.3 查 OneNET 有没有新任务（这里只解析） ---- */
    if (OTA_CheckUpdate(&task) == 0U)
    {
        printf("no task\r\n");
        return;
    }

    /* ---- 2.4 版本 / 大小任意一个不同就升级 ---- */
    if ((old_ok != 0U) &&
        (strcmp(old_meta.target, task.target) == 0) &&
        (old_meta.size == task.size))
    {
        printf("latest %s\r\n", task.target);
        return;
    }

    printf("new %s %lu\r\n", task.target, (unsigned long)task.size);

    /* ---- 2.5 下载到备份区 ---- */
    if (OTA_Download(&task) == 0U)
    {
        printf("dl fail\r\n");
        return;
    }

    /* ---- 2.6 下载成功，这时候才把任务写进 Metadata ---- */
    if (FwMeta_Save(&task) == false)
    {
        printf("meta fail\r\n");
        (void)OTA_Report(task.tid, 107U);        /* 下载失败：未知异常 */
        return;
    }

    printf("jump boot\r\n");
    HAL_Delay(100U);

    /* ---- 2.7 置 BKP 标记 + 软复位，不返回 ---- */
    Boot_RequestBootloader();
}
