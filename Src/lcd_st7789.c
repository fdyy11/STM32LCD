/**
 ******************************************************************************
 * @file           : lcd_st7789.c
 * @brief          : ST7789 LCD Driver for STM32F103RCT6 (ATM0240B32)
 * @note           : 软件 SPI 驱动，2.4 寸 TFT，320x240 横屏
 *                   LED1 (PB1) 作为程序运行指示
 *                   LED2 (PC4) 作为LCD初始化成功指示
 ******************************************************************************
 */

#include "lcd_st7789.h"
#include "chinese_font.h"
#include <stddef.h>

/* ============================================================================
 * 调试指示灯：PC4 (LCD初始化成功指示)
 * ============================================================================ */
#define LCD_INIT_LED_PIN    4
#define LCD_INIT_LED_ON()   (GPIOC->BSRR = (1 << LCD_INIT_LED_PIN))
#define LCD_INIT_LED_OFF()  (GPIOC->BSRR = (1 << (LCD_INIT_LED_PIN + 16)))

/* ============================================================================
 * 粗略延时（72MHz 主频）
 * ============================================================================ */
static void delay_ms(volatile uint32_t ms) {
    while(ms--) {
        for(volatile uint32_t i = 0; i < 500; i++) {
            __asm__("nop");
        }
    }
}

/* ============================================================================
 * 软件 SPI 底层控制
 * ============================================================================ */
static inline void LCD_SCK_HIGH(void) {
    GPIOB->BSRR = (1 << LCD_SCK_PIN);
}

static inline void LCD_SCK_LOW(void) {
    GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
}

static inline void LCD_MOSI_HIGH(void) {
    GPIOB->BSRR = (1 << LCD_MOSI_PIN);
}

static inline void LCD_MOSI_LOW(void) {
    GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
}

/**
 * @brief  软件 SPI 发送一字节（MSB 先发，模式 0）
 *         优化版：添加适量NOP延时提升信号质量，提高SPI时钟频率
 * @note   进入本函数前 SCK 一定是低电平
 */
static void SOFT_SPI_WriteData(uint8_t Data) {
    for(uint8_t i = 0; i < 8; i++) {
        // 设置 MOSI 数据
        if(Data & 0x80) {
            GPIOB->BSRR = (1 << LCD_MOSI_PIN);
        } else {
            GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
        }
        
        // 数据建立时间（可选，提升稳定性）
        __asm__("nop");
        __asm__("nop");
        
        // 一个完整的 SCK 脉冲：拉高（LCD 采样）→ 拉低
        GPIOB->BSRR = (1 << LCD_SCK_PIN);
        
        // 高电平保持时间（LCD采样窗口）
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
        
        GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
        
        // 低电平保持时间
        __asm__("nop");
        __asm__("nop");
        
        Data <<= 1;
    }
}

/* ============================================================================
 * GPIO 初始化
 *   包含 JTAG 禁用（释放 PA15/PB3/PB4）
 *   配置 LED1 (PB1) 为推挽输出并点亮
 * ============================================================================ */
void LCD_GPIO_Init(void) {
    // 1. 使能 AFIO 时钟，禁用 JTAG（保留 SWD）
    RCC->APB2ENR |= (1 << 0);       // AFIO
    AFIO->MAPR &= ~(0x7 << 24);
    AFIO->MAPR |=  (0x2 << 24);     // SWJ_CFG = 010：SWD 使能，JTAG 禁用

    // 2. 使能 GPIOA、GPIOB、GPIOC 时钟
    RCC->APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4);

    // 3. PA12 (CS)：推挽输出 50MHz
    GPIOA->CRH &= ~(0xF << ((LCD_CS_PIN - 8) * 4));
    GPIOA->CRH |=  (0x3 << ((LCD_CS_PIN - 8) * 4));

    // 4. PA15 (RST)：推挽输出 50MHz
    GPIOA->CRH &= ~(0xF << ((LCD_RST_PIN - 8) * 4));
    GPIOA->CRH |=  (0x3 << ((LCD_RST_PIN - 8) * 4));

    // 5. PB1 (LED1)：推挽输出 50MHz
    GPIOB->CRL &= ~(0xF << (LCD_LED1_PIN * 4));
    GPIOB->CRL |=  (0x3 << (LCD_LED1_PIN * 4));

    // 6. PB3 (SCK)：推挽输出 50MHz
    GPIOB->CRL &= ~(0xF << (LCD_SCK_PIN * 4));
    GPIOB->CRL |=  (0x3 << (LCD_SCK_PIN * 4));

    // 7. PB4 (MISO)：浮空输入（本驱动不需要，留默认）
    GPIOB->CRL &= ~(0xF << (4 * 4));
    GPIOB->CRL |=  (0x4 << (4 * 4));    // 浮空输入

    // 8. PB5 (MOSI)：推挽输出 50MHz
    GPIOB->CRL &= ~(0xF << (LCD_MOSI_PIN * 4));
    GPIOB->CRL |=  (0x3 << (LCD_MOSI_PIN * 4));

    // 9. PB6 (DC)：推挽输出 50MHz
    GPIOB->CRL &= ~(0xF << (LCD_DC_PIN * 4));
    GPIOB->CRL |=  (0x3 << (LCD_DC_PIN * 4));

    // 10. PB7 (LEDK背光控制)：推挽输出 50MHz
    GPIOB->CRL &= ~(0xF << (LCD_LEDK_PIN * 4));
    GPIOB->CRL |=  (0x3 << (LCD_LEDK_PIN * 4));
    
    // 10.1. PC4 (LCD初始化指示LED)：推挽输出 50MHz
    GPIOC->CRL &= ~(0xF << (LCD_INIT_LED_PIN * 4));
    GPIOC->CRL |=  (0x3 << (LCD_INIT_LED_PIN * 4));

    // 11. 初始电平
    LCD_CS_HIGH();      // 片选无效
    LCD_DC_HIGH();      // 数据模式
    LCD_RST_HIGH();     // 复位释放

    // 12. 点亮 LED1 —— 程序运行指示
    LCD_LED1_ON();    // PB1 HIGH = LED亮
    
    // 13. 点亮背光
    LCD_BACKLIGHT_ON();
    
    // 14. PC4 LED 点亮，表示LCD初始化开始
    LCD_INIT_LED_ON();
}

/* ============================================================================
 * 按键 GPIO 初始化
 *   PC2 (PageUp) 和 PC3 (PageDown) 配置为上拉输入
 * ============================================================================ */
void KEY_GPIO_Init(void) {
    // 1. 使能 GPIOC 时钟（如果未在LCD_GPIO_Init中使能）
    RCC->APB2ENR |= (1 << 4);
    
    // 2. PC2 (PageUp)：上拉输入模式
    GPIOC->CRL &= ~(0xF << (KEY_PAGEUP_PIN * 4));
    GPIOC->CRL |=  (0x8 << (KEY_PAGEUP_PIN * 4));  // 输入模式 (00) + 上拉 (10) = 1000 = 0x8
    
    // 3. PC3 (PageDown)：上拉输入模式
    GPIOC->CRL &= ~(0xF << (KEY_PAGEDOWN_PIN * 4));
    GPIOC->CRL |=  (0x8 << (KEY_PAGEDOWN_PIN * 4)); // 输入模式 (00) + 上拉 (10) = 1000 = 0x8
    
    // 4. 设置输出高电平（上拉电阻使能）
    GPIOC->BSRR = (1 << KEY_PAGEUP_PIN);   // PC2 上拉
    GPIOC->BSRR = (1 << KEY_PAGEDOWN_PIN); // PC3 上拉
}

/* ============================================================================
 * 写命令 + 参数（内部自动控制 CS）
 * ============================================================================ */
void LCD_Write_CmdData(uint8_t cmd, const uint8_t *data, uint16_t len) {
    LCD_CS_LOW();
    LCD_DC_LOW();
    SOFT_SPI_WriteData(cmd);

    if(len > 0 && data != NULL) {
        LCD_DC_HIGH();
        for(uint16_t i = 0; i < len; i++) {
            SOFT_SPI_WriteData(data[i]);
        }
    }
    LCD_CS_HIGH();
}

/* ============================================================================
 * LCD 硬件复位
 * ============================================================================ */
void LCD_Reset(void) {
    LCD_RST_LOW();
    delay_ms(20);
    LCD_RST_HIGH();
    delay_ms(20);
}

/* ============================================================================
 * LCD 初始化（ST7789 完整序列）
 * ============================================================================ */
void LCD_Init(void) {
    static uint8_t initialized = 0;
    if(!initialized) {
        LCD_GPIO_Init();
        initialized = 1;
    }

    LCD_Reset();

    // 软件复位
    LCD_Write_CmdData(0x01, NULL, 0);   // SWRESET
    delay_ms(50);

    // 退出睡眠
    LCD_Write_CmdData(0x11, NULL, 0);   // SLPOUT
    delay_ms(50);

    // 像素格式 RGB565
    {
        uint8_t p = 0x55;
        LCD_Write_CmdData(0x3A, &p, 1); // COLMOD
    }
    delay_ms(50);

    // 内存访问控制：横屏 320x240
    {
        uint8_t p = 0x60;               // 尝试1：翻转180度横屏
        LCD_Write_CmdData(0x36, &p, 1); // MADCTL
    }
    delay_ms(50);

    // Porch 控制
    {
        uint8_t p[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        LCD_Write_CmdData(0xB2, p, 5);  // PORCTRL
    }
    delay_ms(50);

    // Gate 控制
    {
        uint8_t p = 0x35;
        LCD_Write_CmdData(0xB7, &p, 1); // GCTRL
    }
    delay_ms(50);

    // VCOM 设置
    {
        uint8_t p = 0x2B;
        LCD_Write_CmdData(0xBB, &p, 1); // VCOMS
    }
    delay_ms(50);

    // LCM 控制
    {
        uint8_t p = 0x2C;
        LCD_Write_CmdData(0xC0, &p, 1); // LCMCTRL
    }
    delay_ms(50);

    // VDV 和 VRH 命令使能
    {
        uint8_t p = 0x01;
        LCD_Write_CmdData(0xC2, &p, 1); // VDVVRHEN
    }
    delay_ms(50);

    // VRH 设置
    {
        uint8_t p = 0x12;
        LCD_Write_CmdData(0xC3, &p, 1); // VRHS
    }
    delay_ms(50);

    // VDV 设置
    {
        uint8_t p = 0x20;
        LCD_Write_CmdData(0xC4, &p, 1); // VDVSET
    }
    delay_ms(50);

    // 帧速率控制
    {
        uint8_t p = 0x0F;
        LCD_Write_CmdData(0xC6, &p, 1); // FRCTR2
    }
    delay_ms(50);

    // 电源控制
    {
        uint8_t p[] = {0xA4, 0xA1};
        LCD_Write_CmdData(0xD0, p, 2);  // PWCTRL1
    }
    delay_ms(50);

    // 正极性 Gamma
    {
        uint8_t p[] = {
            0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37,
            0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19
        };
        LCD_Write_CmdData(0xE0, p, 14); // PVGAMCTRL
    }
    delay_ms(50);

    // 负极性 Gamma
    {
        uint8_t p[] = {
            0xD0, 0x00, 0x05, 0x0D, 0x08, 0x06, 0x47,
            0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19
        };
        LCD_Write_CmdData(0xE1, p, 14); // NVGAMCTRL
    }
    delay_ms(50);

    // 列地址范围 0~319 (横屏模式：宽度320)
    {
        uint8_t p[] = {0x00, 0x00, 0x01, 0x3F};  // 0~319
        LCD_Write_CmdData(0x2A, p, 4);  // CASET
    }
    // 行地址范围 0~239 (横屏模式：高度240)
    {
        uint8_t p[] = {0x00, 0x00, 0x00, 0xEF};  // 0~239
        LCD_Write_CmdData(0x2B, p, 4);  // RASET
    }

    // 显示反转开启
    LCD_Write_CmdData(0x21, NULL, 0);   // INVON
    delay_ms(50);

    // 正常显示模式
    LCD_Write_CmdData(0x13, NULL, 0);   // NORON
    delay_ms(50);

    // 开启显示
    LCD_Write_CmdData(0x29, NULL, 0);   // DISPON
    delay_ms(200);
    
    // PC4 LED 保持常亮，表示LCD初始化成功
    LCD_INIT_LED_ON();
}

/* ============================================================================
 * 设置显示窗口（CS 保持低，由调用者在写数据后拉高）
 * ============================================================================ */
void LCD_Set_Window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    LCD_CS_LOW();

    // 列地址
    LCD_DC_LOW();
    SOFT_SPI_WriteData(0x2A);   // CASET
    LCD_DC_HIGH();
    SOFT_SPI_WriteData((x1 >> 8) & 0xFF);
    SOFT_SPI_WriteData(x1 & 0xFF);
    SOFT_SPI_WriteData((x2 >> 8) & 0xFF);
    SOFT_SPI_WriteData(x2 & 0xFF);

    // 行地址
    LCD_DC_LOW();
    SOFT_SPI_WriteData(0x2B);   // RASET
    LCD_DC_HIGH();
    SOFT_SPI_WriteData((y1 >> 8) & 0xFF);
    SOFT_SPI_WriteData(y1 & 0xFF);
    SOFT_SPI_WriteData((y2 >> 8) & 0xFF);
    SOFT_SPI_WriteData(y2 & 0xFF);

    // 写内存
    LCD_DC_LOW();
    SOFT_SPI_WriteData(0x2C);   // RAMWR
    // 不拉高 CS，调用者继续写数据
}

/* ============================================================================
 * 画点
 * ============================================================================ */
void LCD_Draw_Point(uint16_t x, uint16_t y, uint16_t color) {
    if(x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    LCD_Set_Window(x, y, x, y);
    LCD_DC_HIGH();
    SOFT_SPI_WriteData((color >> 8) & 0xFF);
    SOFT_SPI_WriteData(color & 0xFF);
    LCD_CS_HIGH();  // 画完一个点后拉高CS
}

/* ============================================================================
 * 填充矩形（优化版：使用内联展开提升速度）
 * ============================================================================ */
void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    if(x1 >= LCD_WIDTH || y1 >= LCD_HEIGHT) return;
    if(x2 >= LCD_WIDTH)  x2 = LCD_WIDTH - 1;
    if(y2 >= LCD_HEIGHT) y2 = LCD_HEIGHT - 1;
    if(x1 > x2 || y1 > y2) return;

    uint32_t total = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);
    
    // 预先计算颜色值的高字节和低字节
    const uint8_t hi_byte = (color >> 8) & 0xFF;
    const uint8_t lo_byte = color & 0xFF;

    LCD_Set_Window(x1, y1, x2, y2);
    LCD_DC_HIGH();

    // 批量传输优化：每次处理4个像素（8字节）
    uint32_t quads = total / 4;
    uint32_t remainder = total % 4;
    
    // 批量传输4个像素的组
    for(uint32_t i = 0; i < quads; i++) {
        // 像素1
        uint8_t hi = hi_byte;
        uint8_t lo = lo_byte;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(hi & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            hi <<= 1;
        }
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(lo & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            lo <<= 1;
        }
        
        // 像素2
        hi = hi_byte;
        lo = lo_byte;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(hi & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            hi <<= 1;
        }
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(lo & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            lo <<= 1;
        }
        
        // 像素3
        hi = hi_byte;
        lo = lo_byte;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(hi & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            hi <<= 1;
        }
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(lo & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            lo <<= 1;
        }
        
        // 像素4
        hi = hi_byte;
        lo = lo_byte;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(hi & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            hi <<= 1;
        }
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(lo & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            lo <<= 1;
        }
    }
    
    // 处理剩余的单个像素
    for(uint32_t i = 0; i < remainder; i++) {
        uint8_t hi = hi_byte;
        uint8_t lo = lo_byte;
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(hi & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            hi <<= 1;
        }
        for(uint8_t bit = 0; bit < 8; bit++) {
            if(lo & 0x80) GPIOB->BSRR = (1 << LCD_MOSI_PIN);
            else GPIOB->BSRR = (1 << (LCD_MOSI_PIN + 16));
            GPIOB->BSRR = (1 << LCD_SCK_PIN);
            GPIOB->BSRR = (1 << (LCD_SCK_PIN + 16));
            lo <<= 1;
        }
    }
    
    LCD_CS_HIGH();
}

/* ============================================================================
 * 快速填充矩形区域（优化版）
 * 使用批量传输优化，速度更快
 * ============================================================================ */
void LCD_Fill_Rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    // 直接使用LCD_Fill函数，它已经优化过批量传输
    LCD_Fill(x1, y1, x2, y2, color);
}

/* ============================================================================
 * 快速全屏清屏（优化版）
 * 使用批量传输优化，速度更快
 * ============================================================================ */
void LCD_Fill_Fast(uint16_t color) {
    LCD_Fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, color);
}

/* ============================================================================
 * 清屏（填充指定颜色）
 * ============================================================================ */
void LCD_Clear(uint16_t color) {
    LCD_Fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, color);
}

/* ============================================================================
 * 显示ASCII字符（8x16点阵）- 终极优化版
 * 使用批量SPI传输，而不是逐像素绘制
 * ============================================================================ */
void LCD_Show_ASCII(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color) {
    const uint8_t* font_data = ascii_font_lookup(ch);
    if(font_data == NULL) {
        return;  // 未找到字模，直接返回
    }
    
    // 设置窗口（只设置一次）
    LCD_Set_Window(x, y, x + 7, y + 15);
    LCD_DC_HIGH();
    
    // 批量传输16行数据
    for(uint8_t row = 0; row < 16; row++) {
        uint8_t line_data = font_data[row];
        
        // 每行8个像素，直接通过SPI发送
        for(uint8_t col = 0; col < 8; col++) {
            uint16_t pixel_color = (line_data & 0x80) ? color : bg_color;
            
            // 直接发送像素数据（高字节在前）
            SOFT_SPI_WriteData((pixel_color >> 8) & 0xFF);
            SOFT_SPI_WriteData(pixel_color & 0xFF);
            
            line_data <<= 1;
        }
    }
    
    LCD_CS_HIGH();  // 传输完成后拉高CS
}

/* ============================================================================
 * 显示汉字（16x16点阵）- 终极优化版
 * 使用批量SPI传输，而不是逐像素绘制
 * ============================================================================ */
void LCD_Show_Chinese(uint16_t x, uint16_t y, uint16_t unicode, uint16_t color, uint16_t bg_color) {
    const uint8_t* font_data = chinese_font_lookup(unicode);
    if(font_data == NULL) {
        return;  // 未找到字模，直接返回
    }
    
    // 设置窗口（只设置一次）
    LCD_Set_Window(x, y, x + 15, y + 15);
    LCD_DC_HIGH();
    
    // 批量传输16行数据
    for(uint8_t row = 0; row < 16; row++) {
        // 每行2字节（16像素）
        uint8_t line_data_h = font_data[row * 2];
        uint8_t line_data_l = font_data[row * 2 + 1];
        
        // 高字节（前8像素）
        for(uint8_t col = 0; col < 8; col++) {
            uint16_t pixel_color = (line_data_h & 0x80) ? color : bg_color;
            
            // 直接发送像素数据（高字节在前）
            SOFT_SPI_WriteData((pixel_color >> 8) & 0xFF);
            SOFT_SPI_WriteData(pixel_color & 0xFF);
            
            line_data_h <<= 1;
        }
        
        // 低字节（后8像素）
        for(uint8_t col = 0; col < 8; col++) {
            uint16_t pixel_color = (line_data_l & 0x80) ? color : bg_color;
            
            // 直接发送像素数据（高字节在前）
            SOFT_SPI_WriteData((pixel_color >> 8) & 0xFF);
            SOFT_SPI_WriteData(pixel_color & 0xFF);
            
            line_data_l <<= 1;
        }
    }
    
    LCD_CS_HIGH();  // 传输完成后拉高CS
}

/* ============================================================================
 * 显示单个字符（自动判断ASCII或中文）
 * ============================================================================ */
void LCD_Show_Char(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color) {
    // ASCII字符（8x16点阵）
    LCD_Show_ASCII(x, y, ch, color, bg_color);
}

/* ============================================================================
 * 显示字符串（仅ASCII）
 * ============================================================================ */
void LCD_Show_String(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    while(*str != '\0') {
        LCD_Show_ASCII(x, y, *str, color, bg_color);
        x += 8;  // ASCII字符宽度8像素
        if(x >= LCD_WIDTH) {
            x = 0;
            y += 16;
            if(y >= LCD_HEIGHT) break;
        }
        str++;
    }
}

/* ============================================================================
 * 显示混合字符串（支持ASCII和中文）
 * 中文使用UTF-8编码，每个汉字3字节
 * ============================================================================ */
void LCD_Show_String_With_Chinese(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    while(*str != '\0' && x < LCD_WIDTH && y < LCD_HEIGHT) {
        // 判断字符类型
        if((uint8_t)*str < 0x80) {
            // ASCII字符
            LCD_Show_ASCII(x, y, *str, color, bg_color);
            x += 8;  // ASCII字符宽度8像素
            str++;
        } else if((uint8_t)*str >= 0xE0 && (uint8_t)*str <= 0xEF) {
            // 中文字符（UTF-8编码，3字节）
            if(str[0] != '\0' && str[1] != '\0' && str[2] != '\0') {
                // UTF-8解码为Unicode
                uint16_t unicode = 0;
                unicode |= ((uint16_t)((uint8_t)str[0] & 0x0F) << 12);
                unicode |= ((uint16_t)((uint8_t)str[1] & 0x3F) << 6);
                unicode |= ((uint16_t)((uint8_t)str[2] & 0x3F));
                
                LCD_Show_Chinese(x, y, unicode, color, bg_color);
                x += 16;  // 汉字宽度16像素
                str += 3;
            } else {
                break;
            }
        } else if((uint8_t)*str >= 0xC0 && (uint8_t)*str < 0xE0) {
            // 2字节UTF-8字符（较少见）
            if(str[0] != '\0' && str[1] != '\0') {
                uint16_t unicode = 0;
                unicode |= ((uint16_t)((uint8_t)str[0] & 0x1F) << 6);
                unicode |= ((uint16_t)((uint8_t)str[1] & 0x3F));
                
                LCD_Show_Chinese(x, y, unicode, color, bg_color);
                x += 16;
                str += 2;
            } else {
                break;
            }
        } else {
            // 其他字符，跳过
            str++;
        }
        
        // 自动换行
        if(x >= LCD_WIDTH) {
            x = 0;
            y += 16;
            if(y >= LCD_HEIGHT) break;
        }
    }
}
