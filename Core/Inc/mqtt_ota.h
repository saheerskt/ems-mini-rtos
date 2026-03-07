/**
 * @file mqtt_ota.h
 * @brief FreeRTOS-safe MQTT Networking and OTA Updater using W5500 SPI Ethernet
 */

#ifndef MQTT_OTA_H_
#define MQTT_OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os2.h"

/* --- CLOUD COMMS DEFINITIONS --- */
#define MQTT_BROKER_IP      "192.168.1.100" // Example
#define MQTT_BROKER_PORT    1883
#define MQTT_TOPIC_TELEMETRY "ems/mini1/telemetry"
#define MQTT_TOPIC_COMMAND   "ems/mini1/command"
#define MQTT_TOPIC_OTA       "ems/mini1/ota"

/* --- OTA FLASH PARTITIONING DEFINITIONS --- */
// STM32F407 1MB Flash is sector-based. Sector 6 is 128KB, starting at 0x08040000.
// Let's assume MCUboot takes Sectors 0-3 (64KB). 
#define FLASH_BANK1_START        0x08040000 // Active RTOS Application
#define FLASH_BANK2_START        0x080A0000 // Download/OTA Secondary Slot
#define FLASH_SECTOR_SIZE        (128 * 1024)

/* --- EXPORTED TYPES --- */

/**
 * Commands dispatched from the MQTT Cloud downwards to the logic engine.
 */
typedef enum {
    CMD_UPDATE_GRID_LIMIT = 1,
    CMD_FORCE_CHARGE,
    CMD_FORCE_DISCHARGE,
    CMD_SYSTEM_REBOOT,
    CMD_OTA_START
} CloudCommandType_t;

/**
 * The packet structure sent through Queue_Cmd to Task_Ctrl.
 */
typedef struct {
    CloudCommandType_t type;
    int32_t            value; // e.g., the new MAX_GRID_IMPORT_W
} CloudCommand_t;

/* --- EXPORTED FUNCTIONS --- */

/**
 * @brief Initialize the W5500 Ethernet MAC/PHY and establish TCP/MQTT connection.
 */
void NET_Init(void);

/**
 * @brief Main execution loop for the networking sub-engine. Connects to the Cloud,
 *        publishes standard telemetry, and listens for inbound commands or OTA triggers.
 *        This function blocks (sleeps efficiently) and drives Task_Net.
 * @param globalStatePtr: Const pointer to read the latest factory telemetry.
 */
void NET_Process_MQTT(const SystemState_t *globalStatePtr);

/**
 * @brief OTA Download State Machine. Opens an HTTP connect, chunks the binary into Flash Bank 2,
 *        verifies Checksum, and reboots into MCUboot.
 * @param url: The cloud URL to the .bin file.
 */
void NET_Run_OTA_Download(const char* url);


#ifdef __cplusplus
}
#endif

#endif /* MQTT_OTA_H_ */
