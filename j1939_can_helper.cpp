#include "j1939_can_helper.h"

static CAN_HandleTypeDef hcan;

/* CAN MSP Initialization (overrides the weak HAL function) */
extern "C" void HAL_CAN_MspInit(CAN_HandleTypeDef* hcan_inst) {
    if (hcan_inst->Instance == CAN1) {
        /* Enable Peripheral Clocks */
        __HAL_RCC_CAN1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_AFIO_CLK_ENABLE();

        /* Remap CAN1 pins to PB8 and PB9 FIRST before initializing GPIO pins */
        __HAL_AFIO_REMAP_CAN1_2();

        /* Pre-set PB9 to HIGH (recessive) to prevent dominant glitch during startup */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);

        /* Configure CAN GPIO pins:
           PB9 (CAN_TX)  ------> Alternate Function Push-Pull (Arduino D14)
           PB8 (CAN_RX)  ------> Input with Pull-Up (Arduino D15)
        */
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        
        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_8;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
}

/* CAN MSP De-Initialization (overrides the weak HAL function) */
extern "C" void HAL_CAN_MspDeInit(CAN_HandleTypeDef* hcan_inst) {
    if (hcan_inst->Instance == CAN1) {
        __HAL_RCC_CAN1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_9 | GPIO_PIN_8);
    }
}

/* Initialize bxCAN at specified baud rate (250 or 500 kbps) */
bool J1939_CAN_Hardware_Init_Baud(uint32_t baud_kbps) {
    /* If peripheral was already running, stop it first */
    HAL_CAN_Stop(&hcan);

    hcan.Instance = CAN1;

    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t prescaler = 1;
    uint32_t ts1 = CAN_BS1_14TQ;
    uint32_t ts2 = CAN_BS2_3TQ;
    uint32_t tq_sum = 18;

    uint32_t target_baud = (baud_kbps == 250) ? 250000 : 500000;

    if (target_baud == 500000) {
        /* Optimize bit timing for 500 kbps (~80% - 87.5% sample point) */
        if (pclk1 == 36000000) {
            prescaler = 4;
            ts1 = CAN_BS1_14TQ;  // 14 TQ
            ts2 = CAN_BS2_3TQ;   // 3 TQ
            tq_sum = 18;         // Sample Point = (1 + 14) / 18 = 83.33%
        } else if (pclk1 == 32000000) {
            prescaler = 4;
            ts1 = CAN_BS1_12TQ;  // 12 TQ
            ts2 = CAN_BS2_3TQ;   // 3 TQ
            tq_sum = 16;         // Sample Point = (1 + 12) / 16 = 81.25%
        } else if (pclk1 == 18000000) {
            prescaler = 2;
            ts1 = CAN_BS1_14TQ;  // 14 TQ
            ts2 = CAN_BS2_3TQ;   // 3 TQ
            tq_sum = 18;         // Sample Point = 83.33%
        } else if (pclk1 == 8000000) {
            prescaler = 1;
            ts1 = CAN_BS1_12TQ;  // 12 TQ
            ts2 = CAN_BS2_3TQ;   // 3 TQ
            tq_sum = 16;         // Sample Point = 81.25%
        } else {
            bool found = false;
            for (uint32_t tq = 18; tq >= 8; tq--) {
                if ((pclk1 % (tq * 500000)) == 0) {
                    prescaler = pclk1 / (tq * 500000);
                    uint32_t seg2 = tq / 5;
                    if (seg2 < 1) seg2 = 1;
                    uint32_t seg1 = tq - 1 - seg2;
                    ts1 = (seg1 - 1) << CAN_BTR_TS1_Pos;
                    ts2 = (seg2 - 1) << CAN_BTR_TS2_Pos;
                    tq_sum = tq;
                    found = true;
                    break;
                }
            }
            if (!found) {
                prescaler = pclk1 / (18 * 500000);
                if (prescaler == 0) prescaler = 1;
                ts1 = CAN_BS1_14TQ;
                ts2 = CAN_BS2_3TQ;
                tq_sum = 18;
            }
        }
    } else {
        /* Optimize bit timing for 250 kbps (~80% sample point) */
        if (pclk1 == 36000000) {
            prescaler = 9;
            ts1 = CAN_BS1_12TQ;  // 12 TQ
            ts2 = CAN_BS2_3TQ;   // 3 TQ
            tq_sum = 16;         // Sample Point = (1 + 12) / 16 = 81.25%
        } else if (pclk1 == 32000000) {
            prescaler = 8;
            ts1 = CAN_BS1_12TQ;
            ts2 = CAN_BS2_3TQ;
            tq_sum = 16;
        } else if (pclk1 == 18000000) {
            prescaler = 4;
            ts1 = CAN_BS1_13TQ;
            ts2 = CAN_BS2_4TQ;
            tq_sum = 18;
        } else if (pclk1 == 8000000) {
            prescaler = 2;
            ts1 = CAN_BS1_12TQ;
            ts2 = CAN_BS2_3TQ;
            tq_sum = 16;
        } else {
            bool found = false;
            for (uint32_t tq = 16; tq >= 8; tq--) {
                if ((pclk1 % (tq * 250000)) == 0) {
                    prescaler = pclk1 / (tq * 250000);
                    uint32_t seg2 = tq / 5;
                    if (seg2 < 1) seg2 = 1;
                    uint32_t seg1 = tq - 1 - seg2;
                    ts1 = (seg1 - 1) << CAN_BTR_TS1_Pos;
                    ts2 = (seg2 - 1) << CAN_BTR_TS2_Pos;
                    tq_sum = tq;
                    found = true;
                    break;
                }
            }
            if (!found) {
                prescaler = pclk1 / (16 * 250000);
                if (prescaler == 0) prescaler = 1;
                ts1 = CAN_BS1_12TQ;
                ts2 = CAN_BS2_3TQ;
                tq_sum = 16;
            }
        }
    }

    uint32_t seg1_val = (ts1 >> CAN_BTR_TS1_Pos) + 1;
    uint32_t seg2_val = (ts2 >> CAN_BTR_TS2_Pos) + 1;

    Serial.println("-----------------------------------------");
    Serial.print("[CAN DIAG] Baud Rate Configured: ");
    Serial.print((float)pclk1 / (float)(prescaler * tq_sum) / 1000.0f, 1);
    Serial.println(" kbps");
    Serial.print("[CAN DIAG] APB1 Clock: ");
    Serial.print(pclk1 / 1000000.0f, 3);
    Serial.print(" MHz | Prescaler: ");
    Serial.println(prescaler);
    Serial.print("[CAN DIAG] TQ: 1 + ");
    Serial.print(seg1_val);
    Serial.print(" + ");
    Serial.print(seg2_val);
    Serial.print(" = ");
    Serial.print(tq_sum);
    Serial.print(" TQ | Sample Point: ");
    Serial.print((float)(1 + seg1_val) / (float)tq_sum * 100.0f, 1);
    Serial.println("%");
    Serial.println("-----------------------------------------");

    hcan.Init.Prescaler = prescaler;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_2TQ;
    hcan.Init.TimeSeg1 = ts1;
    hcan.Init.TimeSeg2 = ts2;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(&hcan) != HAL_OK) {
        return false;
    }

    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) {
        return false;
    }

    if (HAL_CAN_Start(&hcan) != HAL_OK) {
        return false;
    }

    return true;
}

bool J1939_CAN_Hardware_Init(void) {
    return J1939_CAN_Hardware_Init_Baud(500);
}

/* Transmit a J1939 frame */
bool J1939_CAN_Hardware_Transmit(uint32_t pgn, 
                                 const uint8_t* data, 
                                 uint8_t data_length, 
                                 uint8_t priority, 
                                 uint8_t source_address) {
    if (data == NULL || data_length > 8) {
        return false;
    }

    /* Check if there are free hardware Tx mailboxes. If not, wait for one to open. */
    uint32_t start_wait = millis();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
        if (millis() - start_wait > 2) { // 2ms timeout
            static uint32_t last_mailbox_warn = 0;
            if (millis() - last_mailbox_warn > 2000) {
                uint32_t esr = hcan.Instance->ESR;
                uint8_t tec = (esr >> 16) & 0xFF;  // Transmit Error Counter
                uint8_t rec = (esr >> 24) & 0xFF;  // Receive Error Counter
                uint8_t lec = (esr >> 4) & 0x07;   // Last Error Code

                Serial.print("[CAN WARN] Tx Mailboxes FULL (timeout)! ESR Reg: 0x");
                Serial.print(esr, HEX);
                Serial.print(" | TEC: ");
                Serial.print(tec);
                Serial.print(" | REC: ");
                Serial.print(rec);
                Serial.print(" | Last Error Code (LEC): ");
                
                switch (lec) {
                    case 0: Serial.println("0 (No Error)"); break;
                    case 1: Serial.println("1 (Stuff Error)"); break;
                    case 2: Serial.println("2 (Form Error)"); break;
                    case 3: Serial.println("3 (ACK Error - No device is ACKing the message!)"); break;
                    case 4: Serial.println("4 (Bit Recessive Error)"); break;
                    case 5: Serial.println("5 (Bit Dominant Error)"); break;
                    case 6: Serial.println("6 (CRC Error)"); break;
                    default: Serial.println("Unknown"); break;
                }
                Serial.println("  -> Check hardware connections: CANH/CANL swapped? GND connected?");
                Serial.println("  -> Verify MCP2551 Pin 8 (Rs) is tied to GND for High-Speed mode!");
                Serial.println("  -> Is a 120 ohm terminating resistor present on the bus?");
                Serial.println("  -> Is TSMaster connected, active (Normal mode), and set to 500 kbps?");
                last_mailbox_warn = millis();
            }
            /* Abort stuck frames so hardware mailboxes don't deadlock */
            HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
            return false;
        }
    }

    CAN_TxHeaderTypeDef tx_header;
    uint32_t mailbox;


    uint8_t pf = (pgn >> 8) & 0xFF;
    uint8_t ps = pgn & 0xFF;
    uint8_t dp = (pgn >> 16) & 0x01;
    uint32_t can_id = 0;

    /* Build 29-bit Extended CAN ID according to SAE J1939 specification */
    can_id = ((uint32_t)(priority & 0x07) << 26) |  // Priority (3 bits)
             ((uint32_t)(dp & 0x01) << 24) |        // Data Page (1 bit)
             ((uint32_t)pf << 16);                  // PDU Format (8 bits)

    if (pf < 240) {
        /* PDU1 Format (Destination Directed)
           In J1939, broadcast transmissions of PDU1 messages default to 
           Global Destination Address (0xFF) in the PDU Specific (PS) field.
        */
        uint8_t destination_address = 0xFF; 
        can_id |= ((uint32_t)destination_address << 8);
    } else {
        /* PDU2 Format (Group Extension Broadcast)
           PS field holds the group extension value.
        */
        can_id |= ((uint32_t)ps << 8);
    }

    can_id |= source_address; // Source Address (8 bits)

    /* Setup Tx Header */
    tx_header.StdId = 0;
    tx_header.ExtId = can_id;
    tx_header.IDE = CAN_ID_EXT;     // MUST use extended 29-bit ID
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = data_length;
    tx_header.TransmitGlobalTime = DISABLE;

    /* Add to Tx Mailbox */
    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, (uint8_t*)data, &mailbox) != HAL_OK) {
        return false;
    }

    return true;
}

/* Check for and receive a CAN message */
bool J1939_CAN_Hardware_Receive(uint32_t* pgn, 
                               uint8_t* data, 
                               uint8_t* data_length, 
                               uint8_t* source_address) {
    if (pgn == NULL || data == NULL || data_length == NULL || source_address == NULL) {
        return false;
    }

    CAN_RxHeaderTypeDef rx_header;

    /* Verify if message exists in FIFO0 */
    if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0) {
        return false;
    }

    /* Retrieve message */
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx_header, data) != HAL_OK) {
        return false;
    }

    /* Ensure it is an Extended 29-bit J1939 CAN frame */
    if (rx_header.IDE == CAN_ID_EXT) {
        uint32_t can_id = rx_header.ExtId;
        uint8_t pf = (can_id >> 16) & 0xFF;
        uint8_t ps = (can_id >> 8) & 0xFF;
        uint8_t dp = (can_id >> 24) & 0x01;

        *source_address = can_id & 0xFF;
        *data_length = rx_header.DLC;

        /* Reconstruct PGN from ID fields */
        if (pf < 240) {
            *pgn = (dp << 16) | (pf << 8); // Destination address in PS is masked to 0 for PGN
        } else {
            *pgn = (dp << 16) | (pf << 8) | ps;
        }

        return true;
    }

    return false;
}
