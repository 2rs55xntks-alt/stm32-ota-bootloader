/**
  ******************************************************************************
  * @file    boot_jump.c
  * @brief   App 主动回到 bootloader
  ******************************************************************************
  */

#include "boot_jump.h"
#include "main.h"

/* 打开备份域访问：BKP->DRx 属于备份域，先开 PWR/BKP 时钟并解除写保护 */
static void boot_backup_unlock(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
}

bool Boot_JumpRequested(void)
{
  boot_backup_unlock();

  return (READ_REG(BKP->DR1) == BOOT_JUMP_MAGIC);
}

void Boot_ClearJumpRequest(void)
{
  boot_backup_unlock();
  WRITE_REG(BKP->DR1, 0U);
}

/* bootloader 搬运成功后调用，给新 App 留个"已安装"标记 */
void Boot_MarkInstalled(void)
{
  boot_backup_unlock();
  WRITE_REG(BKP->DR2, BOOT_INSTALLED_MAGIC);
  __DSB();
}

/* 新 App 启动时调用：有标记返回 1 并清除（用于补报 201） */
uint8_t Boot_TakeInstalledFlag(void)
{
  boot_backup_unlock();

  if (READ_REG(BKP->DR2) != BOOT_INSTALLED_MAGIC)
  {
    return 0U;
  }

  WRITE_REG(BKP->DR2, 0U);

  return 1U;
}

/**
  * 方式一：置标记 + 软复位
  *   复位后从 0x08000000 重新跑 bootloader，bootloader 判断 BKP->DR1：
  *     等于 BOOT_JUMP_MAGIC => 是被 App 叫来的，清标记、停在 bootloader 等升级
  *     否则                => 正常上电，跳 App
  */
void Boot_RequestBootloader(void)
{
  boot_backup_unlock();

  WRITE_REG(BKP->DR1, BOOT_JUMP_MAGIC);   /* 复位不会清 BKP 域（掉电才清） */
  __DSB();

  __disable_irq();                        /* 别让 SysTick 打断 */
  NVIC_SystemReset();                     /* Cortex-M3 系统复位 */

}
