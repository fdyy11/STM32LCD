#include "wifi_uart4.h"
#include "stm32f10x.h"

#define WIFI_RX_BUFFER_SIZE 256u

static volatile uint8_t wifi_rx_buffer[WIFI_RX_BUFFER_SIZE];
static volatile uint16_t wifi_rx_head;
static volatile uint16_t wifi_rx_tail;
static volatile uint16_t wifi_rx_count;

static uint32_t apb1_clock_hz(void) {
    static const uint8_t apb_prescalers[8] = {1u, 1u, 1u, 1u, 2u, 4u, 8u, 16u};
    uint8_t prescaler_bits = (uint8_t)((RCC->CFGR >> 8) & 0x07u);
    return SystemCoreClock / apb_prescalers[prescaler_bits];
}

void WiFi_UART4_Init(void) {
    uint32_t pclk1 = apb1_clock_hz();

    RCC->APB2ENR |= (1u << 2) | (1u << 3) | (1u << 4);
    RCC->APB1ENR |= (1u << 19);

    GPIOC->CRH &= ~((0xFu << 8u) | (0xFu << 12u));
    GPIOC->CRH |= (0xBu << 8u) | (0x4u << 12u);

    UART4->CR1 = 0u;
    UART4->CR2 = 0u;
    UART4->CR3 = 0u;
    /* Encode fCK / (baud * 16) as BRR mantissa/fraction. */
    uint32_t brr_denominator = WIFI_UART_BAUD_RATE * 16u;
    uint32_t brr_mantissa = pclk1 / brr_denominator;
    uint32_t brr_remainder = pclk1 % brr_denominator;
    uint32_t brr_fraction = ((brr_remainder * 16u) + (brr_denominator / 2u)) /
                            brr_denominator;
    if(brr_fraction >= 16u) {
        brr_fraction = 0u;
        brr_mantissa++;
    }
    UART4->BRR = (brr_mantissa << 4) | brr_fraction;
    UART4->SR = 0u;
    UART4->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    wifi_rx_head = 0u;
    wifi_rx_tail = 0u;
    wifi_rx_count = 0u;

    NVIC_SetPriority(UART4_IRQn, 4u);
    NVIC_EnableIRQ(UART4_IRQn);
}

void WiFi_UART4_SendBytes(const uint8_t *data, uint16_t length) {
    for(uint16_t i = 0u; i < length; i++) {
        while((UART4->SR & USART_SR_TXE) == 0u) {
        }
        UART4->DR = data[i];
    }
    while((UART4->SR & USART_SR_TC) == 0u) {
    }
}

void WiFi_UART4_SendString(const char *text) {
    uint16_t length = 0u;
    while(text[length] != '\0') {
        length++;
    }
    WiFi_UART4_SendBytes((const uint8_t *)text, length);
}

uint16_t WiFi_UART4_Available(void) {
    return wifi_rx_count;
}

uint16_t WiFi_UART4_ReadBytes(uint8_t *data, uint16_t length) {
    uint16_t read_count = 0u;
    NVIC_DisableIRQ(UART4_IRQn);
    while(read_count < length && wifi_rx_count != 0u) {
        data[read_count++] = wifi_rx_buffer[wifi_rx_tail];
        wifi_rx_tail++;
        if(wifi_rx_tail >= WIFI_RX_BUFFER_SIZE) {
            wifi_rx_tail = 0u;
        }
        wifi_rx_count--;
    }
    NVIC_EnableIRQ(UART4_IRQn);
    return read_count;
}

void UART4_IRQHandler(void) {
    uint32_t status = UART4->SR;

    if((status & USART_SR_RXNE) != 0u) {
        uint8_t value = (uint8_t)UART4->DR;
        if(wifi_rx_count < WIFI_RX_BUFFER_SIZE) {
            wifi_rx_buffer[wifi_rx_head] = value;
            wifi_rx_head++;
            if(wifi_rx_head >= WIFI_RX_BUFFER_SIZE) {
                wifi_rx_head = 0u;
            }
            wifi_rx_count++;
        }
    } else if((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0u) {
        (void)UART4->DR;
    }
}
