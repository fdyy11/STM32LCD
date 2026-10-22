// #ifndef __USART3_H
// #define __USART3_H
// #include "stdio.h"
// #include "sys.h"
// 
// ///////////////////////////////////////////////////////////////////////////////////
// // STM32F103RCT6 USART3 驱动
// // PC10: TX (发送)
// // PC11: RX (接收)
// ///////////////////////////////////////////////////////////////////////////////////
// 
// #define USART3_REC_LEN  200  // 接收缓冲区大小
// 
// extern u8 USART3_RX_BUF[USART3_REC_LEN];  // 接收缓冲区
// extern u16 USART3_RX_STA;                 // 接收状态标志
// extern volatile u8 USART3_RX_COMPLETE;    // 接收完成标志
// 
// void usart3_init(u32 bound);
// void usart3_send_byte(u8 byte);
// void usart3_send_string(char *str);
// void usart3_send_debug_info(void);  // 调试信息输出
// 
// #endif
