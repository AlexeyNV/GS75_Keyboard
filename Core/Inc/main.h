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

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define FPC18_Pin GPIO_PIN_13
#define FPC18_GPIO_Port GPIOC
#define FPC17_Pin GPIO_PIN_0
#define FPC17_GPIO_Port GPIOC
#define FPC16_Pin GPIO_PIN_1
#define FPC16_GPIO_Port GPIOC
#define FPC15_Pin GPIO_PIN_2
#define FPC15_GPIO_Port GPIOC
#define FPC14_Pin GPIO_PIN_3
#define FPC14_GPIO_Port GPIOC
#define FPC13_Pin GPIO_PIN_0
#define FPC13_GPIO_Port GPIOA
#define FPC12_Pin GPIO_PIN_1
#define FPC12_GPIO_Port GPIOA
#define FPC11_Pin GPIO_PIN_2
#define FPC11_GPIO_Port GPIOA
#define FPC10_Pin GPIO_PIN_4
#define FPC10_GPIO_Port GPIOA
#define FPC09_Pin GPIO_PIN_5
#define FPC09_GPIO_Port GPIOA
#define FPC08_Pin GPIO_PIN_6
#define FPC08_GPIO_Port GPIOA
#define FPC07_Pin GPIO_PIN_7
#define FPC07_GPIO_Port GPIOA
#define FPC06_Pin GPIO_PIN_4
#define FPC06_GPIO_Port GPIOC
#define FPC05_Pin GPIO_PIN_5
#define FPC05_GPIO_Port GPIOC
#define FPC04_Pin GPIO_PIN_0
#define FPC04_GPIO_Port GPIOB
#define FPC03_Pin GPIO_PIN_1
#define FPC03_GPIO_Port GPIOB
#define FPC02_Pin GPIO_PIN_2
#define FPC02_GPIO_Port GPIOB
#define FPC01_Pin GPIO_PIN_10
#define FPC01_GPIO_Port GPIOB
#define FPC26_Pin GPIO_PIN_10
#define FPC26_GPIO_Port GPIOC
#define FPC25_Pin GPIO_PIN_11
#define FPC25_GPIO_Port GPIOC
#define FPC24_Pin GPIO_PIN_12
#define FPC24_GPIO_Port GPIOC
#define FPC23_Pin GPIO_PIN_5
#define FPC23_GPIO_Port GPIOB
#define FPC22_Pin GPIO_PIN_6
#define FPC22_GPIO_Port GPIOB
#define FPC21_Pin GPIO_PIN_7
#define FPC21_GPIO_Port GPIOB
#define FPC20_Pin GPIO_PIN_8
#define FPC20_GPIO_Port GPIOB
#define FPC19_Pin GPIO_PIN_9
#define FPC19_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
