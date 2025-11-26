/*
* i2c_lcd.c - PCF8574 -> HD44780 (4-bit)
*
* P0 = RS, P1 = RW, P2 = EN, P3 = BL, P4..7 = D4..D7    (default wiring assumed)
*
* CHANGES INCLUDED:
* - Added configurable nibble shift (PCF_NIBBLE_SHIFT) to fix bit-alignment issues
*   (if characters are missing their rightmost column try setting to 3 or 5).
* - Robust lcd_put_cur(): supports common 16x2 and 20x4 DDRAM mappings automatically
*   based on LCD_COLS/LCD_ROWS definitions (defaults to 16x2).
* - Added utility functions:
*     - lcd_ascii_test(): writes predictable ASCII patterns to help diagnose bit mapping.
*     - lcd_show_wrapped(): helper to display long strings across lines for common sizes.
*     - lcd_scroll_line(): simple left-scrolling helper for long strings on one row.
* - Minor timing tweak and clarified comments.
*
* USAGE:
* - If text still appears clipped (rightmost column missing), try changing the
*   compile-time PCF_NIBBLE_SHIFT value below (3,4,5) to match your PCF8574 wiring.
*
* NOTE:
* - This file expects i2c.h to define:
*     P_CF_RS, P_CF_RW, P_CF_EN, P_CF_BL
*     LCD_COLS, LCD_ROWS
*   Keep those in i2c.h. If LCD_COLS/ROWS are missing, 16x2 is assumed.
*/

#include "i2c.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

/* ---------- Configuration ---------- */
/* Default LCD size if not provided in i2c.h */
#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif

/* Nibble -> PCF8574 shift.
   Default 4: places nibble bits in PCF P4..P7 (common backpack wiring).
   If your adapter maps the 4 bits to different pins, try 3 or 5 to shift left/right.
*/
#ifndef PCF_NIBBLE_SHIFT
#define PCF_NIBBLE_SHIFT 4
#endif

/* ---------- Local state ---------- */
static I2C_HandleTypeDef *hi2c_lcd = NULL;
static uint8_t lcd_addr = 0x27; /* default; overridden by lcd_init */
static uint8_t backlight = P_CF_BL;

/* ---------- Low level I2C write ---------- */
static HAL_StatusTypeDef pcf_write(uint8_t data)
{
   return HAL_I2C_Master_Transmit(hi2c_lcd, (uint16_t)(lcd_addr << 1), &data, 1, HAL_MAX_DELAY);
}

/* Pulse EN to latch nibble */
static void lcd_pulse_enable(uint8_t data)
{
   pcf_write(data | P_CF_EN);
   /* short enable pulse - HD44780 timing: >450ns. HAL_Delay(1) is safe and simple. */
   HAL_Delay(1);
   pcf_write(data & ~P_CF_EN);
   HAL_Delay(1);
}

/* Write 4-bit nibble (lower nibble of nibble param)
   Uses PCF_NIBBLE_SHIFT so you can adapt to different PCF8574 wiring.
*/
static void lcd_write_nibble(uint8_t nibble, uint8_t ctrl)
{
   /* Mask nibble then shift into position for P4..P7 (or adjusted shift) */
   uint8_t out = (uint8_t)(((nibble & 0x0F) << PCF_NIBBLE_SHIFT) & 0xFF);

   /* Or in control bits (RS/RW) and backlight */
   out |= (ctrl & (P_CF_RS | P_CF_RW)) | backlight;

   pcf_write(out);
   lcd_pulse_enable(out);
}

/* Send full 8-bit command */
static void lcd_send_cmd(uint8_t cmd)
{
   uint8_t ctrl = 0;
   lcd_write_nibble((cmd >> 4) & 0x0F, ctrl);
   lcd_write_nibble((cmd >> 0) & 0x0F, ctrl);
   HAL_Delay(2);
}

/* Send full 8-bit data (character) */
static void lcd_send_data(uint8_t data)
{
   uint8_t ctrl = P_CF_RS;
   lcd_write_nibble((data >> 4) & 0x0F, ctrl);
   lcd_write_nibble((data >> 0) & 0x0F, ctrl);
   HAL_Delay(1);
}

/* ---------- PUBLIC API ---------- */

void lcd_init(I2C_HandleTypeDef *hi2c, uint8_t addr7bit)
{
   hi2c_lcd = hi2c;
   lcd_addr = addr7bit & 0x7F;
   backlight = P_CF_BL;
   HAL_Delay(50); /* wait for LCD power-up */

   /* Init sequence — send 0x03 3x then 0x02 to go to 4-bit mode */
   lcd_write_nibble(0x03, 0); HAL_Delay(5);
   lcd_write_nibble(0x03, 0); HAL_Delay(5);
   lcd_write_nibble(0x03, 0); HAL_Delay(2);
   lcd_write_nibble(0x02, 0); HAL_Delay(2);

   /* Function set: 4-bit, N lines, 5x8 dots */
   /* 0x20 = basic 4-bit, 0x08 = 2 lines flag, combine -> 0x28 for 2-line */
   uint8_t func = 0x20;
   if (LCD_ROWS > 1) func |= 0x08;
   lcd_send_cmd(func | 0x00); /* final is typically 0x28 for 2-line */

   /* Display on, cursor off, blink off */
   lcd_send_cmd(0x0C);
   /* Clear display */
   lcd_send_cmd(0x01); HAL_Delay(2);
   /* Entry mode: increment, no shift */
   lcd_send_cmd(0x06);
}

/* Clear */
void lcd_clear(void)
{
   lcd_send_cmd(0x01);
   HAL_Delay(2);
}

/* Position cursor.
   Supports common 16x2 and 20x4 DDRAM maps:
     16x2: line0->0x00, line1->0x40
     20x4: line0->0x00, line1->0x40, line2->0x14, line3->0x54
   For other sizes it falls back to linear mapping row0/row1.
*/
void lcd_put_cur(uint8_t row, uint8_t col)
{
   if (row >= LCD_ROWS) row = LCD_ROWS - 1;
   if (col >= LCD_COLS) col = LCD_COLS - 1;

   uint8_t addr = 0x00;

   if (LCD_ROWS == 2 && LCD_COLS == 16) {
       addr = (row == 0) ? 0x00 : 0x40;
       addr += col;
   }
   else if (LCD_ROWS == 4 && LCD_COLS == 20) {
       switch (row) {
           case 0: addr = 0x00 + col; break;
           case 1: addr = 0x40 + col; break;
           case 2: addr = 0x14 + col; break;
           case 3: addr = 0x54 + col; break;
           default: addr = 0x00 + col; break;
       }
   }
   else {
       /* Generic fallback: row 0 -> 0x00, row 1 -> 0x40 */
       addr = (row == 0) ? 0x00 : 0x40;
       addr += col;
   }

   lcd_send_cmd(0x80 | addr);
}

/* Send C-string to current cursor position */
void lcd_send_string(const char *str)
{
   while (*str) {
       lcd_send_data((uint8_t)(*str++));
   }
}

/* Backlight control */
void lcd_backlight_on(void)
{
   backlight = P_CF_BL;
   pcf_write(backlight);
}
void lcd_backlight_off(void)
{
   backlight = 0;
   pcf_write(backlight);
}

/* ---------- Utility helpers for diagnostics and usability ---------- */

/* Write ASCII test patterns to each line so you can visually inspect bitmapping.
   - For a 16x2 device this writes 16 chars per line (4 lines if the module has them).
   - Use this after init to see if bits/nibbles are aligned correctly.
*/
void lcd_ascii_test(void) {
    char buf[32];
    int total_chars = LCD_ROWS * LCD_COLS;
    int start = 32; /* printable ASCII from space onwards */

    for (int r = 0; r < LCD_ROWS; ++r) {
        int base = start + r * LCD_COLS;
        int len = LCD_COLS;
        if (len > (int)sizeof(buf)-1) len = sizeof(buf)-1;
        for (int i = 0; i < len; ++i) buf[i] = (char)(base + i);
        buf[len] = '\0';
        lcd_put_cur(r, 0);
        lcd_send_string(buf);
    }
}

/* Show long string wrapped across multiple lines (simple helper)
   truncates to available display area. */
void lcd_show_wrapped(const char *s) {
    char tmp[LCD_COLS + 1];
    for (int r = 0; r < LCD_ROWS; ++r) {
        int offset = r * LCD_COLS;
        int i;
        for (i = 0; i < LCD_COLS && s[offset + i] != '\0'; ++i) tmp[i] = s[offset + i];
        tmp[i] = '\0';
        lcd_put_cur(r, 0);
        lcd_send_string(tmp);
        if (s[offset + i] == '\0') break;
    }
}

/* Very simple blocking scroll left for one row.
   Use sparingly (blocking) and tune delay_ms to taste.
*/
void lcd_scroll_line(uint8_t row, const char *text, uint16_t delay_ms) {
    int len = (int)strlen(text);
    if (len <= LCD_COLS) {
        lcd_put_cur(row, 0);
        lcd_send_string(text);
        return;
    }

    char buf[LCD_COLS + 1];
    for (int start = 0; start <= len - LCD_COLS; ++start) {
        for (int i = 0; i < LCD_COLS; ++i) buf[i] = text[start + i];
        buf[LCD_COLS] = '\0';
        lcd_put_cur(row, 0);
        lcd_send_string(buf);
        HAL_Delay(delay_ms);
    }
}

/* ---------- End of file ---------- */
