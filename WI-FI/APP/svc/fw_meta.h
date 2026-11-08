/**
  ******************************************************************************
  * @file    fw_meta.h
  * @brief   OTA 元数据区（0x0800F000，4 KB）记录读写
  *
  *          App 收到 OneNET 的升级应答后，把 target / tid / size / md5
  *          解析出来写进 Metadata 区；bootloader 复位后从同一地址读出来，
  *          就知道"有没有新固件、多大、哪个任务号"。
  *
  *          这个文件 App 和 bootloader 各放一份（内容完全一样），
  *          地址、结构体布局、CRC 算法必须保持一致，不要只改一边。
  ******************************************************************************
  */

#ifndef FW_META_H
#define FW_META_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ==================== Flash 分区：全工程唯一出处 ====================
 *   0x08000000 - 0x08004FFF  Bootloader  (20 KB)
 *   0x08005000 - 0x08009FFF  App         (20 KB)  <-- 本工程
 *   0x0800A000 - 0x0800EFFF  App backup  (20 KB)
 *   0x0800F000 - 0x0800FFFF  Metadata    ( 4 KB)  <-- 这里
 *
 *   改分区只改这一段！STM32F103XX_FLASH.ld 的 FLASH ORIGIN/LENGTH
 *   也必须同步（链接脚本没法引用 C 宏），bootloader 侧同理。
 * ================================================================== */
#define FW_FLASH_BASE_ADDR        (0x08000000UL)
#define FW_BOOT_SIZE              (20UL * 1024UL)
#define FW_APP_START_ADDR         (FW_FLASH_BASE_ADDR + FW_BOOT_SIZE)   /* 0x08005000 */
#define FW_APP_SIZE               (20UL * 1024UL)
#define FW_BACKUP_START_ADDR      (FW_APP_START_ADDR + FW_APP_SIZE)     /* 0x0800A000 */
#define FW_BACKUP_SIZE            (20UL * 1024UL)
#define FW_META_FLASH_ADDR        (FW_BACKUP_START_ADDR + FW_BACKUP_SIZE) /* 0x0800F000 */
#define FW_META_FLASH_SIZE        (4UL * 1024UL)
#define FW_META_PAGE_SIZE         (1UL * 1024UL)   /* STM32F103C8：中容量，1KB/页 */

#define FW_META_APP_MAX_SIZE      (FW_APP_SIZE)    /* App 区大小，超过就不可能装下 */

#define FW_META_MAGIC             (0x314D5746UL)   /* 'F''W''M''1'（小端） */
#define FW_META_LAYOUT_VERSION    (1UL)

#define FW_META_TARGET_LEN        (8U)
#define FW_META_MD5_LEN           (16U)

/**
  * @brief 一条升级记录
  * @note  magic 是最后一个字段、也是最后写入的一个字：
  *        只有整条记录都写成功、且掉电没有把 magic 写坏，bootloader 才认它。
  */
typedef struct
{
  char     target[FW_META_TARGET_LEN];  /*!< 目标版本字符串，如 "v1.0"         */
  uint32_t tid;                         /*!< OneNET 升级任务 ID               */
  uint32_t size;                        /*!< 固件字节数                       */
  uint8_t  md5[FW_META_MD5_LEN];        /*!< 固件 MD5，全 0xFF 表示没解析到   */
  uint32_t layout_version;              /*!< = FW_META_LAYOUT_VERSION         */
  uint32_t crc;                         /*!< 本结构体 crc 之前所有字节的 CRC32 */
  uint32_t magic;                       /*!< = FW_META_MAGIC（最后写）        */
} fw_meta_t;

/* ---- 读写 ---------------------------------------------------------------- */

/**
  * @brief  从 Flash 读出记录并校验（magic + CRC + 版本号 + size 合理性）
  * @retval true: 记录有效   false: 没有记录 / 记录损坏
  */
bool FwMeta_Load(fw_meta_t *meta);

/**
  * @brief  擦除 Metadata 页并写入一条记录（写完后会回读校验）
  * @retval true: 写入并校验成功
  */
bool FwMeta_Save(const fw_meta_t *meta);

/**
  * @brief  擦除 Metadata 页（bootloader 升级完成后调用，清掉待升级标记）
  */
bool FwMeta_Erase(void);

/* ---- 解析 ESP-AT 应答 ----------------------------------------------------- */

/**
  * @brief  从 AT+HTTPCLIENT 的应答文本里解析 target / tid / size / md5
  * @param  rx_text 形如  "+HTTPCLIENT:183,{...,"data":{"target":"v1.0",
  *                  "tid":1534347,"size":4592,...}}"
  * @param  meta    解析结果（只填内存结构体，不写 Flash）
  * @retval true: 三个关键字段都解析到了
  */
bool FwMeta_ParseJson(const char *rx_text, fw_meta_t *meta);

/**
  * @brief  解析 + 直接写进 Metadata 区，一步到位
  */
bool FwMeta_SaveFromJson(const char *rx_text);

#ifdef __cplusplus
}
#endif

#endif /* FW_META_H */
