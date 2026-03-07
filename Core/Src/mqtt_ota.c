/**
  ******************************************************************************
  * @file           : mqtt_ota.c
  * @brief          : W5500 SPI Ethernet MAC/PHY bridging, JSON serialization, and STM32 Flash programming.
  ******************************************************************************
  */

#include "mqtt_ota.h"
#include <stdio.h>
#include <string.h>

extern SPI_HandleTypeDef hspi1;
extern osMessageQueueId_t queueCmdHandle;

// Global buffer for JSON string framing without risking stack overflows
static char mqttPayloadBuffer[256];

/**
 * @brief Initialize the physical SPI W5500 Layer and DHCP client.
 */
void NET_Init(void) {
    // 1. Hardware Reset sequence
    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);
    osDelay(10); // 10ms hard delay
    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_SET);
    osDelay(10); // Allow PHY to boot up
    
    // 2. Here: Register SPI Write/Read callbacks into the official WIZnet ioLibrary_Driver
    // reg_wizchip_spi_cbfunc(wizchip_spi_readbyte, wizchip_spi_writebyte);
    // reg_wizchip_cs_cbfunc(wizchip_cs_select, wizchip_cs_deselect);

    // 3. Obtain IP via DHCP or Static Settings (Placeholder)
    // DHCP_init(0, dhcp_buffer);
    // DHCP_run();
}

/**
 * @brief FreeRTOS Task body simulating connection to MQTT Cloud.
 */
void NET_Process_MQTT(const SystemState_t *globalStatePtr) {
    // 1. Establish TCP Socket to MQTT_BROKER_IP
    // int sockStatus = connect(1, brokerIP, MQTT_BROKER_PORT);
    
    // 2. We are online!
    uint32_t lastPublishTime = osKernelGetTickCount();
    
    while(1) {
        // --- 1. HANDLE INBOUND DOWNLINK DE-QUEUEING (SUBSCRIBE) --- //
        // Example: The W5500 TCP stack receives a packet triggering an internal RX ISR.
        // The MQTT parser yields `{"cmd":"limit", "val": 4000}`.
        // We unpack it here and shove it into `Queue_Cmd` to instantly alert Task_Ctrl!
        
        // Pseudo-logic representing what happens when a payload arrives from Cloud:
        uint8_t cloudPktReceived = 0; 
        if (cloudPktReceived) {
            CloudCommand_t clCmd;
            clCmd.type = CMD_UPDATE_GRID_LIMIT;
            clCmd.value = 4000;
            osMessageQueuePut(queueCmdHandle, &clCmd, 0, 0); // Dispatches upwards!
        }
        

        // --- 2. HANDLE OUTBOUND UPLINK TELEMETRY (PUBLISH) --- //
        uint32_t now = osKernelGetTickCount();
        if ((now - lastPublishTime) >= pdMS_TO_TICKS(5000)) { // Every 5 Seconds
            
            // Build the JSON payload to squirt into the remote Mosquitto Server
            // %lu is long unsigned, %li is long int
            snprintf(mqttPayloadBuffer, sizeof(mqttPayloadBuffer), 
                "{\"gridW\": %li, \"soc\": %u, \"maxChg\": %u, \"maxDis\": %u}",
                globalStatePtr->gridPowerW, 
                globalStatePtr->batterySoC,
                globalStatePtr->maxChargeLimitW,
                globalStatePtr->maxDischargeLimitW
            );

            // Transmit via MQTT Publisher 
            // mqtt_publish(&mqttClient, MQTT_TOPIC_TELEMETRY, mqttPayloadBuffer, strlen(mqttPayloadBuffer), QoS_0);

            lastPublishTime = now;
        }
        
        // Prevent starvation of other RTOS Tasks. The networking stack doesn't need 100% CPU.
        osDelay(250);
    }
}

/**
 * @brief OTA Downloading mechanism mapping the HTTP stream into HAL Flash memory!
 */
void NET_Run_OTA_Download(const char* url) {
    // NOTE: FreeRTOS MUST suspend other heavy tasks before hitting Flash or you will segfault!
    vTaskSuspendAll();
    
    // 1. Unlock STM32 Flash Controller
    HAL_FLASH_Unlock();
    
    // 2. Erase the Secondary Bank 2 
    FLASH_EraseInitTypeDef EraseConfig;
    uint32_t SectorError = 0;
    
    EraseConfig.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseConfig.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseConfig.Sector = FLASH_SECTOR_6; // Arbitrary 0x08040000 mapping
    EraseConfig.NbSectors = 4;           // E.g. Sectors 6,7,8,9 (512 KB total payload space)
    
    if (HAL_FLASHEx_Erase(&EraseConfig, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        xTaskResumeAll();
        return; // Flash erase failed
    }

    // 3. HTTP Download Loop -> Stream into `FLASH_BANK2_START` 1 byte at a time
    uint32_t flashPointer = FLASH_BANK2_START;
    uint8_t chunkBuffer[1024]; // 1KB HTTP Body Chunk
    
    /* Imagine this loop is requesting http packets and getting chunkBuffer back
    while (downloadSizeRemaining > 0) {
        for (int i=0; i < currentChunkSize; i++) {
            // Write 1 byte to the silicon Flash Memory Address offset.
            // MUST include the cryptographically signed payload (Secure Boot).
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, flashPointer, chunkBuffer[i]);
            flashPointer++;
        }
    }
    */

    // 4. Lock Flash and Write the MCUboot Verification Magic string trailer!
    // Note: To implement true Secure Boot, this OTA image MUST be signed (e.g. ECDSA P-256)
    // using `imgtool` during our CI/CD pipeline before it is downloaded over MQTT/HTTP.
    // The downloaded image contains the signature. We write the MCUboot magic trailer 
    // to tell the Bootloader that a new image is pending validation in Bank 2.
    HAL_FLASH_Lock();
    
    // 5. Hard Reset CPU! 
    // It will boot into MCUboot (0x08000000). MCUboot will use its embedded Public Key
    // to cryptographically verify the ECDSA signature of the Bank 2 image.
    // If the signature is invalid (tampered or unauthorized firmware), MCUboot aborts 
    // the upgrade, preventing malicious code execution (Secure Boot Enforcement).
    NVIC_SystemReset();
}
