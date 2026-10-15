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

#include "LTE_handler.h"

// Envoie une commande AT et attend une réponse attendue ("OK", "ERROR", etc.) avec un timeout
bool LTE_send_at_cmd_resp(const char *cmd, const char *expected_resp, uint32_t timeout_ms) {
    // 1. Verrouiller l'accès au bus UART
    if (lte_uart_mutex != NULL) {
        xSemaphoreTake(lte_uart_mutex, portMAX_DELAY);
    }

    // 2. Vider le buffer matériel et la queue d'événements
    uart_flush_input(LTE_UART_PORT_NUM);
    if (LTE_uart_queue != NULL) {
        xQueueReset(LTE_uart_queue);
    }

    // 3. Envoyer la commande AT
    uart_write_bytes(LTE_UART_PORT_NUM, cmd, strlen(cmd));
    uart_write_bytes(LTE_UART_PORT_NUM, "\r\n", 2);

    ESP_LOGI("LTE_send_at_cmd_resp", "Send AT command: %s, waiting for: %s", cmd, expected_resp);

    // Buffer local dynamique/stack au lieu de static
    char local_buf[UART_BUF_SIZE] = {0};
    uint16_t len = 0;
    bool result = false;

    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // 4. Boucle de lecture jusqu'à réponse ou timeout
    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
        int read_len = uart_read_bytes(
            LTE_UART_PORT_NUM, 
            (uint8_t *)(local_buf + len), 
            sizeof(local_buf) - len - 1, 
            pdMS_TO_TICKS(100)
        );

        if (read_len > 0) {
            len += read_len;
            local_buf[len] = '\0';
            ESP_LOGI("LTE_send_at_cmd_resp", "Received: %s", local_buf);

            // Vérifier si la réponse attendue est présente
            if (strstr(local_buf, expected_resp) != NULL) {
                ESP_LOGI("LTE_send_at_cmd_resp", "Expected response found: %s", expected_resp);
                result = true;
                break;
            }

            // Vérifier s'il y a une erreur
            if (strstr(local_buf, "ERROR") != NULL) {
                ESP_LOGW("LTE_send_at_cmd_resp", "Command returned ERROR");
                result = false;
                break;
            }
        }
    }

    // 5. Nettoyer les événements accumulés pendant l'attente
    if (LTE_uart_queue != NULL) {
        xQueueReset(LTE_uart_queue);
    }

    // 6. Déverrouiller le mutex dans TOUS les cas (succès, erreur ou timeout)
    if (lte_uart_mutex != NULL) {
        xSemaphoreGive(lte_uart_mutex);
    }

    return result;
}

// Envoie une commande AT et renvoie la reponse et sa longueur avec un timeout
bool LTE_send_at_cmd(const char *cmd, char *response, size_t max_response_len, size_t *out_len, uint32_t timeout_ms) {
    // 1. Verrouiller le bus UART LTE pour éviter tout conflit avec LTE_UART_task
    if (lte_uart_mutex != NULL) {
        xSemaphoreTake(lte_uart_mutex, portMAX_DELAY);
    }

    // 2. Vider le buffer hardware et la queue d'événements pour partir sur une base propre
    uart_flush_input(LTE_UART_PORT_NUM);
    if (LTE_uart_queue != NULL) {
        xQueueReset(LTE_uart_queue);
    }

    // 3. Envoyer la commande AT
    uart_write_bytes(LTE_UART_PORT_NUM, cmd, strlen(cmd));
    uart_write_bytes(LTE_UART_PORT_NUM, "\r\n", 2);

    ESP_LOGI("LTE_send_at_cmd", "Send AT command: %s", cmd);

    char local_buf[UART_BUF_SIZE] = {0};
    uint16_t len = 0;
    bool success = false;

    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks) {
        int read_len = uart_read_bytes(
            LTE_UART_PORT_NUM, 
            (uint8_t *)(local_buf + len), 
            sizeof(local_buf) - len - 1, 
            pdMS_TO_TICKS(100)
        );

        if (read_len > 0) {
            len += read_len;
            local_buf[len] = '\0';

            if (strstr(local_buf, "OK\r\n") != NULL || strstr(local_buf, "ERROR\r\n") != NULL) {
                success = (strstr(local_buf, "OK\r\n") != NULL);
                break;
            }
        }
    }

    // Copie de la réponse si un buffer a été fourni
    if (response && max_response_len > 0 && len > 0) {
        size_t copy_len = (len < max_response_len - 1) ? len : max_response_len - 1;
        memcpy(response, local_buf, copy_len);
        response[copy_len] = '\0';
    }

    if (out_len) *out_len = len;

    // 4. Vider les événements accumulés pendant notre lecture synchrone
    if (LTE_uart_queue != NULL) {
        xQueueReset(LTE_uart_queue);
    }

    // 5. Déverrouiller le mutex
    if (lte_uart_mutex != NULL) {
        xSemaphoreGive(lte_uart_mutex);
    }

    return success;
}


/* --- Gestion de l'alimentation matérielle du A7670E --- */
void LTE_power_on(void) {    
    gpio_set_level(LTE_EN_VBAT, 1); // Alimentation du module LTE
    vTaskDelay(pdMS_TO_TICKS(500)); // Attente de stabilisation de l'alimentation  
    gpio_set_level(LTE_RESET, 0);
    gpio_set_level(LTE_DTR, 1);
    
    // Impulsion PWRKEY pour démarrer le module (minimum 50ms pour le A7670E)
    ESP_LOGI("LTE_power_on", "Allumage du module A7670E...");
    gpio_set_level(LTE_PWRKEY, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LTE_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(6000)); // Temps de démarrage du modem 5-7s
    ESP_LOGI("LTE_power_on", "Module A7670E allumé.");
}


// Envoie un SMS au format Texte
bool LTE_send_sms(const char *phone_number, const char *message) {
    char cmd[64];
    
    ESP_LOGI("LTE_send_sms", "Envoi de SMS à %s...", phone_number);

    // Configuration en mode Texte (1)
    if (!LTE_send_at_cmd_resp("AT+CMGF=1", "OK", 1000)) {
        ESP_LOGE("LTE_send_sms", "Échec de passage en mode texte");
        return false;
    }

    // Commande d'envoi
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phone_number);
    uart_write_bytes(LTE_UART_PORT_NUM, cmd, strlen(cmd));
    uart_write_bytes(LTE_UART_PORT_NUM, "\r\n", 2);

    // Attente du prompt '>'
    vTaskDelay(pdMS_TO_TICKS(200));

    // Envoi du corps du message suivi de Ctrl+Z (0x1A)
    uart_write_bytes(LTE_UART_PORT_NUM, message, strlen(message));
    uint8_t ctrl_z = 0x1A;
    uart_write_bytes(LTE_UART_PORT_NUM, &ctrl_z, 1);

    // Attente de la confirmation (+CMGS: <mr> et OK)
    uint8_t rx_buf[256];
    int len = uart_read_bytes(LTE_UART_PORT_NUM, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(10000));
    if (len > 0) {
        rx_buf[len] = '\0';
        if (strstr((char *)rx_buf, "OK") != NULL) {
            ESP_LOGI("LTE_send_sms", "SMS envoyé avec succès !");
            return true;
        }
    }

    ESP_LOGE("LTE_send_sms", "Erreur lors de l'envoi du SMS");
    return false;
}

/* TODO : gestion MUTEX */
// Lit et affiche un SMS selon son index
/*void LTE_read_sms(int index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    
    // Passage temporaire du port en lecture directe
    LTE_send_at_cmd_resp("AT+CMGF=1", "OK", 1000);
    LTE_send_at_cmd_resp(cmd, "OK", 3000);

    // Optionnel : Suppression du SMS après lecture
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    LTE_send_at_cmd_resp(cmd, "OK", 1000);
}
*/


bool LTE_init(void) {
    ESP_LOGI("LTE_init", "initialisation du module A7670E...");

    // 1. Synchronisation AT
    ESP_LOGI("LTE_init", "Init step 1");
    int retry = 0;
    while (!LTE_send_at_cmd_resp("AT", "OK", 1000) && retry < 10) {
        retry++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (retry >= 10) return false;

    // 2. Désactivation de l'écho local pour simplifier le parsing
    ESP_LOGI("LTE_init", "Init step 2");
    LTE_send_at_cmd_resp("ATE0", "OK", 1000);

    // 2.5. Vérification de la carte SIM
    ESP_LOGI("LTE_init", "Init step 2.5");
    LTE_send_at_cmd_resp("AT+CPIN=\"0000\"", "READY", 3000);
    

    // 3. Vérification de la carte SIM
    ESP_LOGI("LTE_init", "Init step 3");
    if (!LTE_send_at_cmd_resp("AT+CPIN?", "READY", 3000)) {
        ESP_LOGE("LTE_init", "SIM non prête ou verrouillée PIN");
        return false;
    }

    // 4. Configuration des notifications SMS (CNMI) : Alerte sur l'UART dès réception
    // mt=1 -> Notification +CMTI, mode=2 -> Flux TE
    ESP_LOGI("LTE_init", "Init step 4");
    LTE_send_at_cmd_resp("AT+CNMI=2,1,0,0,0", "OK", 1000);

    // 5. Attente de la connexion réseau (EPS / LTE)    
    ESP_LOGI("LTE_init", "Init step 5");
    ESP_LOGI("LTE_init", "Attente du réseau cellulaire...");
    retry = 0;
    while (retry < 20) {
        if (LTE_send_at_cmd_resp("AT+CEREG?", "+CEREG: 0,1", 1000) || 
            LTE_send_at_cmd_resp("AT+CEREG?", "+CEREG: 0,5", 1000)) {
            ESP_LOGI("LTE_init", "Enregistré sur le réseau 4G LTE !");
            return true;
        }
        retry++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return false;
}