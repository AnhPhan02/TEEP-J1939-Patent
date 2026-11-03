#ifndef TEST_HAL_FLASH_FAKE_H
#define TEST_HAL_FLASH_FAKE_H

#include <stdint.h>

#define TEST_FLASH_BYTES  8192u

void Test_Flash_Reset(void);
void Test_Flash_Set_Fail_After(int32_t mutation_index);
uint32_t Test_Flash_Get_Mutation_Count(void);
void Test_Flash_Snapshot(uint8_t out[TEST_FLASH_BYTES]);
void Test_Flash_Restore(const uint8_t in[TEST_FLASH_BYTES]);
void Test_Flash_Corrupt(uint32_t address, uint8_t value);

#endif
