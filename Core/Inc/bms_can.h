/**
 * @file bms_can.h
 * @brief FreeRTOS-safe CAN Bus bridging layer for EMS Battery modules
 */

#ifndef BMS_CAN_H_
#define BMS_CAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os2.h"

/* --- BATTERY CAN CONFIGURATION --- */

// Define the maximum number of unread incoming battery frames we can hold in RAM
// before dropping data (preventing FreeRTOS heap exhaustion).
#define CAN_RX_QUEUE_SIZE 16

/* --- COMMON BATTERY PGN/IDs (J1939 or Custom) --- */
#define BMS_ID_VOLTAGE_CURRENT  0x0356
#define BMS_ID_SOC_SOH          0x0355
#define BMS_ID_ALARM_WARN       0x0359
#define BMS_ID_LIMITS           0x035A

/* --- EXPORTED TYPES --- */

/**
 * A lightweight structure to hold a received CAN frame
 * as it travels through the FreeRTOS Queue from the ISR.
 */
typedef struct {
    uint32_t  id;            // Extended or Standard Identifier
    uint8_t   data[8];       // The up-to-8-byte payload
    uint8_t   dlc;           // Data Length Code (0-8)
    uint8_t   isExtended;    // 1 if 29-bit ID, 0 if 11-bit ID
} CanFrame_t;

/**
 * The internal state structure mapping the parsed state
 * of the physical battery rack.
 */
typedef struct {
    uint16_t  voltage_100mV;
    int16_t   current_100mA;
    uint8_t   soc_percentage;
    uint8_t   soh_percentage;
    uint16_t  max_charge_limit_W;
    uint16_t  max_discharge_limit_W;
} BatteryState_t;

/* --- EXPORTED FUNCTIONS --- */

/**
 * @brief  Initialize the CAN Peripheral, Hardware Filters, and the FreeRTOS RX Queue.
 *         Finally, transitions CAN1 from Init Mode -> Normal Mode to start participating.
 * @retval HAL_StatusTypeDef - HAL_OK if hardware locked successfully.
 */
HAL_StatusTypeDef BMS_Init(void);

/**
 * @brief  Attempts to locate a Free Mailbox in silicon and load an arbitrary packet
 *         onto the CAN bus immediately. Fails instantly if the bus is locked.
 * @param  id: The CAN ID (e.g. 0x421)
 * @param  payload: Pointer to up to 8 bytes of data
 * @param  len: How many bytes to actually send (0 to 8).
 * @param  isExtended: 1 for 29-bit ID, 0 for 11-bit ID.
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef BMS_Send_Command(uint32_t id, const uint8_t *payload, uint8_t len, uint8_t isExtended);

/**
 * @brief  De-queues received messages from the CAN ISR and parses their 
 *         payloads to automatically update the local `BatteryState_t` struct.
 *         This should be called periodically (without blocking) from Task_Poll.
 * @param  outState: A pointer to your struct where updated data is written.
 */
void BMS_Process_Incoming_Traffic(BatteryState_t *outState);


#ifdef __cplusplus
}
#endif

#endif /* BMS_CAN_H_ */
