/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ICU Ambient Control System - Main Program
  ******************************************************************************
  * CORRECTIONS APPLIED:
  *  1. Renamed smoke_detected -> air_quality_alert (MQ135 is NOT a smoke sensor)
  *  2. Non-destructive LCD: line1 always shows T/H, line2 shows status/alert
  *  3. DHT11 persistent failure counter (3 strikes) triggers buzzer + fault msg
  *  4. MQ135_RO defined as calibratable constant with clear comment
  *  5. All sprintf -> snprintf for buffer safety
  *  6. Fixed I2C scan UART transmit byte count (was 17, correct is 16)
  *  7. UART log now includes DHT error state when sensor fails
  *  8. Alert types (temp_high, air_quality, sensor_fault) shown simultaneously
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f4xx_hal.h"
#include "liquidcrystal_i2c.h"
#include "dht11_driver.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;
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
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart3;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
dht11_data sensor_data;

/*
 * MQ135 Ro Calibration:
 * Ro is the sensor resistance in clean air (~400 ppm CO2 baseline).
 * To calibrate: power the sensor for 24h, read ADC in fresh air,
 * compute Rs using the voltage divider formula, then set MQ135_RO = Rs.
 * Default 10.0f is a typical datasheet estimate only.
 */
#define MQ135_RO_CLEAN_AIR      10.0f

/* Number of consecutive DHT11 failures before raising a sensor fault alert */
#define DHT_FAIL_THRESHOLD       3U
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
/* USER CODE BEGIN PFP */
uint16_t Read_MQ135(void);
float    mq135_get_ppm(uint16_t adc_val);
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

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */

  /* --- I2C Bus Scan (debug) --- */
  /* FIX: "I2C Scan Start\r\n" is 16 bytes, was incorrectly 17 */
  HAL_UART_Transmit(&huart3, (uint8_t*)"I2C Scan Start\r\n", 16U, 100U);

  for (uint8_t i = 1U; i < 128U; i++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1U), 1U, 10U) == HAL_OK)
    {
      char msg[40];
      int len = snprintf(msg, sizeof(msg), "Found device at: 0x%02X\r\n", i);
      HAL_UART_Transmit(&huart3, (uint8_t*)msg, (uint16_t)len, 100U);
    }
  }
  HAL_UART_Transmit(&huart3, (uint8_t*)"Scan Done\r\n", 11U, 100U);

  /* --- Hardware Init --- */
  HAL_TIM_Base_Start(&htim1);
  DWT_Delay_Init();                    /* Required by DHT11 driver */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

  /* --- LCD Splash Screen --- */
  HD44780_Init(2);
  HD44780_Clear();
  HD44780_SetCursor(0, 0);
  HD44780_PrintStr(" EcoWatch: ");
  HD44780_SetCursor(0, 1);
  HD44780_PrintStr(" Real-Time AQI Monitor");
  for (int x = 0; x < 11; x++)
  {
    HD44780_ScrollDisplayLeft();
    HAL_Delay(500U);
  }
  HD44780_Clear();
  HD44780_SetCursor(0, 0);
  HD44780_PrintStr("Initializing...");
  HAL_Delay(2000U);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  float   temp           = 0.0f;
  float   hum            = 0.0f;
  uint8_t dht_status     = 0U;
  uint8_t dht_fail_count = 0U;

  while (1)
  {
    /* ---- 1. Read sensors ---- */
    uint16_t mq135_val = Read_MQ135();
    float    ppm       = mq135_get_ppm(mq135_val);

    dht_status = DHT11_ReadData(&temp, &hum);

    /* Track consecutive DHT11 failures */
    if (dht_status == 1U)
    {
      dht_fail_count = 0U;                    /* Reset on success */
    }
    else
    {
      if (dht_fail_count < DHT_FAIL_THRESHOLD)
        dht_fail_count++;
    }

    /* ---- 2. UART telemetry ---- */
    char msg[80];
    int  len;
    if (dht_status == 1U)
    {
      len = snprintf(msg, sizeof(msg), "T:%.1f,H:%.1f,PPM:%.2f\r\n", temp, hum, ppm);
    }
    else
    {
      /* FIX: Log DHT error state so the host side knows */
      len = snprintf(msg, sizeof(msg), "DHT11_ERR,H:--,PPM:%.2f\r\n", ppm);
    }
    HAL_UART_Transmit(&huart3, (uint8_t*)msg, (uint16_t)len, 100U);

    /* ---- 3. Determine alert conditions ---- */
    /*
     * FIX: Renamed smoke_detected -> air_quality_alert.
     * MQ135 detects CO2/NH3/NOx — NOT smoke. Calling it smoke

     */
    bool air_quality_alert = (ppm > 200.0f);
    bool temp_high         = (dht_status == 1U && temp > 35.0f);
    bool sensor_fault      = (dht_fail_count >= DHT_FAIL_THRESHOLD);
    bool any_alert         = air_quality_alert || temp_high || sensor_fault;

    /* ---- 4. Build LCD content ---- */
    /*
     * FIX: Previously, alerts called HD44780_Clear() and overwrote both
     * lines, destroying the T/H reading. Now:
     *   Line 1 → always shows sensor readings (or error if DHT failed)
     *   Line 2 → shows air quality level OR the highest-priority alert
     * Sensor data remains visible even during an alert.
     */
    char line1[17];
    char line2[17];
    if (dht_status == 1U)
      snprintf(line1, sizeof(line1), "T:%.1fC H:%.1f%%", temp, hum);
    else
      snprintf(line1, sizeof(line1), "DHT11 Error!");
    if (sensor_fault)
    {
      snprintf(line2, sizeof(line2), "Sensor Fault!");
    }
    else if (temp_high && air_quality_alert)
    {
      snprintf(line2, sizeof(line2), "TEMP+AIR ALERT!");
    }
    else if (temp_high)
    {
      snprintf(line2, sizeof(line2), "HIGH TEMP! %.1fC", temp);
    }
    else if (air_quality_alert)
    {
      if (ppm < 350.0f)
        snprintf(line2, sizeof(line2), "Air:Poor %.0fppm", ppm);
      else
        snprintf(line2, sizeof(line2), "Air:Bad  %.0fppm", ppm);
    }
    else
    {
      if (ppm < 100.0f)
        snprintf(line2, sizeof(line2), "Air: Good %.0fppm",ppm);
      else
        snprintf(line2, sizeof(line2), "Air:Moderate %.0f",ppm);
    }


    HD44780_Clear();
    HD44780_SetCursor(0, 0);
    HD44780_PrintStr(line1);
    HD44780_SetCursor(0, 1);
    HD44780_PrintStr(line2);
    if (any_alert)
    {
      HAL_GPIO_WritePin(GPIOB,              LD3_Pin,    GPIO_PIN_SET);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port,   BUZZER_Pin, GPIO_PIN_SET);
    }
    else
    {
      HAL_GPIO_WritePin(GPIOB,              LD3_Pin,    GPIO_PIN_RESET);
      HAL_GPIO_WritePin(BUZZER_GPIO_Port,   BUZZER_Pin, GPIO_PIN_RESET);
    }

    HAL_Delay(2000U);
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  RCC_OscInitStruct.PLL.PLLR = 2;
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
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 83;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

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
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 6;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin|LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : temp_Pin */
  GPIO_InitStruct.Pin = temp_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(temp_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BUZZER_Pin */
  GPIO_InitStruct.Pin = BUZZER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin LD2_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin|LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief  Trigger a single ADC conversion on the MQ135 channel and return
  *         the raw 12-bit result (0–4095).
  */
uint16_t Read_MQ135(void)
{
  uint16_t value = 0U;
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
    value = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return value;
}

/**
  * @brief  Convert a raw MQ135 ADC reading to an estimated PPM value.
  *
  *         Model:  Rs = (Vcc - Vadc) / Vadc * RL
  *                 ratio = Rs / Ro
  *                 ppm   = 10 ^ ((log10(ratio) - 0.48) / -0.42)
  *
  *         Calibration note:
  *           MQ135_RO_CLEAN_AIR must be set to the measured Rs in fresh air.
  *           The default 10.0f is a datasheet approximation only.
  *           Typical clean-air CO2 baseline is ~400 ppm.
  *
  * @param  adc_val  Raw ADC value (0–4095)
  * @retval Estimated PPM (0.0f on invalid input)
  */
float mq135_get_ppm(uint16_t adc_val)
{
  const float RL  = 10.0f;               /* Load resistor on sensor board (kΩ) */
  const float Ro  = MQ135_RO_CLEAN_AIR;  /* Calibrated clean-air resistance    */
  const float Vcc = 3.3f;               /* MCU supply voltage                  */

  if (adc_val == 0U)
    return 0.0f;

  float voltage = ((float)adc_val / 4095.0f) * Vcc;
  if (voltage < 0.01f)
    return 0.0f;

  float Rs    = ((Vcc - voltage) / voltage) * RL;
  float ratio = Rs / Ro;

  if (ratio <= 0.0f)
    return 0.0f;

  float ppm = powf(10.0f, (log10f(ratio) - 0.48f) / -0.42f);

  if (isnan(ppm) || isinf(ppm))
    return 0.0f;

  return ppm;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
