#include "hal_flash_fake.h"
#include "hal_flash.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static uint8_t s_flash[TEST_FLASH_BYTES];
static int32_t s_fail_after = -1;
static uint32_t s_mutations;

static bool prv_range(uint32_t address, uint32_t length, uint32_t* offset)
{
    if ((length == 0u) || (address < HAL_FLASH_PROFILE_SLOT_A_ADDRESS) ||
        (address >= HAL_FLASH_PROFILE_STORAGE_END) ||
        (length > HAL_FLASH_PROFILE_STORAGE_END - address)) {
        return false;
    }
    *offset = address - HAL_FLASH_PROFILE_SLOT_A_ADDRESS;
    return true;
}

static bool prv_fail(void)
{
    if ((s_fail_after >= 0) &&
        (s_mutations == (uint32_t)s_fail_after)) {
        s_mutations++;
        return true;
    }
    s_mutations++;
    return false;
}

void Test_Flash_Reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_fail_after = -1;
    s_mutations = 0u;
}

void Test_Flash_Set_Fail_After(int32_t mutation_index)
{
    s_fail_after = mutation_index;
    s_mutations = 0u;
}

uint32_t Test_Flash_Get_Mutation_Count(void)
{
    return s_mutations;
}

void Test_Flash_Snapshot(uint8_t out[TEST_FLASH_BYTES])
{
    memcpy(out, s_flash, sizeof(s_flash));
}

void Test_Flash_Restore(const uint8_t in[TEST_FLASH_BYTES])
{
    memcpy(s_flash, in, sizeof(s_flash));
    s_fail_after = -1;
    s_mutations = 0u;
}

void Test_Flash_Corrupt(uint32_t address, uint8_t value)
{
    uint32_t offset = 0u;
    if (prv_range(address, 1u, &offset)) {
        s_flash[offset] = value;
    }
}

hal_flash_status_t hal_flash_erase_page(uint32_t address)
{
    uint32_t offset = 0u;
    if (!prv_range(address, HAL_FLASH_PAGE_SIZE, &offset) ||
        ((address % HAL_FLASH_PAGE_SIZE) != 0u)) {
        return HAL_FLASH_BAD_ADDRESS;
    }
    if (prv_fail()) {
        return HAL_FLASH_PROGRAM_ERROR;
    }
    memset(&s_flash[offset], 0xFF, HAL_FLASH_PAGE_SIZE);
    return HAL_FLASH_OK;
}

hal_flash_status_t hal_flash_program_halfword(uint32_t address, uint16_t value)
{
    uint32_t offset = 0u;
    if (!prv_range(address, 2u, &offset) || ((address & 1u) != 0u)) {
        return HAL_FLASH_BAD_ADDRESS;
    }
    if (prv_fail()) {
        return HAL_FLASH_PROGRAM_ERROR;
    }
    const uint16_t old = (uint16_t)s_flash[offset] |
                         ((uint16_t)s_flash[offset + 1u] << 8);
    if ((uint16_t)(old & value) != value) {
        return HAL_FLASH_PROGRAM_ERROR;
    }
    s_flash[offset] = (uint8_t)value;
    s_flash[offset + 1u] = (uint8_t)(value >> 8);
    return HAL_FLASH_OK;
}

hal_flash_status_t hal_flash_read(uint32_t address, void* destination,
                                  uint32_t length)
{
    uint32_t offset = 0u;
    if ((destination == NULL) || !prv_range(address, length, &offset)) {
        return HAL_FLASH_BAD_ARGUMENT;
    }
    memcpy(destination, &s_flash[offset], length);
    return HAL_FLASH_OK;
}

bool hal_flash_is_erased(uint32_t address, uint32_t length)
{
    uint32_t offset = 0u;
    if (!prv_range(address, length, &offset)) {
        return false;
    }
    for (uint32_t i = 0u; i < length; i++) {
        if (s_flash[offset + i] != 0xFFu) {
            return false;
        }
    }
    return true;
}
