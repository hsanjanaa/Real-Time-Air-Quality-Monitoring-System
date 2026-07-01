/**
 *  @file dht11_driver.c
 *	@brief DHT11 Temperature Sensor Driver Library
 *  @date Created on: Dec 24, 2024
 *  @author Author: Simar Singh Ubhi
 *  @version 1.0.0
 *
 */

#include "dht11_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

#define DHT11_PORT GPIOC
#define DHT11_PIN GPIO_PIN_0
//**********************Private Variables**********************//
static GPIO_TypeDef *dht_gpio_port; // GPIOA
static uint16_t dht_gpio_pin; // GPIO_PIN_1, GPIO_PIN_2, etc.
static TIM_HandleTypeDef *dht_tim; // &htim6, &htim12, etc.

//**********************Private Functions**********************//
/*
 *	@brief Microsecond delay
 *	@param us the duration of microseconds to delay
 */
static void delay_us(uint16_t us) {
    __HAL_TIM_SET_COUNTER(dht_tim, 0);
    while (__HAL_TIM_GET_COUNTER(dht_tim) < us);
}

/**
 *	@brief Check if timer has exceeded limit (used to stop infinite while loops)
 *	@param timeout_duration check if the timer has exceeded this duration
 */
static bool check_timeout(uint16_t timeout_duration) {
    return (__HAL_TIM_GET_COUNTER(dht_tim) > timeout_duration);
}

/**
 *	@brief Change GPIO Pin mode between output and input
 *	@param *pin_mode declare if pin mode should be output or input
 */
static void set_gpio_pin_mode(const char *pin_mode) {
    GPIO_InitTypeDef GPIO_Struct = {0}; // Used to HAL GPIO

    if (strcmp(pin_mode,"OUTPUT")==0)
    {
    	GPIO_Struct.Mode = GPIO_MODE_OUTPUT_PP;
    }
    else if (strcmp(pin_mode,"INPUT")==0)
    {
        GPIO_Struct.Mode = GPIO_MODE_INPUT;
    }
    else
    {
    	return;
    }

    // Standard GPIO initialization
    GPIO_Struct.Pin = dht_gpio_pin;
    GPIO_Struct.Pull = GPIO_NOPULL;
    GPIO_Struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    HAL_GPIO_Init(dht_gpio_port, &GPIO_Struct);
}

//**********************Public API Functions**********************//
/**
 *	@brief Initializes DHT11 port, pin, and timer
 *	@param *gpio_port usually GPIOA | GPIOB
 *	@param gpio_pin GPIO_PIN_1, GPIO_PIN_2, etc.
 *	@param *tim &htim6, &htim12, etc.
 */

/*	@brief Starts communication with DHT11 and reads and processes 40 bit response
 */
void dht11_init(GPIO_TypeDef *gpio_port, uint16_t gpio_pin, TIM_HandleTypeDef *tim) {
    dht_gpio_port = gpio_port;
    dht_gpio_pin = gpio_pin;
    dht_tim = tim;

    HAL_TIM_Base_Start(dht_tim); // Start timer count for rest of program
}
void DWT_Delay_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void DWT_Delay_us(volatile uint32_t us) {
    uint32_t clk_cycle_start = DWT->CYCCNT;
    us *= (HAL_RCC_GetHCLKFreq() / 1000000);
    while ((DWT->CYCCNT - clk_cycle_start) < us);
}
static uint8_t DHT11_ReadByte(void) {
    uint8_t i, byte = 0;
    for (i = 0; i < 8; i++) {
        while (!(DHT11_PORT->IDR & DHT11_PIN));  // Wait for HIGH
        DWT_Delay_us(40);  // Wait 40us to sample
        if (DHT11_PORT->IDR & DHT11_PIN)
            byte |= (1 << (7 - i));
        while (DHT11_PORT->IDR & DHT11_PIN);  // Wait for LOW
    }
    return byte;
}

uint8_t DHT11_ReadData(float *temp, float *hum) {
	uint8_t Rh_byte1, Rh_byte2, Temp_byte1, Temp_byte2, sum;

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Output mode - start signal
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(18);  // at least 18ms
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DWT_Delay_us(20);

    // 2. Input mode - wait for response
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);

    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN));       // wait for LOW
    while (!HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN));      // wait for HIGH
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN));       // wait for LOW

    // 3. Read 5 bytes
    Rh_byte1 = DHT11_ReadByte();
    Rh_byte2 = DHT11_ReadByte();
    Temp_byte1 = DHT11_ReadByte();
    Temp_byte2 = DHT11_ReadByte();
    sum = DHT11_ReadByte();

    if (sum == (Rh_byte1 + Rh_byte2 + Temp_byte1 + Temp_byte2)) {
        *hum = (float)Rh_byte1;
        *temp = (float)Temp_byte1;
        return 1.0f;  // success
    }
    return 0.0f;  // checksum error
}



