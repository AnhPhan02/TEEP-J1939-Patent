#include "hal_flash.h"
#include "stm32f103_reg.h"
#include <stddef.h>
#include <string.h>

#define HAL_FLASH_WAIT_ITERATIONS  10000000u

static bool prv_range_valid(uint32_t address, uint32_t length)
{
    if ((length == 0u) || (address < HAL_FLASH_PROFILE_SLOT_A_ADDRESS)) {
        return false;
    }
    return (address < HAL_FLASH_PROFILE_STORAGE_END) &&
           (length <= (HAL_FLASH_PROFILE_STORAGE_END - address));
}

static hal_flash_status_t prv_wait_not_busy(void)
{
    uint32_t remaining = HAL_FLASH_WAIT_ITERATIONS;
    while (((FLASH_IF->SR & FLASH_SR_BSY) != 0u) && (remaining > 0u)) {
        remaining--;
    }
    if (remaining == 0u) {
        return HAL_FLASH_TIMEOUT;
    }
    return HAL_FLASH_OK;
}

static hal_flash_status_t prv_operation_result(void)
{
    if ((FLASH_IF->SR & FLASH_SR_WRPRTERR) != 0u) {
        return HAL_FLASH_WRITE_PROTECTED;
    }
    if ((FLASH_IF->SR & FLASH_SR_PGERR) != 0u) {
        return HAL_FLASH_PROGRAM_ERROR;
    }
    return HAL_FLASH_OK;
}

static hal_flash_status_t prv_unlock(void)
{
    hal_flash_status_t status = prv_wait_not_busy();
    if (status != HAL_FLASH_OK) {
        return status;
    }
    if ((FLASH_IF->CR & FLASH_CR_LOCK) != 0u) {
        FLASH_IF->KEYR = FLASH_KEY1;
        FLASH_IF->KEYR = FLASH_KEY2;
        if ((FLASH_IF->CR & FLASH_CR_LOCK) != 0u) {
            return HAL_FLASH_WRITE_PROTECTED;
        }
    }
    return HAL_FLASH_OK;
}

static void prv_clear_status(void)
{
    FLASH_IF->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
}

static void prv_lock(void)
{
    FLASH_IF->CR |= FLASH_CR_LOCK;
}

hal_flash_status_t hal_flash_erase_page(uint32_t address)
{
    if (!prv_range_valid(address, HAL_FLASH_PAGE_SIZE) ||
        ((address % HAL_FLASH_PAGE_SIZE) != 0u)) {
        return HAL_FLASH_BAD_ADDRESS;
    }

    hal_flash_status_t status = prv_unlock();
    if (status != HAL_FLASH_OK) {
        return status;
    }
    prv_clear_status();
    FLASH_IF->CR |= FLASH_CR_PER;
    FLASH_IF->AR = address;
    FLASH_IF->CR |= FLASH_CR_STRT;
    status = prv_wait_not_busy();
    if (status == HAL_FLASH_OK) {
        status = prv_operation_result();
    }
    FLASH_IF->CR &= ~FLASH_CR_PER;
    prv_lock();
    if (status != HAL_FLASH_OK) {
        return status;
    }
    return hal_flash_is_erased(address, HAL_FLASH_PAGE_SIZE)
        ? HAL_FLASH_OK : HAL_FLASH_VERIFY_ERROR;
}

hal_flash_status_t hal_flash_program_halfword(uint32_t address,
                                              uint16_t value)
{
    if (!prv_range_valid(address, sizeof(value)) || ((address & 1u) != 0u)) {
        return HAL_FLASH_BAD_ADDRESS;
    }

    volatile uint16_t* const target =
        (volatile uint16_t*)(uintptr_t)address;
    if (*target == value) {
        return HAL_FLASH_OK;
    }
    if ((uint16_t)(*target & value) != value) {
        return HAL_FLASH_PROGRAM_ERROR;
    }

    hal_flash_status_t status = prv_unlock();
    if (status != HAL_FLASH_OK) {
        return status;
    }
    prv_clear_status();
    FLASH_IF->CR |= FLASH_CR_PG;
    *target = value;
    status = prv_wait_not_busy();
    if (status == HAL_FLASH_OK) {
        status = prv_operation_result();
    }
    FLASH_IF->CR &= ~FLASH_CR_PG;
    prv_lock();
    if (status != HAL_FLASH_OK) {
        return status;
    }
    return (*target == value) ? HAL_FLASH_OK : HAL_FLASH_VERIFY_ERROR;
}

hal_flash_status_t hal_flash_read(uint32_t address, void* destination,
                                  uint32_t length)
{
    if ((destination == NULL) || !prv_range_valid(address, length)) {
        return HAL_FLASH_BAD_ARGUMENT;
    }
    memcpy(destination, (const void*)(uintptr_t)address, length);
    return HAL_FLASH_OK;
}

bool hal_flash_is_erased(uint32_t address, uint32_t length)
{
    if (!prv_range_valid(address, length)) {
        return false;
    }
    const volatile uint8_t* data =
        (const volatile uint8_t*)(uintptr_t)address;
    for (uint32_t i = 0u; i < length; i++) {
        if (data[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}
