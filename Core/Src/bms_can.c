/**
  ******************************************************************************
  * @file           : bms_can.c
  * @brief          : Battery J1939/CAN Jargon implementation using Hardware Filters
  ******************************************************************************
  * Note: We configure "Accept-All" filters so ALL battery traffic goes to FIFO 0, 
  * which triggers HAL_CAN_RxFifo0MsgPendingCallback(), copying to `Queue_CANRx`.
  * `Task_Poll` lazily parses this queue without blocking.
  */

#include "bms_can.h"
#include <string.h>

extern CAN_HandleTypeDef hcan1;

// The thread-safe Queue holding incoming traffic 
osMessageQueueId_t queueCanRxHandle;
const osMessageQueueAttr_t queueCanRx_attributes = {
  .name = "Queue_CANRx"
};

/**
 * @brief  Initialize the Filters, the RTOS Queue, and Start the Hardware!
 */
HAL_StatusTypeDef BMS_Init(void) {
    // 1. Setup the Hardware Filters
    CAN_FilterTypeDef canfilterconfig;

    canfilterconfig.FilterActivation = CAN_FILTER_ENABLE;
    canfilterconfig.FilterBank = 0;              // Use bank 0 for CAN1 
    canfilterconfig.FilterFIFOAssignment = CAN_RX_FIFO0; 
    
    // Accept EVERYTHING (Mask all zeros). 
    // If you wanted to only accept ID 0x356, you would set MaskIdHigh to 0xFFFF 
    canfilterconfig.FilterIdHigh = 0x0000;
    canfilterconfig.FilterIdLow = 0x0000;
    canfilterconfig.FilterMaskIdHigh = 0x0000;
    canfilterconfig.FilterMaskIdLow = 0x0000;
    
    canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;
    canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;

    if (HAL_CAN_ConfigFilter(&hcan1, &canfilterconfig) != HAL_OK) {
        return HAL_ERROR;
    }

    // 2. Setup the FreeRTOS Queue for safe asynchronous storage
    queueCanRxHandle = osMessageQueueNew(CAN_RX_QUEUE_SIZE, sizeof(CanFrame_t), &queueCanRx_attributes);
    if (queueCanRxHandle == NULL) {
        return HAL_ERROR;
    }

    // 3. Kickstart the CAN silicon into "Normal Mode" (Participating on the bus)
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return HAL_ERROR;
    }

    // 4. Activate the explicit FIFO 0 Interrupt so FreeRTOS wakes up on arrival
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  Attempts to locate a Free Mailbox and pushes a frame out.
 */
HAL_StatusTypeDef BMS_Send_Command(uint32_t id, const uint8_t *payload, uint8_t len, uint8_t isExtended) {
    CAN_TxHeaderTypeDef TxHeader;
    uint32_t TxMailbox; // Silicon tells us which of the 3 mailboxes it used

    if (isExtended) {
        TxHeader.ExtId = id;
        TxHeader.IDE = CAN_ID_EXT;
    } else {
        TxHeader.StdId = id;
        TxHeader.IDE = CAN_ID_STD;
    }

    TxHeader.RTR = CAN_RTR_DATA;
    TxHeader.DLC = (len > 8) ? 8 : len; // Clamp to 8 bytes max
    TxHeader.TransmitGlobalTime = DISABLE;

    // Wait efficiently for an empty mailbox (if bus is congested) vs returning Fail
    int retries = 5;
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0 && retries > 0) {
        osDelay(1); 
        retries--;
    }

    if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, payload, &TxMailbox) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  De-queues received CAN frames from Task_Poll.
 *         (Zero blocking timeout so Task_Poll operates at lightning speed).
 */
void BMS_Process_Incoming_Traffic(BatteryState_t *outState) {
    CanFrame_t rxFrame;

    // Pop everything from the Queue until empty, without putting CPU to sleep
    while (osMessageQueueGet(queueCanRxHandle, &rxFrame, NULL, 0) == osOK) {
        
        switch (rxFrame.id) {
            case BMS_ID_VOLTAGE_CURRENT: // 0x0356 (Example 11-Bit ID)
                // Payload parsing (Little Endian example used by PylonTech/Victron)
                if (rxFrame.dlc >= 4) {
                    outState->voltage_100mV = (rxFrame.data[1] << 8) | rxFrame.data[0];
                    outState->current_100mA = (int16_t)((rxFrame.data[3] << 8) | rxFrame.data[2]);
                }
                break;

            case BMS_ID_SOC_SOH:         // 0x0355
                if (rxFrame.dlc >= 2) {
                    outState->soc_percentage = rxFrame.data[0];
                    outState->soh_percentage = rxFrame.data[1];
                }
                break;

            case BMS_ID_LIMITS:          // 0x035A
                if (rxFrame.dlc >= 4) {
                    outState->max_charge_limit_W = (rxFrame.data[1] << 8) | rxFrame.data[0];
                    outState->max_discharge_limit_W = (rxFrame.data[3] << 8) | rxFrame.data[2];
                }
                break;

            default:
                // Unmapped background traffic, safely ignored!
                break;
        }
    }
}
