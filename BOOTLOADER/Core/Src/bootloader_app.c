/**
  ******************************************************************************
  * @file    bootloader_app.c
  * @brief   把备份区（0x0800A000）的固件搬运到 App 区（0x08005000）
  *
  *          Flash 布局（和 bootloader_jump.h 一致）：
  *            0x08000000 - 0x08004FFF  Bootloader（本程序，20KB）
  *            0x08005000 - 0x08009FFF  App       （20KB，搬运目标）
  *            0x0800A000 - 0x0800EFFF  Backup    （20KB，新固件在这里）
  *            0x0800F000 - 0x0800FFFF  Metadata  （4KB）
  *
  *          注意：Bootloader 和 App 区不重叠，所以"边跑边擦 App 区"是安全的。
  ******************************************************************************
  */

#include "bootloader_app.h"
#include "bootloader_jump.h"

#include "main.h"

#define APP_PAGE_SIZE       (1024UL)                                    /* F103C8 每页 1KB */
#define APP_PAGE_COUNT      (BOOTLOADER_APP_SIZE_BYTES / APP_PAGE_SIZE) /* 20 页          */

/**
  * @brief 把备份区的固件写进 App 区
  * @param meta 升级记录，只用里面的 size
  * @retval true: 擦除 + 写入 + 回读校验全部通过
  */
bool Bootloader_CopyBackupToApp(const fw_meta_t *meta)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t               page_error = 0UL;
  uint32_t               i;
  uint32_t               word;
  uint8_t                b0;
  uint8_t                b1;
  uint8_t                b2;
  uint8_t                b3;
  bool                   ok = true;

  if (meta == NULL)
  {
    return false;
  }

  /* ---- 参数检查：大小必须落在 App 区以内 ---- */
  if ((meta->size == 0UL) || (meta->size > BOOTLOADER_APP_SIZE_BYTES))
  {
    return false;
  }

  (void)HAL_FLASH_Unlock();

  /* ---- 1. 擦除 App 区（20 页 = 20KB） ---- */
  erase.TypeErase   = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = BOOTLOADER_APP_START_ADDRESS;
  erase.NbPages     = APP_PAGE_COUNT;

  if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
  {
    ok = false;
  }

  /* ---- 2. 从备份区逐字拷贝到 App 区 ---- */
  for (i = 0UL; (ok != false) && (i < meta->size); i += 4UL)
  {
    b0 = *(volatile uint8_t *)(BOOTLOADER_BACKUP_START_ADDRESS + i);
    b1 = ((i + 1UL) < meta->size) ? *(volatile uint8_t *)(BOOTLOADER_BACKUP_START_ADDRESS + i + 1UL) : 0xFFU;
    b2 = ((i + 2UL) < meta->size) ? *(volatile uint8_t *)(BOOTLOADER_BACKUP_START_ADDRESS + i + 2UL) : 0xFFU;
    b3 = ((i + 3UL) < meta->size) ? *(volatile uint8_t *)(BOOTLOADER_BACKUP_START_ADDRESS + i + 3UL) : 0xFFU;

    word = (uint32_t)b0
         | ((uint32_t)b1 << 8)
         | ((uint32_t)b2 << 16)
         | ((uint32_t)b3 << 24);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          BOOTLOADER_APP_START_ADDRESS + i,
                          (uint64_t)word) != HAL_OK)
    {
      ok = false;
    }
  }

  (void)HAL_FLASH_Lock();

  /* ---- 3. 回读校验：App 区和备份区逐字节比较 ---- */
  for (i = 0UL; (ok != false) && (i < meta->size); i++)
  {
    if (*(volatile uint8_t *)(BOOTLOADER_APP_START_ADDRESS + i) !=
        *(volatile uint8_t *)(BOOTLOADER_BACKUP_START_ADDRESS + i))
    {
      ok = false;
    }
  }

  return ok;
}
