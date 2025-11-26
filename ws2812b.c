#include "ws2812b.h"

// Timing values for the WS2812 (in nanoseconds)
#define T0H  350  // 0 bit high time
#define T1H  700  // 1 bit high time
#define T0L  800  // 0 bit low time
#define T1L  600  // 1 bit low time

// Convert nanoseconds to cycles, assumes 168MHz clock (STM32F4 default)
// Adjust if using a different MCU/clock
#define NS_TO_CYCLES(n)  ((n) * SystemCoreClock / 1000000000UL)

static void ws2812b_delay(uint32_t ns) {
    uint32_t cycles = NS_TO_CYCLES(ns)/2; // Adjust loop to actual cycle count
    while(cycles--) {
        __NOP();
    }
}

void ws2812b_send(uint8_t *led_buffer, uint16_t led_count) {
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq(); // Timing is critical!

    for (uint16_t i = 0; i < led_count * 3; i++) {
        uint8_t cur_byte = led_buffer[i];
        for (int8_t bit = 7; bit >= 0; bit--) {
            if (cur_byte & (1 << bit)) {
                // '1' bit
                HAL_GPIO_WritePin(WS2812B_PORT, WS2812B_PIN, GPIO_PIN_SET);
                ws2812b_delay(T1H);
                HAL_GPIO_WritePin(WS2812B_PORT, WS2812B_PIN, GPIO_PIN_RESET);
                ws2812b_delay(T1L);
            } else {
                // '0' bit
                HAL_GPIO_WritePin(WS2812B_PORT, WS2812B_PIN, GPIO_PIN_SET);
                ws2812b_delay(T0H);
                HAL_GPIO_WritePin(WS2812B_PORT, WS2812B_PIN, GPIO_PIN_RESET);
                ws2812b_delay(T0L);
            }
        }
    }
    // Reset pulse: > 50us
    HAL_GPIO_WritePin(WS2812B_PORT, WS2812B_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);

    // Restore IRQs
    if (!irq_state) __enable_irq();
}
