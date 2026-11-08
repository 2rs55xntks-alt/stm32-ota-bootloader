/**
  ******************************************************************************
  * @file    boot_jump.h
  * @brief   App 主动回到 bootloader（0x08000000）
  *
  *          两种方式：
  *            1) Boot_RequestBootloader()     —— 置 BKP 标记 + 软复位（推荐）
  *            2) Boot_JumpToBootloaderDirect() —— 不做复位，直接跳（仅调试用）
  *
  *          bootloader 侧要配合：跳转 App 之前先看 BKP->DR1 是不是
  *          BOOT_JUMP_MAGIC，是就清掉标记并且不跳 App。
  ******************************************************************************
  */

#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BOOT_JUMP_MAGIC        (0x0000A55AU)    /* 存进 BKP->DR1 的标记   */

/* 存进 BKP->DR2：bootloader 搬运成功后置位，新 App 启动时报 201 然后清除 */
#define BOOT_INSTALLED_MAGIC   (0x00005A5AU)

/**
  * @brief bootloader 用：标记"新固件已装好"
  */
void Boot_MarkInstalled(void);

/**
  * @brief App 用：检查是否刚装完新固件，有则返回 1 并清标记
  */
uint8_t Boot_TakeInstalledFlag(void);

/**
  * @brief 置标记并软复位，回到 bootloader（推荐）
  * @note  复位后 BKP 域内容还在，bootloader 读 BKP->DR1 就知道是被 App 叫来的
  */
void Boot_RequestBootloader(void);

/**
  * @brief bootloader 用：判断是不是 App 主动请求进来的
  * @retval true: 有请求（调用方随后应 Boot_ClearJumpRequest()）
  */
bool Boot_JumpRequested(void);

/**
  * @brief 清掉请求标记
  */
void Boot_ClearJumpRequest(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_JUMP_H */
