/*
Somfy Protexiom 4G Retrofit Module Firmware
Copyright (C) 2026  RSP <sarinsr@gmail.com>
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see .
*/

#ifndef HW_CONFIG_H_
#define HW_CONFIG_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"


//BAT CHARGER GPIO
#define CHG_PG GPIO_NUM_2
#define CHG_RST GPIO_NUM_1

//GPIO
#define ESP_LED GPIO_NUM_45

//SFY GPIO
#define SFY_EN_VBAT GPIO_NUM_16  
#define SFY_RST GPIO_NUM_8
#define SFY_DTR GPIO_NUM_4
//SFY UART  
#define SFY_RTS GPIO_NUM_15
#define SFY_RXD GPIO_NUM_6
#define SFY_TXD GPIO_NUM_7
#define SFY_CTS GPIO_NUM_5

//LTE GPIO
#define LTE_EN_VBAT GPIO_NUM_48
#define LTE_PWRKEY GPIO_NUM_35
#define LTE_RESET GPIO_NUM_38
#define LTE_DTR GPIO_NUM_41
//LTE UART
#define LTE_RXD GPIO_NUM_36
#define LTE_RTS GPIO_NUM_37
#define LTE_TXD GPIO_NUM_39
#define LTE_CTS GPIO_NUM_40


#define SFY_UART_PORT_NUM     1
#define LTE_UART_PORT_NUM     2
#define SFY_UART_BAUD_RATE    9600
#define LTE_UART_BAUD_RATE    115200  

#define MAX_ARGS              10
#define UART_BUF_SIZE         (1024)

#define DBG_MODE

#endif /* HW_CONFIG_H_ */