#ifndef J1939_CAN_HELPER_H
#define J1939_CAN_HELPER_H

#include <Arduino.h>
#include <stm32f1xx_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the CAN hardware peripheral at specified baud rate (250 or 500 kbps) */
bool J1939_CAN_Hardware_Init_Baud(uint32_t baud_kbps);
bool J1939_CAN_Hardware_Init(void);

/* Transmit a J1939 message over CAN */
bool J1939_CAN_Hardware_Transmit(uint32_t pgn, 
                                 const uint8_t* data, 
                                 uint8_t data_length, 
                                 uint8_t priority, 
                                 uint8_t source_address);

/* Check for and receive a J1939 message */
bool J1939_CAN_Hardware_Receive(uint32_t* pgn, 
                               uint8_t* data, 
                               uint8_t* data_length, 
                               uint8_t* source_address);

#ifdef __cplusplus
}
#endif

#endif // J1939_CAN_HELPER_H
