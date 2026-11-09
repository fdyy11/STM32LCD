#ifndef WIFI_UART4_H
#define WIFI_UART4_H

#include <stdint.h>

#ifndef WIFI_UART_BAUD_RATE
#define WIFI_UART_BAUD_RATE 115200u
#endif

void WiFi_UART4_Init(void);
void WiFi_UART4_SendBytes(const uint8_t *data, uint16_t length);
void WiFi_UART4_SendString(const char *text);
uint16_t WiFi_UART4_ReadBytes(uint8_t *data, uint16_t length);
uint16_t WiFi_UART4_Available(void);
void UART4_IRQHandler(void);

#endif
