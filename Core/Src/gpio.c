/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : FPC18_Pin FPC17_Pin FPC16_Pin FPC15_Pin
                           FPC14_Pin FPC06_Pin FPC05_Pin FPC26_Pin
                           FPC25_Pin FPC24_Pin */
  GPIO_InitStruct.Pin = FPC18_Pin|FPC17_Pin|FPC16_Pin|FPC15_Pin
                          |FPC14_Pin|FPC06_Pin|FPC05_Pin|FPC26_Pin
                          |FPC25_Pin|FPC24_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : FPC13_Pin FPC12_Pin FPC11_Pin FPC10_Pin
                           FPC09_Pin FPC08_Pin FPC07_Pin */
  GPIO_InitStruct.Pin = FPC13_Pin|FPC12_Pin|FPC11_Pin|FPC10_Pin
                          |FPC09_Pin|FPC08_Pin|FPC07_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : FPC04_Pin FPC03_Pin FPC02_Pin FPC01_Pin
                           FPC23_Pin FPC22_Pin FPC21_Pin FPC20_Pin
                           FPC19_Pin */
  GPIO_InitStruct.Pin = FPC04_Pin|FPC03_Pin|FPC02_Pin|FPC01_Pin
                          |FPC23_Pin|FPC22_Pin|FPC21_Pin|FPC20_Pin
                          |FPC19_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
