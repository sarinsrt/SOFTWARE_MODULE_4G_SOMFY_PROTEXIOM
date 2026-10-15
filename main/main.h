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

#ifndef MAIN_H_
#define MAIN_H_


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "SFY_handler.h"
#include "LTE_handler.h"

extern QueueHandle_t SFY_uart_queue;
extern QueueHandle_t LTE_uart_queue;
extern TaskHandle_t lte_uart_task_handle;
extern SemaphoreHandle_t lte_uart_mutex;

extern int sfy_sledsav_enabled; // Économie d'énergie LED (0: Désactivé, 1: Activé)
extern int sfy_sled_mode;   // Mode de la LED (0: OFF, 1: ON, 2: BLINK)
extern int sfy_sled_timer;  // timer clignotement LED (x100ms)

#define TASK_STACK_SIZE         (3072)

#endif /* MAIN_H_ */
