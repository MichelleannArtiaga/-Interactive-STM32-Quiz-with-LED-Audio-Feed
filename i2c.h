
#ifndef I2C_LCD_H
#define I2C_LCD_H
#include "stm32f4xx_hal.h"
#include <stdint.h>
/* LCD geometry (change if you have 20x4 etc) */
#define LCD_COLS 16
#define LCD_ROWS 2
/* PCF8574 bit mapping (adjust if your module is different) */
#define P_CF_RS    (1<<0)   /* P0 */
#define P_CF_RW    (1<<1)   /* P1 */
#define P_CF_EN    (1<<2)   /* P2 */
#define P_CF_BL    (1<<3)   /* P3 backlight */
#define P_CF_DATA  (0xF0)   /* P4..P7 used for D4..D7 */
/* Public API */
void lcd_init(I2C_HandleTypeDef *hi2c, uint8_t addr7bit);
void lcd_clear(void);
void lcd_put_cur(uint8_t row, uint8_t col);
void lcd_send_string(const char *str);
void lcd_backlight_on(void);
void lcd_backlight_off(void);
#endif /* I2C_LCD_H */


