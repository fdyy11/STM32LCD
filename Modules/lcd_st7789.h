#ifndef __LCD_ST7789_H
#define __LCD_ST7789_H

#include "stm32f10x.h"
#include <stdint.h>

/* ============================================================================
 * LCD 分辨率（横屏模式）
 * ============================================================================ */
#define LCD_WIDTH           320
#define LCD_HEIGHT          240

/* ============================================================================
 * 引脚定义（按接线图）
 *   CS   -> PA12
 *   RST  -> PA15
 *   DC   -> PB6
 *   SCK  -> PB3
 *   MOSI -> PB5
 *   MISO -> PB4 (本驱动不使用)
 *   LED1 -> PB1  (运行指示)
 *   LEDK -> PB7  (背光阴极控制)
 * ============================================================================ */
#define LCD_CS_PIN          12
#define LCD_RST_PIN         15
#define LCD_DC_PIN          6
#define LCD_SCK_PIN         3
#define LCD_MOSI_PIN        5
#define LCD_LED1_PIN        1
#define LCD_LEDK_PIN        7

// 按键引脚定义
#define KEY_PAGEUP_PIN      2   // PC2 - PageUp (上一页)
#define KEY_PAGEDOWN_PIN    3   // PC3 - PageDown (下一页)

/* ============================================================================
 * GPIO 操作宏
 * ============================================================================ */
#define LCD_CS_HIGH()       (GPIOA->BSRR = (1 << LCD_CS_PIN))
#define LCD_CS_LOW()        (GPIOA->BSRR = (1 << (LCD_CS_PIN + 16)))
#define LCD_RST_HIGH()      (GPIOA->BSRR = (1 << LCD_RST_PIN))
#define LCD_RST_LOW()       (GPIOA->BSRR = (1 << (LCD_RST_PIN + 16)))
#define LCD_DC_HIGH()       (GPIOB->BSRR = (1 << LCD_DC_PIN))
#define LCD_DC_LOW()        (GPIOB->BSRR = (1 << (LCD_DC_PIN + 16)))
#define LCD_LED1_ON()       (GPIOB->BSRR = (1 << LCD_LED1_PIN))
#define LCD_LED1_OFF()      (GPIOB->BSRR = (1 << (LCD_LED1_PIN + 16)))
// 尝试改为 PB7 HIGH = 背光亮（某些模块LEDK内部接法可能不同）
#define LCD_BACKLIGHT_ON()  (GPIOB->BSRR = (1 << LCD_LEDK_PIN))         // PB7 HIGH
#define LCD_BACKLIGHT_OFF() (GPIOB->BSRR = (1 << (LCD_LEDK_PIN + 16)))  // PB7 LOW

// 按键读取宏（PC2/PC3 低电平表示按下）
#define KEY_PAGEUP_PRESSED()    ((GPIOC->IDR & (1 << KEY_PAGEUP_PIN)) == 0)
#define KEY_PAGEDOWN_PRESSED()  ((GPIOC->IDR & (1 << KEY_PAGEDOWN_PIN)) == 0)

/* ============================================================================
 * 常用颜色（RGB565）
 * ============================================================================ */
#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F
#define COLOR_YELLOW        0xFFE0
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_GRAY          0x8410
#define COLOR_ORANGE        0xFD20

/* ============================================================================
 * 函数声明
 * ============================================================================ */
void LCD_GPIO_Init(void);
void KEY_GPIO_Init(void);  // 按键GPIO初始化
void LCD_Init(void);
void LCD_Reset(void);
void LCD_Clear(uint16_t color);
void LCD_Draw_Point(uint16_t x, uint16_t y, uint16_t color);
void LCD_Set_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_Show_Char(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color);
void LCD_Show_String(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);
void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_Fill_Rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);  // 快速局部清屏
void LCD_Fill_Fast(uint16_t color);  // 快速全屏清屏
void LCD_Write_CmdData(uint8_t cmd, const uint8_t *data, uint16_t len);

/* 中文显示函数 */
void LCD_Show_Chinese(uint16_t x, uint16_t y, uint16_t unicode, uint16_t color, uint16_t bg_color);
void LCD_Show_ASCII(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color);
void LCD_Show_String_With_Chinese(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);

#endif /* __LCD_ST7789_H */
