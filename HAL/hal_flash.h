/* STM32F103 internal-Flash operations restricted to the profile slots. */
#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define HAL_FLASH_PAGE_SIZE                 1024u
#define HAL_FLASH_PROFILE_SLOT_SIZE          4096u
#define HAL_FLASH_PROFILE_SLOT_A_ADDRESS  0x0800E000u
#define HAL_FLASH_PROFILE_SLOT_B_ADDRESS  0x0800F000u
#define HAL_FLASH_PROFILE_STORAGE_END      0x08010000u

typedef enum {
    HAL_FLASH_OK = 0,
    HAL_FLASH_BAD_ARGUMENT,
    HAL_FLASH_BAD_ADDRESS,
    HAL_FLASH_TIMEOUT,
    HAL_FLASH_PROGRAM_ERROR,
    HAL_FLASH_WRITE_PROTECTED,
    HAL_FLASH_VERIFY_ERROR
} hal_flash_status_t;

hal_flash_status_t hal_flash_erase_page(uint32_t address);
hal_flash_status_t hal_flash_program_halfword(uint32_t address,
                                              uint16_t value);
hal_flash_status_t hal_flash_read(uint32_t address, void* destination,
                                  uint32_t length);
bool hal_flash_is_erased(uint32_t address, uint32_t length);

#ifdef __cplusplus
}
#endif
#endif /* HAL_FLASH_H */
