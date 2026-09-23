/*
 * =============================================================================
 * FILE : hal_console.h
 * WHAT : Debug/command console (UART) - non-blocking read, formatted write.
 * =============================================================================
 */

#ifndef HAL_CONSOLE_H
#define HAL_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void hal_console_init(uint32_t baud);

/* Returns next received byte, or -1 if none available */
int  hal_console_read(void);

void hal_console_write(const char* str);
void hal_console_write_line(const char* str);   /* str may be NULL -> newline only */
void hal_console_write_u32(uint32_t value);
void hal_console_write_i32(int32_t value);
void hal_console_write_hex(uint32_t value);
void hal_console_write_float(float value, uint8_t decimals);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CONSOLE_H */
