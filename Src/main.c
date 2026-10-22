/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : STM32F103RCT6 + ST7789 LCD 分页文字显示
 * @note           : 
 *                   - PB1: 系统运行指示（循环闪烁）
 *                   - PC4: LCD初始化成功指示（常亮）
 *                   - PB7: LCD背光控制（常亮）
 *                   - PC2: PageUp按键（上一页）
 *                   - PC3: PageDown按键（下一页）
 *                   - 屏幕: 横屏320x240，黑底白字，每页5行，按键翻页
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "lcd_st7789.h"
// #include "usart3.h"
#include <string.h>

/* 简单延时 */
static void delay_ms(volatile uint32_t ms) {
    while(ms--) {
        for(volatile uint32_t i = 0; i < 500; i++) {
            __asm__("nop");
        }
    }
}

/* 按键消抖延时 */
static void delay_key_debounce(void) {
    delay_ms(1);  // 1ms快速消抖
}


/* 待显示的文字内容（26行）- 支持中文 */
static const char* text_lines[] = {
    /* 第1页：7行 - PV输入参数 */
    "PV1电压：22.5V",
    "PV2电压：33.8V",
    "PV3电压：45.2V",
    "PV1电流：5.3A",
    "PV2电流：6.1A",
    "PV3电流：4.8A",
    "MPPT输出电压：58.6V",
    
    /* 第2页：7行 - PV累计数据 */
    "PV1累计发电量：125.8kWh",
    "PV2累计发电量：203.5kWh",
    "PV3累计发电量：178.2kWh",
    "PV1告警：正常",
    "PV2告警：正常",
    "PV3告警：正常",
    "模块告警：正常",
    
    /* 第3页：7行 - 交流输出参数 */
    "交流输出电压：220.0V",
    "交流输出频率：50.0Hz",
    "交流输出电流：10.5A",
    "交流输出功率：2310W",
    "电池温度：25.3℃",
    "电池放电电流：5.2A",
    "电池充电电流：3.8A",
    
    /* 第4页：5行 - 电池和系统参数 */
    "电池电压：48.2V",
    "系统温度：28.5℃",
    "系统湿度：45.2%",
    "系统风况：正常",
    "系统雨况：正常"
};
#define TOTAL_LINES     26  // 26行，共4页
#define LINES_PER_PAGE  7
// 优化布局：让内容刚好排满屏幕
// 屏幕高度240px，第1行顶格(y=0)，第7行底格(y=240-16=224)
// 行高 = (224-0) / (7-1) = 224/6 ≈ 37px，取整为32px便于计算
#define LINE_HEIGHT     32  // 行高32像素（16x16字模的2倍）
#define START_Y         0   // 第1行顶格
#define START_X         10  // 左边距10像素

// 串口数据显示相关变量
// #define SERIAL_MAX_LINES    5   // 最多显示5行
// #define SERIAL_MAX_CHARS    40  // 每行最多40个字符（320像素/6像素=53字符，留些余量）

// 串口数据行缓冲区
// static char serial_lines[SERIAL_MAX_LINES][SERIAL_MAX_CHARS + 1];
// static uint8_t serial_lines_count = 0;  // 当前已接收的行数
// static uint8_t serial_current_line = 0; // 当前写入行索引

// 定时发送相关变量
// static uint32_t hello_send_counter = 0;  // Hello发送计数器
// static uint8_t pc4_led_state = 0;        // PC4 LED状态

// --------------------------------------------------------------------
// 串口数据解析：将接收到的数据按行拆分，并添加回显功能
// --------------------------------------------------------------------
// void parse_serial_data(void)
// {
//     if(USART3_RX_COMPLETE)
//     {
//         // 关闭接收中断，防止数据被覆盖
//         USART3->CR1 &= ~(1 << 5);  // 清除RXNEIE位
//         
//         // 处理接收到的数据
//         if(USART3_RX_STA & 0x8000)
//         {
//             uint16_t len = USART3_RX_STA & 0x3FFF;
//             
//             // === 调试：发送接收确认信息 ===
//             usart3_send_string("\r\n[RX] Received ");
//             char len_str[10];
//             int i = 0;
//             if(len == 0)
//             {
//                 len_str[i++] = '0';
//             }
//             else
//             {
//                 char tmp[10];
//                 int d = 0;
//                 int v = len;
//                 while(v > 0)
//                 {
//                     tmp[d++] = '0' + (v % 10);
//                     v /= 10;
//                 }
//                 for(int j = 0; j < d; j++)
//                 {
//                     len_str[i++] = tmp[d - 1 - j];
//                 }
//             }
//             len_str[i] = '\0';
//             usart3_send_string(len_str);
//             usart3_send_string(" bytes\r\n");
//             
//             // === 串口回显功能：将接收到的数据原样返回 ===
//             // 发送接收到的数据回去（验证收发功能）
//             for(uint16_t i = 0; i < len; i++)
//             {
//                 usart3_send_byte(USART3_RX_BUF[i]);
//             }
//             // 发送换行符
//             usart3_send_byte('\r');
//             usart3_send_byte('\n');
//             
//             // 将数据复制到当前行缓冲区
//             uint8_t copy_len = (len < SERIAL_MAX_CHARS) ? len : SERIAL_MAX_CHARS;
//             memcpy(serial_lines[serial_current_line], USART3_RX_BUF, copy_len);
//             serial_lines[serial_current_line][copy_len] = '\0';
//             
//             // 移动到下一行
//             serial_current_line++;
//             if(serial_current_line >= SERIAL_MAX_LINES)
//             {
//                 serial_current_line = 0;  // 循环覆盖
//             }
//             if(serial_lines_count < SERIAL_MAX_LINES)
//             {
//                 serial_lines_count++;
//             }
//             
//             // === 调试：显示当前显示状态 ===
//             usart3_send_string("[DISP] Line count: ");
//             usart3_send_byte('0' + serial_lines_count);
//             usart3_send_string(", Current: ");
//             usart3_send_byte('0' + serial_current_line);
//             usart3_send_string("\r\n");
//         }
//         
//         // 清空接收状态，准备下一次接收
//         USART3_RX_STA = 0;
//         USART3_RX_COMPLETE = 0;
//         
//         // 重新使能接收中断
//         USART3->CR1 |= (1 << 5);  // 设置RXNEIE位
//     }
// }

// --------------------------------------------------------------------
// 显示串口数据到LCD
// 仅在有串口数据时显示，避免清空第5页的预设文字
// --------------------------------------------------------------------
// void display_serial_data(void)
// {
//     // 如果没有串口数据，直接返回，不清空文字区域
//     if(serial_lines_count == 0)
//     {
//         return;
//     }
//     
//     // 清空文字区域
//     LCD_Fill(START_X, START_Y, 
//              START_X + 300, START_Y + SERIAL_MAX_LINES * LINE_HEIGHT - 1, 
//              COLOR_BLACK);
//     
//     // 计算要显示的行数
//     uint8_t lines_to_show = (serial_lines_count > SERIAL_MAX_LINES) ? 
//                             SERIAL_MAX_LINES : serial_lines_count;
//     
//     // 滚动偏移（如果数据超过最大行数，显示最新的）
//     uint8_t scroll_offset = (serial_lines_count > SERIAL_MAX_LINES) ?
//                             (serial_lines_count - SERIAL_MAX_LINES) : 0;
//     
//     // 显示串口数据
//     for(uint8_t i = 0; i < lines_to_show; i++) {
//         uint8_t actual_line = scroll_offset + i;
//         uint16_t y = START_Y + i * LINE_HEIGHT;
//         
//         if(actual_line < serial_lines_count) {
//             LCD_Show_String_With_Chinese(START_X, y, serial_lines[actual_line], COLOR_WHITE, COLOR_BLACK);
//         }
//     }
// }

int main(void) {
    /* --------------------------------------------------------------------
     * 1. 初始化 LCD、按键
     * -------------------------------------------------------------------- */
    LCD_Init();
    KEY_GPIO_Init();  // 初始化按键GPIO
    // usart3_init(115200);  // 初始化USART3，波特率115200
    
    /* 串口初始化提示 */
    // usart3_send_string("\r\n=== USART3 Initialization ===\r\n");
    // usart3_send_string("PC10=TX, PC11=RX\r\n");
    // usart3_send_string("Baud: 115200\r\n");
    // usart3_send_string("Waiting for data...\r\n");
    // usart3_send_string("===========================\r\n\r\n");
    
    /* 清屏为黑色 */
    LCD_Clear(COLOR_BLACK);
    
    /* --------------------------------------------------------------------
     * 2. 主循环：按键控制翻页显示
     * -------------------------------------------------------------------- */
    uint32_t counter = 0;
    uint8_t led_state = 1;
    uint8_t current_page = 0;  // 当前页码（0开始）
    uint8_t total_pages = (TOTAL_LINES + LINES_PER_PAGE - 1) / LINES_PER_PAGE;  // 总页数
    
    /* 显示初始页面 - 优化：先清屏再一次性显示 */
    LCD_Clear(COLOR_BLACK);
    for(uint8_t i = 0; i < LINES_PER_PAGE; i++) {
        uint8_t line_idx = current_page * LINES_PER_PAGE + i;
        if(line_idx < TOTAL_LINES) {
            uint16_t y = START_Y + i * LINE_HEIGHT;
            LCD_Show_String_With_Chinese(START_X, y, text_lines[line_idx], COLOR_WHITE, COLOR_BLACK);
        }
    }
    
    /* 按键状态记录（防止重复触发） */
    uint8_t keyup_released = 1;   // 1=已释放, 0=已按下
    uint8_t keydown_released = 1; // 1=已释放, 0=已按下
    
    /* === 调试变量 === */
    static uint32_t debug_counter = 0;  // 调试计数器
    
    while(1) {
        /* === 切换LED1状态（循环闪烁） === */
        if(led_state) {
            LCD_LED1_OFF();
            led_state = 0;
        } else {
            LCD_LED1_ON();
            led_state = 1;
        }
        
        /* === 检测PageDown按键（PC3）- 下一页 === */
        if(KEY_PAGEDOWN_PRESSED()) {
            if(keydown_released) {  // 确保按键已释放才响应
                delay_key_debounce();  // 快速消抖
                if(KEY_PAGEDOWN_PRESSED()) {  // 再次确认
                    keydown_released = 0;  // 标记为已按下
                    
                    current_page++;
                    if(current_page >= total_pages) {
                        current_page = 0;  // 循环到第一页
                    }
                    
                    /* 先快速全屏清屏（使用优化的全屏清屏函数） */
                    LCD_Fill_Fast(COLOR_BLACK);
                    
                    /* 绘制新页面文字 */
                    for(uint8_t i = 0; i < LINES_PER_PAGE; i++) {
                        uint8_t line_idx = current_page * LINES_PER_PAGE + i;
                        if(line_idx < TOTAL_LINES) {
                            uint16_t y = START_Y + i * LINE_HEIGHT;
                            LCD_Show_String_With_Chinese(START_X, y, text_lines[line_idx], COLOR_WHITE, COLOR_BLACK);
                        }
                    }

                    /* 如果是第5页（索引4），清空串口数据显示区域 */

                }
            }
        } else {
            keydown_released = 1;  // 按键释放，标记为可响应
        }
        
        /* === 检测PageUp按键（PC2）- 上一页 === */
        if(KEY_PAGEUP_PRESSED()) {
            if(keyup_released) {  // 确保按键已释放才响应
                delay_key_debounce();  // 快速消抖
                if(KEY_PAGEUP_PRESSED()) {  // 再次确认
                    keyup_released = 0;  // 标记为已按下
                    
                    if(current_page == 0) {
                        current_page = total_pages - 1;  // 循环到最后一页
                    } else {
                        current_page--;
                    }
                    
                    /* 局部刷新：只更新文字区域（不清全屏） */
                    /* 先清空文字区域为黑色 */
                    /* 优化：使用专门的局部清屏函数，速度更快 */
                    LCD_Fill_Rect(START_X, START_Y, 
                             START_X + 310, START_Y + LINES_PER_PAGE * LINE_HEIGHT - 1, 
                             COLOR_BLACK);
                    
                    /* 绘制新页面文字 */
                    for(uint8_t i = 0; i < LINES_PER_PAGE; i++) {
                        uint8_t line_idx = current_page * LINES_PER_PAGE + i;
                        if(line_idx < TOTAL_LINES) {
                            uint16_t y = START_Y + i * LINE_HEIGHT;
                            LCD_Show_String_With_Chinese(START_X, y, text_lines[line_idx], COLOR_WHITE, COLOR_BLACK);
                        }
                    }
                    
                    /* 如果是第5页（索引4），清空串口数据显示区域 */
                    // if(current_page == 4) {
                    //     // 清空串口数据显示区域（比预设文字区域稍大）
                    //     LCD_Fill(START_X, START_Y, 
                    //              START_X + 300, START_Y + SERIAL_MAX_LINES * LINE_HEIGHT - 1, 
                    //              COLOR_BLACK);
                    //     // 重新显示第5页的预设文字
                    //     for(uint8_t i = 0; i < LINES_PER_PAGE; i++) {
                    //         uint8_t line_idx = current_page * LINES_PER_PAGE + i;
                    //         if(line_idx < TOTAL_LINES) {
                    //             uint16_t y = START_Y + i * LINE_HEIGHT;
                    //             LCD_Show_String_With_Chinese(START_X, y, text_lines[line_idx], COLOR_WHITE, COLOR_BLACK);
                    //         }
                    //     }
                    // }
                }
            }
        } else {
            keyup_released = 1;  // 按键释放，标记为可响应
        }

        /* === 处理串口数据 === */
        // parse_serial_data();
        
        /* === 更新计数器显示（左下角） === */
        char cnt_info[32];
        cnt_info[0] = 'C';
        cnt_info[1] = 'n';
        cnt_info[2] = 't';
        cnt_info[3] = ':';
        cnt_info[4] = ' ';
        
        uint32_t v = counter;
        uint8_t digits = 0;
        char tmp[12];
        if(v == 0) {
            tmp[digits++] = '0';
        } else {
            while(v > 0) {
                tmp[digits++] = '0' + (v % 10);
                v /= 10;
            }
        }
        for(uint8_t i = 0; i < digits; i++) {
            cnt_info[5 + i] = tmp[digits - 1 - i];
        }
        cnt_info[5 + digits] = '\0';
        // 计数器显示在左下角，紧贴最后一行文字下方
        LCD_Show_String_With_Chinese(10, 224, cnt_info, COLOR_YELLOW, COLOR_BLACK);
        
        /* === 显示页码信息（右下角） === */
        char page_info[32];
        page_info[0] = 'P';
        page_info[1] = 'a';
        page_info[2] = 'g';
        page_info[3] = 'e';
        page_info[4] = ' ';
        page_info[5] = '0' + (current_page + 1);
        page_info[6] = '/';
        page_info[7] = '0' + total_pages;
        page_info[8] = '\0';
        // 页码显示在右下角，紧贴最后一行文字下方
        LCD_Show_String_With_Chinese(240, 224, page_info, COLOR_CYAN, COLOR_BLACK);

        /* === 如果在第5页（索引4），显示串口数据 === */
        // if(current_page == 4) {
        //     display_serial_data();
        // }
        
        /* === 更新计数器 === */
        counter++;
        
        /* === 定时发送：每100次循环（约1秒）发送一次Hello1并闪烁PC4 === */
        // hello_send_counter++;
        // if(hello_send_counter >= 100)  // 100 * 10ms = 1秒
        // {
        //     hello_send_counter = 0;
        //     
        //     // 发送Hello1数据到串口
        //     usart3_send_string("Hello1\r\n");
        //     
        //     // 切换PC4 LED状态（闪烁）
        //     pc4_led_state = !pc4_led_state;
        //     if(pc4_led_state) {
        //         GPIOC->BSRR = (1 << 4);  // PC4置高（点亮LED）
        //     } else {
        //         GPIOC->BRR = (1 << 4);   // PC4置低（熄灭LED）
        //     }
        // }
        
        /* === 延时10ms（控制刷新率和LED闪烁频率） === */
        delay_ms(10);
    }
}
