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

#include "main.h"
#include "hw_config.h"

// Instanciation unique des variables globales
QueueHandle_t LTE_uart_queue = NULL;
QueueHandle_t SFY_uart_queue = NULL;
TaskHandle_t lte_uart_task_handle = NULL;
SemaphoreHandle_t lte_uart_mutex = NULL;

int sfy_sledsav_enabled = 0; // Économie d'énergie LED (0: Désactivé, 1: Activé)
int sfy_sled_mode = 0;   // Mode de la LED (0: OFF, 1: ON, 2: BLINK)
int sfy_sled_timer = 5;  // timer clignotement LED (x100ms)


/**
 * @brief Convertit une valeur uint8_t en chaîne hexadécimale (ex: 0x41 -> "41")
 * 
 * @param val    La valeur ASCII / uint8_t à convertir
 * @param buffer Le buffer de destination (doit faire au moins 3 octets)
 */
void uint8_to_hex_str(uint8_t val, char *buffer) {
    snprintf(buffer, 3, "%02X", val);
}

/* --- Découpage d'arguments (gestion des virgules et des guillemets) --- */
static int parse_args(char *str, char **argv, int max_args) {
    int argc = 0;
    bool in_quotes = false;
    char *p = str;

    if (*p == '\0') return 0;
    argv[argc++] = p;

    while (*p && argc < max_args) {
        if (*p == '"') {
            in_quotes = !in_quotes;
            // Optionnel : décaler ou supprimer le guillemet si besoin
        } else if (*p == ',' && !in_quotes) {
            *p = '\0';
            argv[argc++] = p + 1;
        }
        p++;
    }
    return argc;
}

/* --- Parseur de syntaxe AT --- */
static void parse_at_command(char *line) {
    line[strcspn(line, "\r\n")] = 0;
    if (strlen(line) == 0) return;

    if (strncasecmp(line, "AT", 2) != 0) {
        send_error(4, "operation not supported");
        return;
    }

    char *cmd_ptr = line + 2;
    if (*cmd_ptr == '\0') {
        uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
        return;
    }

    //AT ECHO -> update echo_enabled variable and send ATE0 or ATE1 to LTE module
    if (strcasecmp(cmd_ptr, "E0") == 0) {
        echo_enabled = false; 
        if(LTE_send_at_cmd_resp("ATE0", "OK", 2000)) {
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
        }
        else{
            uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 6);
        }
        //double confirm
        //LTE_send_at_cmd_resp("ATE0", "OK", 2000);
        return; 
        }
    if (strcasecmp(cmd_ptr, "E1") == 0) { 
        echo_enabled = true;  
        if(LTE_send_at_cmd_resp("ATE1", "OK", 2000)) {
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
        }
        return; 
        }

    at_cmd_type_t type = AT_TYPE_EXEC;
    char *args_ptr = NULL;

    char *eq_ptr = strchr(cmd_ptr, '=');
    char *q_ptr = strchr(cmd_ptr, '?');

    if (eq_ptr != NULL) {
        if (strcmp(eq_ptr, "=?") == 0) {
            type = AT_TYPE_TEST;
            *eq_ptr = '\0';
        } else {
            type = AT_TYPE_SET;
            *eq_ptr = '\0';
            args_ptr = eq_ptr + 1;
        }
    } else if (q_ptr != NULL && *(q_ptr + 1) == '\0') {
        type = AT_TYPE_READ;
        *q_ptr = '\0';
    }

    bool found = false;
    for (size_t i = 0; i < cmd_table_len; i++) {
        if (strcasecmp(cmd_ptr, cmd_table[i].name) == 0) {
            char *argv[MAX_ARGS] = {0};
            int argc = 0;

            if (type == AT_TYPE_SET && args_ptr) {
                argc = parse_args(args_ptr, argv, MAX_ARGS);
            }

            cmd_table[i].handler(type, argc, argv);
            found = true;
            break;
        }
    }

    if (!found) {
        send_error(4, "operation not supported");
    }
}



/* --- Tâche principale de gestion UART (événements + timeout) --- */
static void SFY_UART_task(void *pvParameters) {
    uart_event_t event;
    uint8_t rx_buf[UART_BUF_SIZE];
    size_t rx_fifo_pos = 0;
        
    for (;;) {
        if (xQueueReceive(SFY_uart_queue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    uart_read_bytes(SFY_UART_PORT_NUM, rx_buf + rx_fifo_pos, event.size, portMAX_DELAY);
                    
                    #ifdef DBG_MODE
                    //ESP_LOGI("SFY_UART_task", "Recv str: %s", (char *) rx_buf);
                    ESP_LOGI("SFY_UART_task", "current_mode: %d", get_current_mode());
                    #endif

                    if (get_current_mode() == AT_MODE_COMMAND && echo_enabled) {
                        uart_write_bytes(SFY_UART_PORT_NUM, (const char *)(rx_buf + rx_fifo_pos), event.size);
                    }
                    rx_fifo_pos += event.size;

                    // Nettoyage du premier caractère si = \n ou \r
                    if (rx_fifo_pos > 0 && (rx_buf[0] == '\r' || rx_buf[0] == '\n')) {
                        // Décalage des données d'un octet vers la gauche
                        memmove(rx_buf, rx_buf + 1, rx_fifo_pos - 1);
                        rx_fifo_pos--;
                        rx_buf[rx_fifo_pos] = '\0'; // Assure la terminaison de chaîne si c'est du texte
                    }

                    if (get_current_mode() == AT_MODE_DATA) {
                        // Mode flux binaire / transparent
                        handle_raw_data(rx_buf, rx_fifo_pos);
                        rx_fifo_pos = 0;
                    } else if (get_current_mode() == AT_MODE_SMS_TEXT) {
                        // Traitement spécifique à la saisie de message SMS
                        #ifdef DBG_MODE
                        ESP_LOGI("SFY_UART_task", "handle_sms_input: rx_fifo_pos %s, rx_fifo_pos %d", (char *) rx_buf, rx_fifo_pos);
                        #endif
                        handle_sms_input(rx_buf, rx_fifo_pos);
                        rx_fifo_pos = 0;
                    } else {
                        // Mode Commandes AT standard
                        if (rx_buf[rx_fifo_pos - 1] == '\n' || rx_buf[rx_fifo_pos - 1] == '\r') {
                            rx_buf[rx_fifo_pos] = '\0';
                            parse_at_command((char *)rx_buf);
                            rx_fifo_pos = 0;
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(SFY_UART_PORT_NUM);
                    xQueueReset(SFY_uart_queue);
                    rx_fifo_pos = 0;
                    break;

                default:
                    break;
            }
        }
    }
}



/* --- Task pour la gestion asynchrone de l'UART (Réception d'URC / SMS) --- */
static void LTE_UART_task(void *pvParameters) {
    uart_event_t event;
    uint8_t rx_buf[UART_BUF_SIZE];

    for (;;) {
        if (xQueueReceive(LTE_uart_queue, (void *)&event, portMAX_DELAY)) {
            // Prendre le mutex avant d'accéder aux données UART
            if (xSemaphoreTake(lte_uart_mutex, portMAX_DELAY) == pdTRUE) {
                if (event.type == UART_DATA) {
                    int len = uart_read_bytes(LTE_UART_PORT_NUM, rx_buf, event.size, pdMS_TO_TICKS(100));
                    if (len > 0) {
                        rx_buf[len] = '\0';
                        #ifdef DBG_MODE
                        ESP_LOGD("LTE_UART_task", "RX: %s", rx_buf);
                        #endif
                        // Détection des notifications d'arrivée de SMS (URC: +CMTI: "ME", <index>)
                        char *cmti_ptr = strstr((char *)rx_buf, "+CMTI:");
                        // Détection des notifications d'appel (RING)
                        char *ring_ptr = strstr((char *)rx_buf, "RING");
                        if (cmti_ptr != NULL) {
                            //send notification to SFY UART
                            uart_write_bytes(SFY_UART_PORT_NUM,rx_buf,len);
                            
                            /*
                            int sms_index = 0;
                            char *comma_ptr = strchr(cmti_ptr, ',');
                            if (comma_ptr != NULL) {
                                sms_index = atoi(comma_ptr + 1);
                                #ifdef DBG_MODE
                                ESP_LOGI("SFY_UART", "Nouveau SMS reçu à l'index: %d", sms_index);
                                #endif
                                //LTE_read_sms(sms_index);                            
                            }
                            */

                        }
						/*else if (ring_ptr != NULL) {
							//rejeter l'appel -> envoyer ATH ou AT+CHUP
							LTE_send_at_cmd_resp("AT+CHUP", "OK", 1000);
						}*/
                    }
                }
                // Libérer le mutex
                xSemaphoreGive(lte_uart_mutex);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////
////////////////////////////// INIT FUNCTIONS    ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

// SFY uart init
static void SFY_UART_init(void)
{
   /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = SFY_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(SFY_UART_PORT_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 20, &SFY_uart_queue, intr_alloc_flags));
    //ESP_ERROR_CHECK(uart_driver_install(SFY_UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(SFY_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SFY_UART_PORT_NUM, SFY_RXD, SFY_TXD, SFY_RTS, SFY_CTS));

    // Activation de la détection de TOUT (Timeout) sur la ligne RX
    ESP_ERROR_CHECK(uart_set_rx_timeout(SFY_UART_PORT_NUM, 3)); // Timeout exprimé en symboles UART

    // Forcer la ligne RTS au niveau bas (0)
    // 0 = Niveau bas (Active / Ready)
    // 1 = Niveau haut (Inactive / Busy)
    uart_set_rts(SFY_UART_PORT_NUM, 0);

}

// LTE uart init
static void LTE_UART_init(void)
{
   /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = LTE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(LTE_UART_PORT_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 20, &LTE_uart_queue, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(LTE_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(LTE_UART_PORT_NUM, LTE_RXD, LTE_TXD, LTE_RTS, LTE_CTS));

    // Activation de la détection de TOUT (Timeout) sur la ligne RX
    ESP_ERROR_CHECK(uart_set_rx_timeout(LTE_UART_PORT_NUM, 3)); // Timeout exprimé en symboles UART

}


void DTR_Control_Task(void *params)
{
    while(true)
    {
        gpio_set_level(LTE_DTR, gpio_get_level(SFY_DTR));
        vTaskDelay(10/ portTICK_PERIOD_MS);
    }
}

void Led_Control_Task(void *params)
{
    int ON = 0;

    while(true)
    {
        // just make led blink if sfy_sled_mode is set to 2 (blinking mode) and use the timer for the blink interval
        if(sfy_sled_mode <= 1)
        {            
            gpio_set_level(ESP_LED, 1);
            vTaskDelay(10/ portTICK_PERIOD_MS);
        }
        else if(sfy_sled_mode > 1)
        {            
            ON = !ON;
            gpio_set_level(ESP_LED, ON);
            vTaskDelay((sfy_sled_timer * 100) / portTICK_PERIOD_MS);
        }
        else
        {
            gpio_set_level(ESP_LED, 0);
            vTaskDelay(10/ portTICK_PERIOD_MS);
        }
    }
}


void gpio_init(void)
{
    // Configure GPIOs for SFY
    gpio_config_t sfy_io_conf = {
        .pin_bit_mask = (1ULL << SFY_EN_VBAT) | (1ULL << SFY_RST) | (1ULL << SFY_DTR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sfy_io_conf);

    // Configure GPIOs for LTE
    gpio_config_t lte_io_conf = {
        .pin_bit_mask = (1ULL << LTE_PWRKEY) | (1ULL << LTE_RESET) | (1ULL << LTE_DTR | (1ULL << LTE_EN_VBAT) ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&lte_io_conf);

    // Configure CHG_PG GPIO
    gpio_config_t chg_pg_conf = {
        .pin_bit_mask = (1ULL << CHG_PG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&chg_pg_conf);

    // Configure CHG_RST GPIO
    gpio_config_t chg_rst_conf = {
        .pin_bit_mask =  (1ULL << CHG_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&chg_rst_conf);
    gpio_set_level(CHG_RST, 0); // Set CHG_RST low to enable the charger IC

    // Configure ESP_LED GPIO
    gpio_config_t esp_led_conf = {
        .pin_bit_mask = (1ULL << ESP_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&esp_led_conf);
    
}

void app_main(void)
{
    lte_uart_mutex = xSemaphoreCreateMutex();
    assert(lte_uart_mutex != NULL);

    sfy_sled_mode = 0;
    sfy_sled_timer = 1; // Timer par défaut (ex: x100ms)
    printf("=\n");
    printf(" Somfy Protexiom 4G Retrofit Firmware v1.0\n");
    printf(" Copyright (C) 2026  RSP\n");
    printf(" Licensed under GNU General Public License v3.0\n");
    printf("=\n");

    gpio_init();
    SFY_UART_init();
    LTE_UART_init();

    LTE_power_on();

    xTaskCreate(SFY_UART_task, "SFY_UART_task", TASK_STACK_SIZE*2, NULL, 10, NULL);
    xTaskCreate(LTE_UART_task, "LTE_UART_task", TASK_STACK_SIZE, NULL, 10, NULL);
    xTaskCreate(Led_Control_Task, "Led_Control_Task", 2048, NULL, 1, NULL);
    xTaskCreate(DTR_Control_Task, "DTR_Control_Task", 2048, NULL, 1, NULL);

    //for testing purpose, uncomment the following lines to initialize LTE and send a test SMS
    //LTE_init();
    //LTE_send_sms("+33612345678", "Test SMS depuis ESP32");
}
