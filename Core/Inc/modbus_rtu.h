/**
 * @file modbus_rtu.h
 * @brief FreeRTOS-safe RS-485 Modbus bridging layer for EMS
 */

#ifndef MODBUS_RTU_H_
#define MODBUS_RTU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os2.h"

/* --- MODBUS CONFIGURATION SETTINGS --- */

// The maximum Modbus application frame length (+ CRC buffer)
#define MODBUS_MAX_FRAMELEN 256

// RTOS Timeout values
#define MODBUS_POLL_TIMEOUT_MS 250    // Time to wait for Slave to respond

// Function Codes
#define FC_READ_HOLDING_REGS   0x03
#define FC_READ_INPUT_REGS     0x04
#define FC_WRITE_SINGLE_REG    0x06
#define FC_WRITE_MULTIPLE_REGS 0x10

/* --- INTERFACE ENUMERATIONS --- */
typedef enum {
    MODBUS_BUS_GRID = 0,    // USART1 (Grid Meter)
    MODBUS_BUS_INVERTER     // USART2 (Solar/Battery Inverters)
} ModbusBus_t;

typedef enum {
    MODBUS_ERR_OK = 0,
    MODBUS_ERR_TIMEOUT,
    MODBUS_ERR_CRC,
    MODBUS_ERR_BAD_SLAVE_ID,
    MODBUS_ERR_TX_FAIL
} ModbusStatus_t;

/* --- EXPORTED TYPES --- */

/**
 * Handle structure describing the physical bounds
 * of a managed RS-485 interface connection.
 */
typedef struct {
    ModbusBus_t       busId;
    UART_HandleTypeDef *huart;       // e.g. &huart1
    GPIO_TypeDef       *dePort;      // e.g. RS485_1_DE_GPIO_Port
    uint16_t           dePin;        // e.g. RS485_1_DE_Pin
    
    // Memory arrays for DMA background movement
    uint8_t txBuffer[MODBUS_MAX_FRAMELEN];
    uint8_t rxBuffer[MODBUS_MAX_FRAMELEN];
    uint16_t rxBytesReceived;
    
    // Synchronization Handles
    osSemaphoreId_t rxCompleteSem;   // Released by USART ISR when frame finishes 
} ModbusInterface_t;

/* --- EXPORTED FUNCTIONS --- */

/**
 * @brief Initialize all mapped Modbus Interfaces. Creates Semaphores.
 */
void Modbus_Init(void);

/**
 * @brief Thread-safe polling request over RS-485. 
 * This puts the calling task to sleep (waits on semaphore) 
 * until the slave responds or times out.
 */
ModbusStatus_t Modbus_Read_Registers(
        ModbusBus_t targetBus, 
        uint8_t slaveId, 
        uint8_t functionCode, 
        uint16_t startAddress, 
        uint16_t numRegisters, 
        uint8_t *outputDataBuffer
);


#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H_ */
