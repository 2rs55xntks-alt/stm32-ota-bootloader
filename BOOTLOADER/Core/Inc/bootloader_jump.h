#ifndef BOOTLOADER_JUMP_H
#define BOOTLOADER_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*
 * STM32F103C8T6: 64 KB Flash.
 *
 * 0x08000000 - 0x08004FFF : Bootloader (20 KB)
 * 0x08005000 - 0x08009FFF : Application (20 KB)
 * 0x0800A000 - 0x0800EFFF : Application backup (20 KB)
 * 0x0800F000 - 0x0800FFFF : Metadata/event records (4 KB)
 */
#define STM32_FLASH_BASE_ADDRESS       (0x08000000UL)
#define BOOTLOADER_SIZE_BYTES          (20UL * 1024UL)
#define BOOTLOADER_APP_START_ADDRESS   (STM32_FLASH_BASE_ADDRESS + BOOTLOADER_SIZE_BYTES)
#define BOOTLOADER_APP_SIZE_BYTES      (20UL * 1024UL)
#define BOOTLOADER_APP_END_ADDRESS     (BOOTLOADER_APP_START_ADDRESS + BOOTLOADER_APP_SIZE_BYTES)
#define BOOTLOADER_BACKUP_START_ADDRESS (BOOTLOADER_APP_END_ADDRESS)
#define BOOTLOADER_BACKUP_SIZE_BYTES   (20UL * 1024UL)
#define BOOTLOADER_METADATA_START_ADDRESS \
  (BOOTLOADER_BACKUP_START_ADDRESS + BOOTLOADER_BACKUP_SIZE_BYTES)
#define BOOTLOADER_METADATA_SIZE_BYTES (4UL * 1024UL)

#define STM32_SRAM_START_ADDRESS       (0x20000000UL)
#define STM32_SRAM_END_ADDRESS         (0x20005000UL) /* 20 KB SRAM */

#define BOOT_JUMP_MAGIC        (0x0000A55AU)    /* 存进 BKP->DR1 的标记   */


bool Boot_JumpRequested(void);

void Boot_ClearJumpRequest(void);
/**
 * @brief Validate the App vector table and jump to the App reset handler.
 * @param app_address App vector-table address, normally BOOTLOADER_APP_START_ADDRESS.
 * @return false if the vector table is invalid; does not return after a valid jump.
 */
bool Bootloader_JumpToApp(uint32_t app_address);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_JUMP_H */
