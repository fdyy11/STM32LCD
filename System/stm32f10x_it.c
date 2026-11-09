/**
  ******************************************************************************
  * @file    stm32f10x_it.c
  * @brief   STM32F103 interrupt service routines.
  ******************************************************************************
  */

#include "stm32f10x_it.h"
#include "battery_rs485.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while(1) {
    }
}

void MemManage_Handler(void)
{
    while(1) {
    }
}

void BusFault_Handler(void)
{
    while(1) {
    }
}

void UsageFault_Handler(void)
{
    while(1) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    Battery_RS485_SysTickHandler();
}
