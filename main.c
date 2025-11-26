/*
 main.c — Quiz app (I2C character LCD)
 Behavior:
 - Shows each question and accepts answer over UART.
 - Gives immediate "Correct!" / "Wrong!" feedback with LEDs and beeps.
 - Tracks score internally.
 - Displays the final score ONLY at the end of the quiz (after NUM_QUESTIONS),
   as a "Round complete" screen, then resets score and continues.
 - Optional ASCII diagnostic (RUN_ASCII_TEST).
*/
#include "main.h"
#include "i2c.h"
#include <string.h>
#include <strings.h> /* for strcasecmp */
#include <ctype.h>   /* for isspace, tolower */
#include <stdio.h>   /* for snprintf */
UART_HandleTypeDef huart1;
I2C_HandleTypeDef hi2c1;
/* --- CONFIG --- */
#define RUN_ASCII_TEST 0   /* set to 1 to run ascii mapping test at startup (temporary) */
/* --- PERIPHERAL / UI PINS --- */
#define BUZZER_PIN   GPIO_PIN_0
#define BUZZER_PORT  GPIOB
#define LED_PORT     GPIOF
#define LED_R_PIN    GPIO_PIN_12
#define LED_B_PIN    GPIO_PIN_11
#define LED_G_PIN    GPIO_PIN_10
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
void play_tone(uint32_t freq, uint32_t duration_ms);
void correct_sound(void);
void wrong_sound(void);
/* --- Quiz data and helpers (file scope) --- */
#define NUM_QUESTIONS 3
/* --- Add your Questions here --- */

const char *questions[NUM_QUESTIONS] = {
   "How many bones  do humans have?",
   "Currency of the Philippines is?",
   "  What is the   capital of Japan?"
};
/* --- Add your Answers here --- */
/* Acceptable answer variants (lowercase) */
const char *answers_variants[NUM_QUESTIONS][3] = {
   { "206", "206 bones", NULL },
   { "philippine peso", "peso", "php" },
   { "tokyo", NULL, NULL }
};
uint8_t rx_buffer[32];
int q_index = 0;
/* Score tracking */
int score = 0;
/* trim leading/trailing whitespace */
static void str_trim(char *s)
{
   char *p = s;
   while (*p && isspace((unsigned char)*p)) p++;
   if (p != s) memmove(s, p, strlen(p) + 1);
   size_t len = strlen(s);
   while (len > 0 && isspace((unsigned char)s[len - 1])) {
       s[len - 1] = '\0';
       len--;
   }
}
/* lowercase in-place */
static void str_tolower_inplace(char *s)
{
   for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}
/* check answer variants */
static int is_answer_correct(const char *user_in, int idx)
{
   char tmp[64];
   strncpy(tmp, user_in, sizeof(tmp) - 1);
   tmp[sizeof(tmp) - 1] = '\0';
   str_trim(tmp);
   str_tolower_inplace(tmp);
   for (int i = 0; i < 3; ++i) {
       const char *cand = answers_variants[idx][i];
       if (cand == NULL) break;
       if (strcasecmp(tmp, cand) == 0) return 1;
   }
   return 0;
}
/* show final round score centered on second line (row 1) */
static void show_final_score_and_reset(void)
{
   char line1[LCD_COLS + 1];
   char line2[LCD_COLS + 1];
   snprintf(line1, sizeof(line1), "Round complete");
   snprintf(line2, sizeof(line2), "Score: %d/%d", score, NUM_QUESTIONS);
   /* center both lines if they fit */
   char buf1[LCD_COLS + 1], buf2[LCD_COLS + 1];
   memset(buf1, ' ', LCD_COLS); buf1[LCD_COLS] = '\0';
   memset(buf2, ' ', LCD_COLS); buf2[LCD_COLS] = '\0';
   size_t n1 = strlen(line1);
   size_t n2 = strlen(line2);
   size_t pad1 = (n1 < (size_t)LCD_COLS) ? ((LCD_COLS - n1) / 2) : 0;
   size_t pad2 = (n2 < (size_t)LCD_COLS) ? ((LCD_COLS - n2) / 2) : 0;
   if (n1 > (size_t)LCD_COLS) n1 = LCD_COLS;
   if (n2 > (size_t)LCD_COLS) n2 = LCD_COLS;
   memcpy(buf1 + pad1, line1, n1);
   memcpy(buf2 + pad2, line2, n2);
   lcd_clear();
   lcd_put_cur(0, 0);
   lcd_send_string(buf1);
   lcd_put_cur(1, 0);
   lcd_send_string(buf2);
   HAL_Delay(3000); /* show final score for 3 seconds */
   /* reset for next round */
   score = 0;
}
/* --- main --- */
int main(void)
{
   HAL_Init();
   SystemClock_Config();
   MX_GPIO_Init();
   MX_USART1_UART_Init();
   MX_I2C1_Init();
   /* LCD init (I2C character) */
   lcd_init(&hi2c1, 0x27);
   lcd_backlight_on();
   lcd_clear();
#if RUN_ASCII_TEST
   /* Temporary diagnostic: writes ASCII blocks so you can inspect bit mapping */
   lcd_ascii_test();
   HAL_Delay(3000);
   lcd_clear();
#endif
   /* Ensure LEDs are OFF at startup (common-anode -> HIGH = off) */
   HAL_GPIO_WritePin(LED_PORT, LED_R_PIN | LED_B_PIN | LED_G_PIN, GPIO_PIN_SET);
   while (1)
   {
       /* 1) Show the question. Use wrapped display so long text fits the module. */
       lcd_clear();
       lcd_show_wrapped((char*)questions[q_index]);
       /* 2) Read user input from UART (blocking until CR/LF) */
       memset(rx_buffer, 0, sizeof(rx_buffer));
       int idx = 0;
       uint8_t ch;
       while (idx < (int)sizeof(rx_buffer) - 1) {
           HAL_UART_Receive(&huart1, &ch, 1, HAL_MAX_DELAY);
           if (ch == '\r' || ch == '\n') break;
           rx_buffer[idx++] = ch;
       }
       rx_buffer[idx] = '\0';
       /* 3) Check answer */
       int correct = is_answer_correct((char*)rx_buffer, q_index);
       /* 4) Feedback on LCD + LED + sound */
       lcd_clear();
       lcd_put_cur(0, 0);
       if (correct) {
           lcd_send_string("Correct!");
           /* Turn RED and GREEN OFF, turn BLUE ON (common-anode: RESET = ON) */
           HAL_GPIO_WritePin(LED_PORT, LED_R_PIN | LED_G_PIN, GPIO_PIN_SET);
           HAL_GPIO_WritePin(LED_PORT, LED_B_PIN, GPIO_PIN_RESET);
           correct_sound();
           HAL_Delay(400);
           HAL_GPIO_WritePin(LED_PORT, LED_B_PIN, GPIO_PIN_SET);
           /* update score */
           score++;
       } else {
           lcd_send_string("Wrong!");
           HAL_GPIO_WritePin(LED_PORT, LED_G_PIN | LED_B_PIN, GPIO_PIN_SET);
           HAL_GPIO_WritePin(LED_PORT, LED_R_PIN, GPIO_PIN_RESET);
           wrong_sound();
           HAL_Delay(400);
           HAL_GPIO_WritePin(LED_PORT, LED_R_PIN, GPIO_PIN_SET);
       }
       /* Advance question index */
       q_index = (q_index + 1) % NUM_QUESTIONS;
       /* If we've completed a round (wrapped back to 0), show final score and reset */
       if (q_index == 0) {
           show_final_score_and_reset();
       }
       HAL_Delay(200);
   }
   /* unreachable */
}
/* ---- sounds ---- */
void correct_sound(void) {
   play_tone(2000, 450);  // first beep
   HAL_Delay(200);        // gap between beeps
   play_tone(1000, 450);  // second beep
}
void wrong_sound(void) {
   play_tone(900, 4000);
}
/* ---- buzzer ---- */
void play_tone(uint32_t freq, uint32_t duration_ms) {
   if (freq == 0) {
       HAL_Delay(duration_ms);
       return;
   }
   uint32_t cycles = (freq * duration_ms) / 1000;
   uint32_t t;
   for (uint32_t i = 0; i < cycles; i++) {
       HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
       for (t = 0; t < 250; ++t) __NOP();
       HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
       for (t = 0; t < 250; ++t) __NOP();
   }
}
/* --- Peripheral stubs (kept unchanged) --- */
void SystemClock_Config(void)
{
   RCC_OscInitTypeDef RCC_OscInitStruct = {0};
   RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
   __HAL_RCC_PWR_CLK_ENABLE();
   __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
   RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
   RCC_OscInitStruct.HSIState = RCC_HSI_ON;
   RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
   RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
   RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
   RCC_OscInitStruct.PLL.PLLM = 16;
   RCC_OscInitStruct.PLL.PLLN = 336;
   RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
   RCC_OscInitStruct.PLL.PLLQ = 7;
   if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }
   RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                 RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
   RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
   RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
   RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
   RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
   if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }
}
static void MX_USART1_UART_Init(void)
{
   huart1.Instance = USART1;
   huart1.Init.BaudRate = 9600;
   huart1.Init.WordLength = UART_WORDLENGTH_8B;
   huart1.Init.StopBits = UART_STOPBITS_1;
   huart1.Init.Parity = UART_PARITY_NONE;
   huart1.Init.Mode = UART_MODE_TX_RX;
   huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
   huart1.Init.OverSampling = UART_OVERSAMPLING_16;
   if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}
static void MX_I2C1_Init(void)
{
   hi2c1.Instance = I2C1;
   hi2c1.Init.ClockSpeed = 100000;
   hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
   hi2c1.Init.OwnAddress1 = 0;
   hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
   hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
   hi2c1.Init.OwnAddress2 = 0;
   hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
   hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
   if (HAL_I2C_Init(&hi2c1) != HAL_OK) { Error_Handler(); }
}
static void MX_GPIO_Init(void)
{
   __HAL_RCC_GPIOB_CLK_ENABLE();
   __HAL_RCC_GPIOF_CLK_ENABLE();
   GPIO_InitTypeDef GPIO_InitStruct = {0};
   GPIO_InitStruct.Pin = BUZZER_PIN;
   GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
   GPIO_InitStruct.Pull = GPIO_NOPULL;
   GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
   HAL_GPIO_Init(BUZZER_PORT, &GPIO_InitStruct);
   GPIO_InitStruct.Pin = LED_R_PIN | LED_B_PIN | LED_G_PIN;
   GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
   GPIO_InitStruct.Pull = GPIO_PULLUP;
   GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
   HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
}
void Error_Handler(void)
{
   __disable_irq();
   while (1) {}
}
