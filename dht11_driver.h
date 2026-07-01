/*
 * @file DHT11 Driver
 * @date Created: December 24, 2024
 * @author Author: Simar Singh Ubhi
 * @version 1.0.0
 */

#ifndef DHT11_DRIVER_H
#define DHT11_DRIVER_H

#include "main.h"  // Include the main header for HAL and other dependencies
#include <stdbool.h> // Include for the boolean type
#include "stm32f4xx_hal.h"

/**
 * @brief DHT11 data structure
 */
typedef struct {
    float humidity;      // Humidity value
    float temperature;   // Temperature value
    bool error;          // Indicates if there was an error
} dht11_data;


// Public API
void dht11_init(GPIO_TypeDef *gpio_port, uint16_t gpio_pin, TIM_HandleTypeDef *tim); // Initializes the dht11
dht11_data dht11_data_read(void); // Reads data from dht11

void DWT_Delay_Init(void);
void DWT_Delay_us(volatile uint32_t us);

uint8_t DHT11_ReadData(float *temp, float *hum);

#endif // DHT11_DRIVER_H
