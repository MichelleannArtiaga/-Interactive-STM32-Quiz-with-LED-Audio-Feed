#ifndef __WS2812B_H
#define __WS2812B_H

#include "stm32f4xx_hal.h"

// User configuration: which GPIO and pin
#define WS2812B_PORT    GPIOB
#define WS2812B_PIN     GPIO_PIN_1

void ws2812b_send(uint8_t *led_buffer, uint16_t led_count);

#endif
