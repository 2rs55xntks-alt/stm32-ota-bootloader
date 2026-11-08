/**
  ******************************************************************************
  * @file    bootloader_app.h
  * @brief   从备份区（0x0800A000）把固件搬运到 App 区（0x08005000）
  *
  *          App 下载成功后会往 BKP->DR1 写 BOOT_REQUEST_MAGIC 再软复位，
  *          bootloader 上电读到这个魔数就知道"要搬运了"。
  ******************************************************************************
  */

#ifndef BOOTLOADER_APP_H
#define BOOTLOADER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "fw_meta.h"

/* 必须和 App 工程 APP/boot_jump.h 里的 BOOT_JUMP_MAGIC 保持一致！ */
#define BOOT_REQUEST_MAGIC      (0x0000A55AUL)

/* 搬运成功后写到 BKP->DR2，新 App 看到它就上报 201（升级成功） */
#define BOOT_INSTALLED_MAGIC    (0x00005A5AUL)

/**
  * @brief 把备份区的固件写进 App 区
  * @param meta 升级记录（元数据里的 size / md5），CopyBackupToApp 只用 size
  * @retval true  写入并回读校验通过
  * @retval false 擦除/写入/校验失败
  * @note  失败时 App 区可能是半成品，调用方不要跳转，应停在 bootloader
  */
bool Bootloader_CopyBackupToApp(const fw_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_APP_H */
