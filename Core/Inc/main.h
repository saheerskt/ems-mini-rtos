/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
/**
 * @brief Unified container holding the real-time physical state of the hardware.
 *        Passed from the fast hardware Polling Task to the logic Control Task.
 */
typedef struct {
    int32_t   gridPowerW;           // Negative = Exporting to grid, Positive = Importing
    uint8_t   batterySoC;           // 0-100%
    uint16_t  maxDischargeLimitW;   // Broadcasted by BMS CAN
    uint16_t  maxChargeLimitW;      // Broadcasted by BMS CAN
} SystemState_t;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
// Hardcoded Logic Limits (Without Cloud override)
#define MAX_GRID_IMPORT_W    3000   // Try to keep house from pulling > 3kW from utility
#define MIN_ALLOWABLE_SOC    15     // Stop discharging if battery drops below 15%
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define W5500_CS_Pin GPIO_PIN_4
#define W5500_CS_GPIO_Port GPIOA
#define W5500_RST_Pin GPIO_PIN_4
#define W5500_RST_GPIO_Port GPIOC
#define RS485_1_DE_Pin GPIO_PIN_8
#define RS485_1_DE_GPIO_Port GPIOA
#define RS485_2_DE_Pin GPIO_PIN_4
#define RS485_2_DE_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
