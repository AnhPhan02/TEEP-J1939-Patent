/*
 * =============================================================================
 * FILE : op_fault.c
 * WHAT : Cortex-M3 fault policy. A faulted node must not keep driving the bus:
 *        leave the CAN bus, light the LED, break into an attached debugger,
 *        otherwise reset and let the lifecycle start over.
 * =============================================================================
 */

#include "hal_can.h"
#include "hal_led.h"
#include "hal_system.h"

static void prv_fault(void)
{
    hal_can_stop();
    hal_led_set(true);

    if (hal_system_debugger_attached()) {
        __asm volatile ("bkpt #0");
    }
    hal_system_reset();
}

void HardFault_Handler(void)  { prv_fault(); }
void MemManage_Handler(void)  { prv_fault(); }
void BusFault_Handler(void)   { prv_fault(); }
void UsageFault_Handler(void) { prv_fault(); }
