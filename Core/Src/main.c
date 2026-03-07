/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "modbus_rtu.h"
#include "bms_can.h"
#include "mqtt_ota.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* Definitions for Task_Net */
osThreadId_t netTaskHandle;
const osThreadAttr_t netTask_attributes = {
  .name = "Task_Net",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for Task_Ctrl */
osThreadId_t ctrlTaskHandle;
const osThreadAttr_t ctrlTask_attributes = {
  .name = "Task_Ctrl",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Definitions for Task_Poll */
osThreadId_t pollTaskHandle;
const osThreadAttr_t pollTask_attributes = {
  .name = "Task_Poll",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};

/* Definitions for Queue_Cmd */
osMessageQueueId_t queueCmdHandle;
const osMessageQueueAttr_t queueCmd_attributes = {
  .name = "Queue_Cmd"
};

/* Definitions for Queue_Data */
osMessageQueueId_t queueDataHandle;
const osMessageQueueAttr_t queueData_attributes = {
  .name = "Queue_Data"
};
/* USER CODE BEGIN PV */
// Global thread-safe telemetry reflection for the 5-second MQTT publisher
volatile SystemState_t latest_system_state = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CAN1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);
void StartNetTask(void *argument);
void StartCtrlTask(void *argument);
void StartPollTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_CAN1_Init();
  MX_SPI1_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  Modbus_Init();
  BMS_Init();
  NET_Init();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Create the queues for inter-task communication */
  queueCmdHandle = osMessageQueueNew(16, sizeof(CloudCommand_t), &queueCmd_attributes);
  // queueData passes the full assembled system status from Polling up to Control logic
  queueDataHandle = osMessageQueueNew(32, sizeof(SystemState_t), &queueData_attributes);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  netTaskHandle = osThreadNew(StartNetTask, NULL, &netTask_attributes);
  ctrlTaskHandle = osThreadNew(StartCtrlTask, NULL, &ctrlTask_attributes);
  pollTaskHandle = osThreadNew(StartPollTask, NULL, &pollTask_attributes);

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 8;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_16TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_4TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  /* DMA2_Stream7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, W5500_CS_Pin|RS485_1_DE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RS485_2_DE_GPIO_Port, RS485_2_DE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : W5500_CS_Pin RS485_1_DE_Pin */
  GPIO_InitStruct.Pin = W5500_CS_Pin|RS485_1_DE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : W5500_RST_Pin */
  GPIO_InitStruct.Pin = W5500_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(W5500_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RS485_2_DE_Pin */
  GPIO_InitStruct.Pin = RS485_2_DE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RS485_2_DE_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

extern ModbusInterface_t Modbus_GridMeter;
extern ModbusInterface_t Modbus_Inverters;

/**
 * @brief This hardware interrupt fires the instant the final DMA byte fully leaves the silicone UART queue.
 *        We immediately pull the MAX3485 Transceiver 'DE' (Driver Enable) pin LOW so we flip 
 *        the bus from Transmit to Listen before the Slave device starts throwing data back at us.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        HAL_GPIO_WritePin(Modbus_GridMeter.dePort, Modbus_GridMeter.dePin, GPIO_PIN_RESET);
        
        // Listen backwards using Interrupt. It puts every byte into rxBuffer.
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, Modbus_GridMeter.rxBuffer, MODBUS_MAX_FRAMELEN);
        __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT); // Disable half-transfer interrupt
    } 
    else if (huart->Instance == USART2) {
        HAL_GPIO_WritePin(Modbus_Inverters.dePort, Modbus_Inverters.dePin, GPIO_PIN_RESET);
        
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, Modbus_Inverters.rxBuffer, MODBUS_MAX_FRAMELEN);
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}

/**
 * @brief This hardware IDLE interrupt fires when the bus goes completely silent after a stream of bytes.
 *        In Modbus, 3.5 character times of silence strictly means the packet is fully over.
 *        This means the Slave device just stopped transmitting.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        // Record size of the finished packet
        Modbus_GridMeter.rxBytesReceived = Size; 
        
        // Unblock the FreeRTOS Poll_Task instantly!
        osSemaphoreRelease(Modbus_GridMeter.rxCompleteSem);
    } 
    else if (huart->Instance == USART2) {
        Modbus_Inverters.rxBytesReceived = Size;
        osSemaphoreRelease(Modbus_Inverters.rxCompleteSem);
    }
}

extern osMessageQueueId_t queueCanRxHandle;

/**
 * @brief This hardware interrupt fires the instant a CAN frame securely lands in 
 *        the Silicon RX FIFO 0 Mailbox. It unpacks the raw memory registers and 
 *        safely stuffs the frame onto the end of the FreeRTOS Queue_CANRx.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance == CAN1) {
        CAN_RxHeaderTypeDef RxHeader;
        CanFrame_t rxFrame;

        // Fetch frame safely from silicon hardware mailbox into our C-Struct
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, rxFrame.data) == HAL_OK) {
            
            // Unpack 11-Bit or 29-Bit Identifier
            if (RxHeader.IDE == CAN_ID_EXT) {
                rxFrame.id = RxHeader.ExtId;
                rxFrame.isExtended = 1;
            } else {
                rxFrame.id = RxHeader.StdId;
                rxFrame.isExtended = 0;
            }
            
            rxFrame.dlc = RxHeader.DLC; // Data Length (0-8)

            // Instantly shovel into the RTOS queue (Non-Blocking)
            // If the 16-frame queue is totally full, we discard it to prevent crashing the OS.
            osMessageQueuePut(queueCanRxHandle, &rxFrame, 0, 0);
        }
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartNetTask */
/**
  * @brief  Function implementing the Task_Net thread.
  * @param  argument: Not used
  * @retval None
  * @note   This handles the W5500 physical MAC/PHY logic over SPI and publishes to Mosquitto.
  */
/* USER CODE END Header_StartNetTask */
void StartNetTask(void *argument)
{
  /* USER CODE BEGIN StartNetTask */

  // This function drives the entire MQTT polling and parsing loop
  // It takes a pointer to the global SystemState so it can snapshot it every 5s
  NET_Process_MQTT((SystemState_t*)&latest_system_state);

  /* USER CODE END StartNetTask */
}

/* USER CODE BEGIN Header_StartCtrlTask */
/**
  * @brief  Function implementing the Task_Ctrl thread.
  * @param  argument: Not used
  * @retval None
  * @note   This task executes the INSTANTANEOUS LOAD BALANCING (Peak-Shaving) logic!
  *         It rests at 0% CPU until Task_Poll drops a fresh SystemState_t into the Queue.
  */
/* USER CODE END Header_StartCtrlTask */
void StartCtrlTask(void *argument)
{
  /* USER CODE BEGIN StartCtrlTask */
  SystemState_t incomingData;
  CloudCommand_t incomingCmd;
  int32_t commandInverterTargetPower_W = 0; // Positive = Charge Bat, Negative = Discharge Bat

  // Keep a local copy of limits that Cloud can overwrite
  int32_t activeGridLimitW = MAX_GRID_IMPORT_W;

  /* Infinite loop */
  for(;;)
  {
    // 1. Process any Top-Down Cloud commands (Priority overrides)
    if(osMessageQueueGet(queueCmdHandle, &incomingCmd, NULL, 0) == osOK) {
        if (incomingCmd.type == CMD_UPDATE_GRID_LIMIT) {
            activeGridLimitW = incomingCmd.value; // Dynamically tighten/loosen the import limit!
        }
        else if (incomingCmd.type == CMD_OTA_START) {
            NET_Run_OTA_Download("http://192.168.1.50/firmware.bin");
        }
    }

    // 2. Await High-Speed Hardware Telemetry from Bottom-Up
    // osWaitForever means this task literally sleeps indefinitely without burning clock cycles 
    // until `queueDataHandle` gets populated by the RS-485/CAN hardware!
    if(osMessageQueueGet(queueDataHandle, &incomingData, NULL, osWaitForever) == osOK) {
        
        // Update the global visual state for the Network Publisher to steal
        latest_system_state = incomingData;
        
        // --- PEAK SHAVING ALGORITHM --- //
        commandInverterTargetPower_W = 0; // Default to idle 

        // CASE A: We are pulling too much power from the Utility Grid! 
        // We need the Battery to discharge and "Shave" the peak off.
        if (incomingData.gridPowerW > activeGridLimitW) {
            
            // Calculate how much we need the battery to push
            int32_t deltaPowerNeeded = incomingData.gridPowerW - activeGridLimitW;
            
            // Safety constraints: Do not kill the battery or violate BMS limits!
            if (incomingData.batterySoC > MIN_ALLOWABLE_SOC) {
                // Clamp target to the BMS's allowed limit
                if (deltaPowerNeeded > incomingData.maxDischargeLimitW) {
                    deltaPowerNeeded = incomingData.maxDischargeLimitW;
                }
                
                // Discharge command is negative in this convention
                commandInverterTargetPower_W = -deltaPowerNeeded;
            }
        }
        
        // CASE B: Solar is pushing massive excess out to the Grid!
        // We should capture this into the battery instead of exporting it.
        else if (incomingData.gridPowerW < 0) {
            
            // E.g. Grid is -2000W (Exporting 2kW). We want to absorb 2000W.
            int32_t excessPower = -(incomingData.gridPowerW); 
            
            if (incomingData.batterySoC < 100) {
                // Clamp target to BMS allowed limit
                if (excessPower > incomingData.maxChargeLimitW) {
                    excessPower = incomingData.maxChargeLimitW;
                }
                commandInverterTargetPower_W = excessPower; // Positive = Charge
            }
        }

        // --- EXECUTE COMMAND --- //
        if (commandInverterTargetPower_W != 0) {
            // In reality, this would be a Modbus WRITE holding register command 
            // over USART2 to tell the Solar/Battery Inverter what to do!
            // Modbus_Write_Register(MODBUS_BUS_INVERTER, ... commandInverterTargetPower_W);
        }

    } // End Queue Data Receive Block
  }   // End Infinite Loop
  /* USER CODE END StartCtrlTask */
}

/* USER CODE BEGIN Header_StartPollTask */
/**
  * @brief  Function implementing the Task_Poll thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartPollTask */
void StartPollTask(void *argument)
{
  /* USER CODE BEGIN StartPollTask */
  uint8_t rxData[16] = {0};
  
  // Persistent structural models holding raw parsed byte data
  BatteryState_t bmsData = {
    .soc_percentage = 50,           // Fallback defaults
    .maxDischargeLimitW = 3000,
    .maxChargeLimitW = 3000
  }; 
  SystemState_t liveSystem;         // Assembled payload for Control task

  /* Infinite loop */
  for(;;)
  {
    /* --- 1. PROCESS ALWAYS-ON CAN BUS TRAFFIC --- */
    // Empty the `Queue_CANRx` instantly into the `bmsData` struct if the batteries broadcasted.
    // This does completely 0 CPU blocking. It only takes nanoseconds if the queue is empty.
    BMS_Process_Incoming_Traffic(&bmsData);


    /* --- 2. TRIGGER SEQUENTIAL MODBUS READS --- */
    // Example: Read 2 holding registers (4 bytes) from Grid Meter (Slave ID 1) at Address 0x0118 (Grid Power).
    // This call puts this task to sleep (Yields to OS) until the MAX3485 finishes the hardware transaction!
    ModbusStatus_t status = Modbus_Read_Registers(
        MODBUS_BUS_GRID, 
        1, 
        FC_READ_HOLDING_REGS, 
        0x0118,  // Hypothetical address for Total System Power (W)
        2, 
        rxData
    );

    if (status == MODBUS_ERR_OK) {
        // Valid hardware response! Unpack physical bytes to Signed Integer (W)
        int32_t meterPowerW = (int32_t)((rxData[0] << 24) | (rxData[1] << 16) | (rxData[2] << 8) | rxData[3]);
        
        // Assemble consolidated state
        liveSystem.gridPowerW         = meterPowerW;
        liveSystem.batterySoC         = bmsData.soc_percentage;
        liveSystem.maxChargeLimitW    = bmsData.maxChargeLimitW;
        liveSystem.maxDischargeLimitW = bmsData.maxDischargeLimitW;

        /* STATE_PUBLISH: Pass the complete package into Queue_Data to instantly trigger Peak Shaving! */
        osMessageQueuePut(queueDataHandle, &liveSystem, 0, 0);
    } 
    else {
        // Handle physical bridge error (timeout, crc fail)
    }

    // Hard real-time polling delay
    osDelay(300); // Poll meters every 300ms (Very fast control loop)
  }
  /* USER CODE END StartPollTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
