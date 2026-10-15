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

#ifndef LTE_HANDLER_H_
#define LTE_HANDLER_H_


#include "hw_config.h"

// Déclaration du mutex partagé
extern QueueHandle_t LTE_uart_queue;
extern TaskHandle_t lte_uart_task_handle;
extern SemaphoreHandle_t lte_uart_mutex;



#ifndef UART_BUF_SIZE
#define UART_BUF_SIZE           (1024)
#endif

bool LTE_send_at_cmd_resp(const char *cmd, const char *expected_resp, uint32_t timeout_ms);
bool LTE_send_at_cmd(const char *cmd, char *response, size_t max_response_len, size_t *out_len, uint32_t timeout_ms);
void LTE_power_on(void);
bool LTE_init(void);
bool LTE_send_sms(const char *phone_number, const char *message);
void LTE_read_sms(int index);


#endif /* LTE_HANDLER_H_ */