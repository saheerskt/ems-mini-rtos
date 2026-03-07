/**
  ******************************************************************************
  * @file           : modbus_rtu.c
  * @brief          : Modbus Hardware Bridging for RTOS
  ******************************************************************************
  * Note: Modbus CRC calculations, DMA handling, and OS blocking logic.
  */

#include "modbus_rtu.h"
#include <string.h>

// External UART Handles from main.c Auto-generation
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

// The configured Modbus interface buses
ModbusInterface_t Modbus_GridMeter;
ModbusInterface_t Modbus_Inverters;

/* Private Function Prototypes */
static uint16_t Modbus_CalculateCRC(uint8_t *buffer, uint16_t length);

/**
 * @brief Initialize all mapped Modbus Interfaces. Creates Semaphores.
 */
void Modbus_Init(void) {
    // 1. Setup the Grid Meter Interface (USART1, Pins PA9/PA10, DE on PA8)
    Modbus_GridMeter.busId         = MODBUS_BUS_GRID;
    Modbus_GridMeter.huart         = &huart1;
    Modbus_GridMeter.dePort        = RS485_1_DE_GPIO_Port;
    Modbus_GridMeter.dePin         = RS485_1_DE_Pin;
    // Create the blocking OS Semaphore
    Modbus_GridMeter.rxCompleteSem = osSemaphoreNew(1, 0, NULL);
    
    // We immediately turn OFF the Driver Exable (Low) so we sit in RECEIVE mode
    HAL_GPIO_WritePin(Modbus_GridMeter.dePort, Modbus_GridMeter.dePin, GPIO_PIN_RESET);

    // 2. Setup the Inverter Interface (USART2, Pins PA2/PA3, DE on PD4)
    Modbus_Inverters.busId         = MODBUS_BUS_INVERTER;
    Modbus_Inverters.huart         = &huart2;
    Modbus_Inverters.dePort        = RS485_2_DE_GPIO_Port;
    Modbus_Inverters.dePin         = RS485_2_DE_Pin;
    // Create the blocking OS Semaphore
    Modbus_Inverters.rxCompleteSem = osSemaphoreNew(1, 0, NULL);

    HAL_GPIO_WritePin(Modbus_Inverters.dePort, Modbus_Inverters.dePin, GPIO_PIN_RESET);
}

/**
  * @brief  Standard Modbus CRC-16 Calculation
  */
static uint16_t Modbus_CalculateCRC(uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)buffer[i];
        for (uint8_t j = 8; j != 0; j--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Thread-safe polling request over RS-485 using DMA and IDLE Interrupts.
 */
ModbusStatus_t Modbus_Read_Registers(
        ModbusBus_t targetBus, 
        uint8_t slaveId, 
        uint8_t functionCode, 
        uint16_t startAddress, 
        uint16_t numRegisters, 
        uint8_t *outputDataBuffer) 
{
    // 1. Select the correct physical interface configuration
    ModbusInterface_t *interface;
    if (targetBus == MODBUS_BUS_GRID) {
        interface = &Modbus_GridMeter;
    } else {
        interface = &Modbus_Inverters;
    }

    // 2. Build the exact Modbus RTU byte frame in our transmit buffer
    interface->txBuffer[0] = slaveId;
    interface->txBuffer[1] = functionCode;
    interface->txBuffer[2] = (startAddress >> 8) & 0xFF; // Address High
    interface->txBuffer[3] = startAddress & 0xFF;        // Address Low
    interface->txBuffer[4] = (numRegisters >> 8) & 0xFF; // Count High
    interface->txBuffer[5] = numRegisters & 0xFF;        // Count Low
    
    // Attach the 16-bit CRC to the end of the frame (Little-Endian)
    uint16_t crc = Modbus_CalculateCRC(interface->txBuffer, 6);
    interface->txBuffer[6] = crc & 0xFF;
    interface->txBuffer[7] = (crc >> 8) & 0xFF;
    
    uint16_t requestLength = 8;
    
    // 3. Clear our binary semaphore exactly before starting, in case it was given accidentally 
    while (osSemaphoreAcquire(interface->rxCompleteSem, 0) == osOK);

    // 4. Assert DE (Driver Enable) High -> we are now controlling the RS485 bus natively
    HAL_GPIO_WritePin(interface->dePort, interface->dePin, GPIO_PIN_SET);
    
    // Tiny forced delay for the physical MAX3485 chip to fully lock the bus
    for(volatile int i=0; i<300; i++); 

    // 5. Fire off the DMA (Direct Memory Access)! 
    // This tells the silicon to automatically stream these 8 bytes out the UART TX pin in the background.
    if (HAL_UART_Transmit_DMA(interface->huart, interface->txBuffer, requestLength) != HAL_OK) {
        HAL_GPIO_WritePin(interface->dePort, interface->dePin, GPIO_PIN_RESET);
        return MODBUS_ERR_TX_FAIL;
    }

    // --- NON-BLOCKING HANDOFF ---
    // The CPU is now completely free. 
    // The very microsecond the last bit leaves, the HAL_UART_TxCpltCallback() interrupt handles 
    // dropping the DE pin back Low so we can hear the slave.
    // We also arm the DMA to silently receive the incoming slave response until the line goes IDLE.

    // 6. Put the calling RTOS task (e.g. Task_Poll) to sleep exactly here.
    // When the UART IDLE interrupt decides a complete frame has arrived, it will "Give" this semaphore and wake us.
    osStatus_t waitStatus = osSemaphoreAcquire(interface->rxCompleteSem, pdMS_TO_TICKS(MODBUS_POLL_TIMEOUT_MS));

    if (waitStatus == osErrorTimeout) {
        // If the 250ms timeout expires and the semaphore wasn't given, the slave is dead/disconnected.
        // Stop DMA to clean up.
        HAL_UART_AbortReceive(interface->huart);
        return MODBUS_ERR_TIMEOUT;
    }

    // 7. WE ARE AWAKE AND HAVE DATA!
    // The total bytes received will have been logged by the IDLE interrupt handler.
    if (interface->rxBytesReceived < 5) return MODBUS_ERR_CRC;

    // Check the Slave ID
    if (interface->rxBuffer[0] != slaveId) return MODBUS_ERR_BAD_SLAVE_ID;
    
    // Extract the raw payload from the frame (after Slave ID, Function, ByteCount)
    uint8_t payloadBytes = interface->rxBuffer[2];
    
    // Calculate CRC over the received frame (excluding the last 2 CRC bytes itself)
    uint16_t calcCrc = Modbus_CalculateCRC(interface->rxBuffer, interface->rxBytesReceived - 2);
    uint16_t recvCrc = interface->rxBuffer[interface->rxBytesReceived - 2] | 
                      (interface->rxBuffer[interface->rxBytesReceived - 1] << 8);

    if (calcCrc != recvCrc) {
        return MODBUS_ERR_CRC;
    }

    // Everything is valid! Copy the decoded integer payload bytes back to the caller's buffer 
    memcpy(outputDataBuffer, &interface->rxBuffer[3], payloadBytes);

    return MODBUS_ERR_OK;
}
