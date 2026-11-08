/**
  ******************************************************************************
  * @file    fw_meta.c
  * @brief   OTA 元数据区读写（App 和 bootloader 共用同一份实现）
  ******************************************************************************
  */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "fw_meta.h"

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/* CRC32，多项式 0xEDB88320（和 zlib / 以太网 CRC 一致）：初值 0xFFFFFFFF，结果取反 */
static uint32_t fw_crc32(const void *data, uint32_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t       crc = 0xFFFFFFFFUL;
  uint32_t       i;
  int            bit;

  for (i = 0UL; i < len; i++)
  {
    crc ^= (uint32_t)p[i];

    for (bit = 0; bit < 8; bit++)
    {
      if ((crc & 1UL) != 0UL)
      {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      }
      else
      {
        crc = (crc >> 1);
      }
    }
  }

  return ~crc;
}

#define FW_META_CRC_LEN     (offsetof(fw_meta_t, crc))        /* 36 字节 */
#define FW_META_DATA_WORDS  (offsetof(fw_meta_t, magic) / 4UL) /* 10 个字 */

/* ==========================================================================
 * 读 / 写 / 擦
 * ========================================================================== */

bool FwMeta_Load(fw_meta_t *meta)
{
  if (meta == NULL)
  {
    return false;
  }

  /* Flash 可以直接按地址读，Meta 区是 8 字节对齐的 */
  (void)memcpy(meta, (const void *)FW_META_FLASH_ADDR, sizeof(fw_meta_t));

  if (meta->magic != FW_META_MAGIC)
  {
    return false;                       /* 从没写过，或写了一半掉电 */
  }

  if (meta->layout_version != FW_META_LAYOUT_VERSION)
  {
    return false;                       /* 结构体布局变了，丢弃旧记录 */
  }

  if (meta->crc != fw_crc32(meta, (uint32_t)FW_META_CRC_LEN))
  {
    return false;                       /* 数据损坏 */
  }

  if ((meta->size == 0UL) || (meta->size > FW_META_APP_MAX_SIZE))
  {
    return false;                       /* 大小不合理，App 区装不下 */
  }

  return true;
}

bool FwMeta_Erase(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t               page_error = 0UL;
  bool                   ok;

  (void)HAL_FLASH_Unlock();

  erase.TypeErase   = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = FW_META_FLASH_ADDR;
  erase.NbPages     = 1UL;              /* 一条记录只占 1 个 1KB 页 */

  ok = (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK);

  (void)HAL_FLASH_Lock();

  return ok;
}

bool FwMeta_Save(const fw_meta_t *meta)
{
  fw_meta_t        record;
  const uint32_t  *src;
  fw_meta_t        check;
  uint32_t         i;
  bool             ok = true;

  if (meta == NULL)
  {
    return false;
  }

  record = *meta;
  record.layout_version = FW_META_LAYOUT_VERSION;
  record.magic          = 0xFFFFFFFFUL;                       /* 先占位，最后再写真的 */
  record.crc            = fw_crc32(&record, (uint32_t)FW_META_CRC_LEN);

  if ((FwMeta_Load(&check) != false) &&
      (memcmp(&check, &record, (size_t)FW_META_CRC_LEN) == 0))
  {
    return true;                        /* 内容和已有记录一样，别白擦一次 */
  }

  (void)HAL_FLASH_Unlock();

  /* 1. 先擦页 */
  {
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t               page_error = 0UL;

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FW_META_FLASH_ADDR;
    erase.NbPages     = 1UL;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
      ok = false;
    }
  }

  /* 2. 按字写 target/tid/size/md5/version/crc（不含 magic） */
  src = (const uint32_t *)&record;

  for (i = 0UL; (ok != false) && (i < (uint32_t)FW_META_DATA_WORDS); i++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          FW_META_FLASH_ADDR + (i * 4UL),
                          (uint64_t)src[i]) != HAL_OK)
    {
      ok = false;
    }
  }

  /* 3. 最后写 magic：这一步成功，整条记录才算生效 */
  if (ok != false)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          FW_META_FLASH_ADDR + (uint32_t)offsetof(fw_meta_t, magic),
                          (uint64_t)FW_META_MAGIC) != HAL_OK)
    {
      ok = false;
    }
  }

  (void)HAL_FLASH_Lock();

  /* 4. 回读校验，防止写进去的是坏的 */
  if ((ok != false) && (FwMeta_Load(&check) == false))
  {
    ok = false;
  }

  return ok;
}

/* ==========================================================================
 * 解析 ESP-AT 应答
 * ========================================================================== */

static int fw_hex_val(char c)
{
  if ((c >= '0') && (c <= '9'))
  {
    return (int)(c - '0');
  }

  if ((c >= 'a') && (c <= 'f'))
  {
    return (int)(c - 'a') + 10;
  }

  if ((c >= 'A') && (c <= 'F'))
  {
    return (int)(c - 'A') + 10;
  }

  return -1;
}

/* 取 "key":123 里的数字（key 必须包含前面的引号和冒号，如 "\"tid\":"） */
static bool fw_json_get_u32(const char *json, const char *key, uint32_t *out)
{
  const char   *p = strstr(json, key);
  char         *end = NULL;
  unsigned long value;

  if (p == NULL)
  {
    return false;
  }

  p += strlen(key);

  while (*p == ' ')
  {
    p++;
  }

  if ((*p < '0') || (*p > '9'))
  {
    return false;
  }

  value = strtoul(p, &end, 10);

  if (end == p)
  {
    return false;
  }

  *out = (uint32_t)value;

  return true;
}

/* 取 "key":"字符串" 里的字符串（key 含前面的引号和冒号，如 "\"target\":\""） */
static bool fw_json_get_str(const char *json, const char *key,
                            char *buf, uint32_t buf_len)
{
  const char *p = strstr(json, key);
  const char *quote;
  uint32_t    n = 0UL;

  if ((p == NULL) || (buf_len == 0U))
  {
    return false;
  }

  p += strlen(key);
  quote = strchr(p, '"');

  if (quote == NULL)
  {
    return false;
  }

  while ((p < quote) && (n < (buf_len - 1UL)))
  {
    buf[n] = *p;
    n++;
    p++;
  }

  buf[n] = '\0';

  return (n > 0UL);
}

/* 取 "md5":"32位十六进制" 并转成 16 字节，解析不到就全 0xFF */
static bool fw_json_get_md5(const char *json, uint8_t out[FW_META_MD5_LEN])
{
  const char *p = strstr(json, "\"md5\":\"");
  uint32_t    i;
  int         hi;
  int         lo;

  if (p == NULL)
  {
    return false;
  }

  p += 7;

  for (i = 0UL; i < (2UL * FW_META_MD5_LEN); i++)
  {
    if (fw_hex_val(p[i]) < 0)
    {
      return false;
    }
  }

  for (i = 0UL; i < FW_META_MD5_LEN; i++)
  {
    hi = fw_hex_val(p[2UL * i]);
    lo = fw_hex_val(p[(2UL * i) + 1UL]);
    out[i] = (uint8_t)((hi << 4) | lo);
  }

  return true;
}

bool FwMeta_ParseJson(const char *rx_text, fw_meta_t *meta)
{
  const char *json;

  if ((rx_text == NULL) || (meta == NULL))
  {
    return false;
  }

  (void)memset(meta, 0, sizeof(fw_meta_t));
  (void)memset(meta->md5, 0xFF, FW_META_MD5_LEN);   /* 0xFF = 没有 md5 */
  meta->layout_version = FW_META_LAYOUT_VERSION;

  /*
   * ESP-AT 的应答长这样（可能前面还夹着别的打印）：
   *   +HTTPCLIENT:183,{"code":0,"msg":"succ","data":{"target":"v1.0",
   *                   "tid":1534347,"size":4592,"md5":"994d...","status":1,...}}
   * 先定位 +HTTPCLIENT: 后面的 '{'，找不到就退化成找第一个 '{'。
   */
  json = strstr(rx_text, "+HTTPCLIENT:");

  if (json != NULL)
  {
    json = strchr(json, '{');
  }

  if (json == NULL)
  {
    json = strchr(rx_text, '{');
  }

  if (json == NULL)
  {
    return false;
  }

  if (fw_json_get_str(json, "\"target\":\"", meta->target, FW_META_TARGET_LEN) == false)
  {
    return false;
  }

  if (fw_json_get_u32(json, "\"tid\":", &meta->tid) == false)
  {
    return false;
  }

  if (fw_json_get_u32(json, "\"size\":", &meta->size) == false)
  {
    return false;
  }

  (void)fw_json_get_md5(json, meta->md5);   /* md5 没有也不影响升级 */

  if ((meta->tid == 0UL) ||
      (meta->size == 0UL) ||
      (meta->size > FW_META_APP_MAX_SIZE))
  {
    return false;
  }

  return true;
}

bool FwMeta_SaveFromJson(const char *rx_text)
{
  fw_meta_t meta;

  if (FwMeta_ParseJson(rx_text, &meta) == false)
  {
    return false;
  }

  return FwMeta_Save(&meta);
}
