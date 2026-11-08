#include "bootloader_jump.h"
#include "main.h"

typedef void (*app_entry_t)(void);

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

/**
 * @brief Validate the App vector table and jump to the App reset handler.
 */
bool Bootloader_JumpToApp(uint32_t app_address)
{
  uint32_t app_stack_pointer;
  uint32_t app_reset_handler;
  uint32_t index;

  if ((app_address < BOOTLOADER_APP_START_ADDRESS) ||
      (app_address >= BOOTLOADER_APP_END_ADDRESS) ||
      ((app_address & 0x7UL) != 0UL))
  {
    return false;
  }

  app_stack_pointer = *(volatile uint32_t *)app_address;
  app_reset_handler = *(volatile uint32_t *)(app_address + 4UL);

  /* A valid initial MSP lies inside the STM32F103C8's 20 KB SRAM. */
  if ((app_stack_pointer < (STM32_SRAM_START_ADDRESS + 8UL)) ||
      (app_stack_pointer > STM32_SRAM_END_ADDRESS) ||
      ((app_stack_pointer & 0x7UL) != 0UL))
  {
    return false;
  }

  /* The Reset handler must be inside App and its address must use the Thumb bit. */
  if (((app_reset_handler & 0x1UL) == 0UL) ||
      ((app_reset_handler & ~0x1UL) < BOOTLOADER_APP_START_ADDRESS) ||
      ((app_reset_handler & ~0x1UL) >= BOOTLOADER_APP_END_ADDRESS))
  {
    return false;
  }

  /* Reset peripherals and clocks to the state expected by App startup code. */
  (void)HAL_DeInit();
  (void)HAL_RCC_DeInit();

  /* Stop current interrupts before changing the vector table and stack. */
  __disable_irq();
  SysTick->CTRL = 0UL;
  SysTick->LOAD = 0UL;
  SysTick->VAL = 0UL;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  for (index = 0UL; index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); index++)
  {
    NVIC->ICER[index] = 0xFFFFFFFFUL;
    NVIC->ICPR[index] = 0xFFFFFFFFUL;
  }

  SCB->VTOR = app_address;
  __DSB();
  __ISB();

#if defined(__GNUC__) || defined(__clang__)
  /*
   * Both operands are loaded before this block. Do not use a normal C function
   * call here: at -O0 the compiler could reload the reset address from the old
   * stack after MSP has already been changed.
   */
  __asm volatile(
      "msr msp, %0\n\t"
      "cpsie i\n\t"
      "bx %1\n\t"
      :
      : "r"(app_stack_pointer), "r"(app_reset_handler)
      : "memory");
#else
  app_entry_t app_entry = (app_entry_t)app_reset_handler;

  __set_MSP(app_stack_pointer);
  __enable_irq();
  app_entry();
#endif

  /* A correct App Reset_Handler never returns. */
  return true;
}
