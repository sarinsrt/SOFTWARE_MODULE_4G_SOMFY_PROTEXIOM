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

#include "SFY_handler.h"

at_mode_t get_current_mode(void) {
    return current_mode;
}

void set_current_mode(at_mode_t mode) {
    current_mode = mode;
}


/* --- envoi des erreurs AT --- */
void send_error(int err_code, const char *err_msg) {
    char buf[64];
    // 1 - enable +CME ERROR:<err> reports, with <err> in numeric format
    // code 3 operation not allowed
    // code 4 operation not supported

    if (cmee_mode == 1) {
        snprintf(buf, sizeof(buf), "\r\n+CME ERROR: %d\r\n", err_code);
        uart_write_bytes(UART_PORT, buf, strlen(buf));
    // 2 - enable +CME ERROR: <err> reports, with <err> in verbose format
    } else if (cmee_mode == 2) {
        snprintf(buf, sizeof(buf), "\r\n+CME ERROR: %s\r\n", err_msg);
        uart_write_bytes(UART_PORT, buf, strlen(buf));
    // 0 - disable +CME ERROR:<err> reports, use only ERROR report.
    } else {
        uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
    }
}

/* --- Traitement du Mode DATA --- */
void handle_raw_data(uint8_t *data, size_t len) {
    // Détection de la séquence d'échappement "+++" pour quitter le mode DATA
    if (len >= 3 && memcmp(data, "+++", 3) == 0) {
        current_mode = AT_MODE_COMMAND;
        uart_write_bytes(UART_PORT, "\r\nCLOSED\r\nOK\r\n", 13);
        return;
    }

    // Traitement des données reçues en mode transparent
    ESP_LOGI("handle_raw_data", "Données transparentes reçues (%d octets)", len);
    
    // Une fois le traitement terminé, on valide et revient en mode AT (si besoin)
    uart_write_bytes(UART_PORT, "\r\nSEND OK\r\n", 11);
    current_mode = AT_MODE_COMMAND;
}

/* --- Traitement spécifique du corps de texte du SMS --- */
void handle_sms_input(uint8_t *data, size_t len) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 4000;

    static char sms_payload[UART_BUF_SIZE];
    static size_t payload_len = 0;
    static int mr_counter = 1; // Counter Message Reference (+CMGS: <mr>)

    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];

        if (ch == ESC_KEY) {
            // Annulation du SMS sur touche Échap
            payload_len = 0;
            
            //uint8_t esc = 0x1B;
            //uart_write_bytes(LTE_UART_PORT_NUM, &esc, 1);

            if(LTE_send_at_cmd_resp((char*)(0x1B), "OK", 2000)) {
                uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
            }
            else {
                uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            }
            current_mode = AT_MODE_COMMAND;            
            return;
        } else if (ch == CTRL_Z) {
            // Validation et envoi du SMS sur Ctrl+Z
            sms_payload[payload_len] = '\0';
            ESP_LOGI("handle_sms_input", "SMS envoyé à %s | Message: \"%s\"", sms_recipient, sms_payload);

            //char response[64];
            //snprintf(response, sizeof(response), "\r\n+CMGS: %d\r\nOK\r\n", mr_counter++);
            //uart_write_bytes(UART_PORT, response, strlen(response));

            // Envoi du corps du message suivi de Ctrl+Z (0x1A)
            uart_write_bytes(LTE_UART_PORT_NUM, sms_payload, strlen(sms_payload));
            uint8_t ctrl_z = 0x1A;
            uart_write_bytes(LTE_UART_PORT_NUM, &ctrl_z, 1);

            // Attente de la confirmation (+CMGS: <mr> et OK)
            uint8_t rx_buf[256];
            int len = uart_read_bytes(LTE_UART_PORT_NUM, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(10000));
            if (len > 0) {
                rx_buf[len] = '\0';
                if (strstr((char *)rx_buf, "OK") != NULL) {
                    ESP_LOGI("handle_sms_input", "SMS envoyé avec succès !");                    
                    uart_write_bytes(UART_PORT, rx_buf, len);
                }
                else {
                    uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
                }
            }


            payload_len = 0;
            current_mode = AT_MODE_COMMAND;
            return;
        } else {
            // Ajout du caractère dans le buffer du SMS
            if (payload_len < sizeof(sms_payload) - 1) {
                sms_payload[payload_len++] = ch;
            }
        }
    }
}



/*
--------------------------- AT COMMAND HANDLERS  ---------------------------
*/


/* --- Handler AT#CCID (CCID) ID de la carte SIM ---
 * Syntaxe :
 * AT#CCID=?        -> Test (valeurs supportées)
 * AT#CCID?         -> Lecture
 * AT#CCID=<value>  -> Écriture
 */
void handle_cmd_ccid(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    switch (type) {
        case AT_TYPE_EXEC:
        case AT_TYPE_READ:
            // Renvoie le numéro CICCID 
            if (LTE_send_at_cmd("AT+CICCID", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {         
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_ccid", "Recv str avant modification: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                // remplacement du caractere + par le caractere # pour la reponse type TELIT
                char *pos = strstr(LTE_response_buf, "+I");
                if (pos != NULL) {
                    *pos = '#';
                    // On décale le reste de la chaîne (à partir du 'C') d'un cran vers la gauche
                    memmove(pos + 1, pos + 2, strlen(pos + 2) + 1);
                    //on met a jour la longueur de la reponse
                    LTE_response_len--;
                }
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_ccid", "Recv str apres modification: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);
            } 
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            break;

        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 10);
            break;

        case AT_TYPE_SET:
        default:
            uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            break;
    }
}


/* --- Handler AT#CFLO (Flow Control) ---
 * Syntaxe :
 * AT#CFLO=?        -> Test (valeurs supportées)
 * AT#CFLO?         -> Lecture
 * AT#CFLO=<mode>   -> Écriture (0: Aucun, 1: HW RTS/CTS, 2: SW XON/XOFF)
 */
void handle_cmd_cflo(at_cmd_type_t type, int argc, char **argv) {
    static int current_flow_ctrl = 0; // 0 = désactivé par défaut
    char response[64];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "#CFLO: (0-2)\r\nOK\r\n", 19);
            break;
        case AT_TYPE_READ:
            snprintf(response, sizeof(response), "#CFLO: %d\r\nOK\r\n", current_flow_ctrl);
            uart_write_bytes(UART_PORT, response, strlen(response));
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int mode = atoi(argv[0]);
                if (mode >= 0 && mode <= 2) {
                    current_flow_ctrl = mode;

                    // Application de la configuration matérielle selon le mode
                    if (mode == 1) {
                        uart_set_hw_flow_ctrl(UART_PORT, UART_HW_FLOWCTRL_CTS_RTS, 122);                        
                    } else {
                        uart_set_hw_flow_ctrl(UART_PORT, UART_HW_FLOWCTRL_DISABLE, 0);
                    }
                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                } else {
                    uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
                }
            } else {
                uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            }
            break;
        default:
            uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            break;
    }
}

/* --- Handler AT#SLED (Status LED) ---
 * Syntaxe :
 * AT#SLED=?              -> Test
 * AT#SLED?               -> Lecture
 * AT#SLED=<mode>[,<timer>] -> Écriture (0: Off, 1: On, 2: Clignotement)
 */
void handle_cmd_sled(at_cmd_type_t type, int argc, char **argv) {
    char response[64];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "#SLED: (0-2),(1-100)\r\nOK\r\n", 28);
            break;
        case AT_TYPE_READ:
            snprintf(response, sizeof(response), "#SLED: %d,%d\r\nOK\r\n", sfy_sled_mode, sfy_sled_timer);
            uart_write_bytes(UART_PORT, response, strlen(response));
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int mode = atoi(argv[0]);
                if (mode >= 0 && mode <= 2) {
                    sfy_sled_mode = mode;
                    if (argc >= 2) {
                        sfy_sled_timer = atoi(argv[1]);
                    }
                    else{ // valeur par defaut
                        sfy_sled_timer = 5;
                    }
                    // Piloter la GPIO associée à la LED selon `sfy_sled_mode`
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_sled", "LED GPIO -> Mode: %d, Timer: %d", sfy_sled_mode, sfy_sled_timer);
                    #endif

                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                } else {
                    uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
                }
            } else {
                uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            }
            break;
        default:
            uart_write_bytes(UART_PORT, "\r\nERROR\r\n", 9);
            break;
    }
}

/* --- Handler AT#SLEDSAV (LED Power Saving) ---
 * Syntaxe :
 * AT#SLEDSAV=?     -> Test
 * AT#SLEDSAV?      -> Lecture
 * AT#SLEDSAV=<enabled> -> Écriture (0: Désactivé / LED active en veille, 1: Activé / LED éteinte en veille)
 */
void handle_cmd_sledsav(at_cmd_type_t type, int argc, char **argv) {
    static int sledsav_enabled = 0; // 0 = désactivé par défaut
    char response[32];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "#SLEDSAV: (0,1)\r\nOK\r\n", 21);
            break;
        case AT_TYPE_READ:
            snprintf(response, sizeof(response), "#SLEDSAV: %d\r\nOK\r\n", sledsav_enabled);
            uart_write_bytes(UART_PORT, response, strlen(response));
            break;
        case AT_TYPE_EXEC:
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int mode = atoi(argv[0]);
                if (mode == 0 || mode == 1) {
                    sledsav_enabled = mode;
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_sledsav", "Mode Économie LED défini sur : %d", sledsav_enabled);
                    #endif
                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                } else {
                    send_error(21, "invalid index");
                }
            } else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* --- Handler AT+IPR (Baudrate UART)---
 * Syntaxe :
 * AT+IPR=?                             -> Test
 * AT+IPR?                              -> Lecture
 * AT+IPR=<baudrate>                    -> Écriture
 */
void handle_cmd_ipr(at_cmd_type_t type, int argc, char **argv) {
    static uint32_t current_baud = SFY_UART_BAUD_RATE;
    char buf[32];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+IPR: (9600,19200,38400,57600,115200)\r\nOK\r\n", 43);
            break;
        case AT_TYPE_READ:
            snprintf(buf, sizeof(buf), "+IPR: %lu\r\nOK\r\n", current_baud);
            uart_write_bytes(UART_PORT, buf, strlen(buf));
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                uint32_t new_baud = atoi(argv[0]);
                if (new_baud >= 9600 && new_baud <= 115200) {
                    current_baud = new_baud;
                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                    uart_set_baudrate(UART_PORT, current_baud);
                } else {
                    send_error(21, "invalid index");
                }
            } else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }   
}

/* --- Handler AT+IFC: Flow Control (V.250)---
 * Syntaxe :
 * AT+IFC=?                             -> Test
 * AT+IFC?                              -> Lecture
 * AT+IFC=<mode>                        -> Écriture
 */
void handle_cmd_ifc(at_cmd_type_t type, int argc, char **argv) {

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+IFC: (0-2),(0-2)\r\nOK\r\n", 25);
            break;
        case AT_TYPE_READ:
            // Lecture du mode actuel (exemple: 0 = désactivé, 1 = RTS/CTS, 2 = XON/XOFF)
            uart_write_bytes(UART_PORT, "+IFC: 0,0\r\nOK\r\n", 15);
            break;
        case AT_TYPE_SET:
            if (argc == 2) {
                int rx_flow = atoi(argv[0]);
                int tx_flow = atoi(argv[1]);
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_ifc", "argv0: %d, argv1: %d", rx_flow, tx_flow);
                #endif

                if (rx_flow >= 0 && rx_flow <= 2 && tx_flow >= 0 && tx_flow <= 2) {
                    uart_hw_flowcontrol_t mode = UART_HW_FLOWCTRL_DISABLE;
                    uint8_t rx_thresh = 0;

                    // Détermination du mode par combinaison des états non nuls
                    if (rx_flow > 0 && tx_flow > 0) {
                        mode = UART_HW_FLOWCTRL_CTS_RTS;
                    } else if (rx_flow > 0) {
                        mode = UART_HW_FLOWCTRL_RTS;
                    } else if (tx_flow > 0) {
                        mode = UART_HW_FLOWCTRL_CTS;
                    }

                    if (mode != UART_HW_FLOWCTRL_DISABLE) {
                        rx_thresh = 122;
                    }

                    uart_set_hw_flow_ctrl(UART_PORT, mode, rx_thresh);
                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                } else {
                    send_error(21, "invalid index");
                }
            } else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* --- Handler AT+CGSN: Serial Number / IMEI---
 * Syntaxe :
 * AT+CGSN=?                             -> Test
 * AT+CGSN?                              -> Lecture
 * AT+CGSN                               -> Execution
 * AT+CGSN=<serial_number>               -> Écriture interdite
 */
void handle_cmd_cgsn(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
            break;
        case AT_TYPE_READ:
        case AT_TYPE_EXEC:
            // Renvoie le numéro IMEI du module LTE
            if (LTE_send_at_cmd("AT+CGSN", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cgsn", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }            
            break;
        case AT_TYPE_SET:
        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CSQ: Signal Quality---
 * Syntaxe :
 * AT+CSQ=?                             -> Test
 * AT+CSQ?                              -> Lecture
 * AT+CSQ                               -> Execution
 * AT+CSQ=<signal_quality>              -> Écriture interdite
 */
void handle_cmd_csq(at_cmd_type_t type, int argc, char **argv) {
     char LTE_response_buf[UART_BUF_SIZE];
     size_t LTE_response_len = 0;
     uint32_t LTE_timeout_ms = 2000;
     switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CSQ: (0-31,99),(0-7,99)\r\nOK\r\n", 31);
            break;
        case AT_TYPE_READ:
        case AT_TYPE_EXEC:  
            // Renvoie un faux niveau de signal du module LTE
            if (LTE_send_at_cmd("AT+CSQ", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_csq", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            break;
        case AT_TYPE_SET:
        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CGMR: Software Revision---
 * Syntaxe :
 * AT+CGMR=?                             -> Test
 * AT+CGMR?                              -> Lecture
 * AT+CGMR                               -> Execution
 * AT+CGMR=<software_revision>          -> Écriture interdite
 */
void handle_cmd_cgmr(at_cmd_type_t type, int argc, char **argv) {
     char LTE_response_buf[UART_BUF_SIZE];
     size_t LTE_response_len = 0;
     uint32_t LTE_timeout_ms = 2000;
     switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
            break;
        case AT_TYPE_READ:
        case AT_TYPE_EXEC:
            // Renvoie une version logicielle du module LTE
            if (LTE_send_at_cmd("AT+CGMR", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {                
                const char *prefix = "+CGMR: ";    
                // 1. Chercher la présence du préfixe dans le buffer
                char *match = strstr(LTE_response_buf, prefix);                
                if (match != NULL) {
                    size_t prefix_len = strlen(prefix);                    
                    // Calcule la taille de la portion restante après le préfixe
                    // Ex: si LTE_response_buf pointe sur "+CGMR: A124...", match pointe au même endroit.
                    size_t bytes_to_move = LTE_response_len - ((match - LTE_response_buf) + prefix_len);
                    // 2. Décaler la réponse (y compris le caractère ' \0' final si présent)
                    memmove(match, match + prefix_len, bytes_to_move + 1);
                    // 3. Mettre à jour la longueur stockée
                    LTE_response_len -= prefix_len;
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_cgmr", "Recv str apres modification: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                    #endif
                }   
                #ifdef DBG_MODE              
                ESP_LOGI("handle_cmd_cgmr", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            //version Telit GL865-DUAL
            //uart_write_bytes(UART_PORT, "10.00.154\r\nOK\r\n", 19);
            break;
        case AT_TYPE_SET:
        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CPIN: SIM PIN Status---
 * Syntaxe :
 * AT+CPIN=?                             -> Test
 * AT+CPIN?                              -> Lecture
 * AT+CPIN=<pin>                         -> Écriture
 */
void handle_cmd_cpin(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    char buf[32];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CPIN: (READY,PIN,PIN2,PUK,PH-NET-PUK)\r\nOK\r\n", 47);
            break;
        case AT_TYPE_READ:
             // Renvoie le status CMEE du module LTE
            if (LTE_send_at_cmd("AT+CPIN?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cpin", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No response from LTE module");
            }
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                snprintf(buf, sizeof(buf), "AT+CPIN=%s", argv[0]);
                if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                    uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                }
                else {
                    send_error(100, "ERROR: No/Bad response from LTE module");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CMEE: Error Reporting---
 * Syntaxe :
 * AT+CMEE=?                             -> Test
 * AT+CMEE?                              -> Lecture
 * AT+CMEE=<mode>                        -> Écriture
 */
void handle_cmd_cmee(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    char buf[32];
    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMEE: (0-2)\r\nOK\r\n", 19);
            break;
        case AT_TYPE_READ:
            // Renvoie le status CMEE du module LTE
            if (LTE_send_at_cmd("AT+CMEE?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cmee", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No response from LTE module");
            }
            // On ne prend pas celui en interne
            //snprintf(buf, sizeof(buf), "+CMEE: %d\r\nOK\r\n", cmee_mode);
            //uart_write_bytes(UART_PORT, buf, strlen(buf));
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int mode = atoi(argv[0]);
                if (mode >= 0 && mode <= 2) {
                    cmee_mode = mode; // internal CMEE mode must match LTE CMEE mode
                    buf[0] = '\0';
                    snprintf(buf, sizeof(buf), "AT+CMEE=%d", cmee_mode);
                    if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                        uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                    }
                    else {
                        send_error(100, "ERROR: No/Bad response from LTE module");
                    }
                } 
                else {
                    send_error(21, "invalid index");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
         
}


/* --- Handler AT+CREG: Network Registration---
 * Syntaxe :
 * AT+CREG=?                             -> Test
 * AT+CREG?                              -> Lecture
 * AT+CREG=<n>                           -> Écriture
 */
void handle_cmd_creg(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    static int creg_n = 0;
    char buf[32];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CREG: (0-2)\r\nOK\r\n", 19);
            break;
        case AT_TYPE_READ:
            // Renvoie le status CREG du module LTE
            if (LTE_send_at_cmd("AT+CREG?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_creg", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No response from LTE module");
            }
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int n = atoi(argv[0]);
                if (n >= 0 && n <= 2) {
                    creg_n = n;
                    buf[0] = '\0';
                    snprintf(buf, sizeof(buf), "AT+CREG=%d", creg_n);
                    if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                        uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                    }
                    else {
                        send_error(100, "ERROR: No/Bad response from LTE module");
                    }
                } 
                else {
                    send_error(21, "invalid index");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;  
    }
}


/* --- Handler AT+COPS: Operator Selection---
 * Syntaxe :
 * AT+COPS=?                             -> Test
 * AT+COPS?                              -> Lecture
 * AT+COPS=<n>                           -> Écriture
 */
void handle_cmd_cops(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    char buf[32];
    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+COPS: (2,\"F-Bouygues Telecom\",\"BYTEL\",\"20820\",0),(1,\"Orange F\",\"Orange\",\"20801\",0),(1,\"F SFR\",\"SFR\",\"20810\",0),,(0,1,2,3,4),(0,1,2))\r\nOK\r\n", 75);
            break;
        case AT_TYPE_READ:
            // Renvoie le status COPS du module LTE
            if (LTE_send_at_cmd("AT+COPS?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cops", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                if (argc < 2) {
                    int n = atoi(argv[0]);
                    buf[0] = '\0';                
                    if (n >= 0 && n <= 4) {
                        snprintf(buf, sizeof(buf), "AT+COPS=%s", argv[0]);
                        if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                        }
                        else {
                            send_error(100, "ERROR: No/Bad response from LTE module");
                        }
                    } 
                    else {
                        send_error(21, "invalid index");
                    }                
                }
                else{
                    int n = atoi(argv[0]);
                    int m = atoi(argv[1]);
                    buf[0] = '\0';                
                    if ((n >= 0 && n <= 4)&&(m >= 0 && m <= 2)) {
                        snprintf(buf, sizeof(buf), "AT+COPS=%s,%s", argv[0], argv[1]);
                        if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                        }
                        else {
                            send_error(100, "ERROR: No/Bad response from LTE module");
                        }
                    } 
                    else {
                        send_error(21, "invalid index");
                    }
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* --- Handler AT+CMGF: SMS Message Format (0: PDU, 1: Text)---
 * Syntaxe :
 * AT+CMGF=?                             -> Test
 * AT+CMGF?                              -> Lecture
 * AT+CMGF=<n>                           -> Écriture
 */
void handle_cmd_cmgf(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    static int sms_format = 0;
    char buf[32];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMGF: (0,1)\r\nOK\r\n", 18);
            break;
        case AT_TYPE_READ:
            if (LTE_send_at_cmd("AT+CMGF?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cmgf", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            // On ne prend pas celui en interne
            //snprintf(buf, sizeof(buf), "+CMGF: %d\r\nOK\r\n", sms_format);
            //uart_write_bytes(UART_PORT, buf, strlen(buf));
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                int fmt = atoi(argv[0]);
                if (fmt == 0 || fmt == 1)  {
                    sms_format = fmt;  // internal sms_format mode must match LTE sms_format mode
                    buf[0] = '\0';
                    snprintf(buf, sizeof(buf), "AT+CMGF=%d", fmt);
                    if(LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                        uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                    }
                    else {
                        send_error(100, "ERROR: No/Bad response from LTE module");
                    }
                } 
                else {
                    send_error(21, "invalid index");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}

// AT+CMGS: SMS Message 
/* --- Handler AT+CMGS: SMS Message ---
 * Syntaxe :
 * AT+CMGS=?                             -> Test
 * AT+CMGS=<n>                           -> Écriture
 */
void handle_cmd_cmgs(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;

    static int sms_format = 0;
    char buf[64];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMGS: \"da\"\r\nOK\r\n", 19);
            break;
        case AT_TYPE_SET:
            if (argc >= 1) {
                // Nettoyage des éventuels guillemets entourant le numéro
                char *number = argv[0];
                if (number[0] == '"') number++;
                size_t len = strlen(number);
                if (len > 0 && number[len - 1] == '"') number[len - 1] = '\0';
                strncpy(sms_recipient, number, sizeof(sms_recipient) - 1);

                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cmgs", "sms_recipient: %s", (char *) sms_recipient);
                ESP_LOGI("handle_cmd_cmgs", "current_mode: %d", current_mode);
                #endif
                
                //envoi de la commande au module LTE pour préparer l'envoi du SMS
                //et attente de la réponse du module LTE (prompt '> ')
                snprintf(buf, sizeof(buf), "AT+CMGS=\"%s\"\r\n", sms_recipient);                
                if (LTE_send_at_cmd_resp(buf, "> ", 2000)) {
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_cmgs", "Module LTE prêt pour l'envoi du SMS");
                    #endif
                    // Basculement dans le mode d'écriture de SMS
                    current_mode = AT_MODE_SMS_TEXT;
                    uart_write_bytes(UART_PORT, "\r\n> ", 4);
                } 
                else {
                    send_error(4, "ERROR");
                }
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cmgs", "current_mode: %d", current_mode);
                #endif
            } 
            else {
                send_error(304, "invalid pdu mode parameter");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* --- Handler AT+CNMI ---
 * Syntaxe :
 * AT+CNMI=?                             -> Test
 * AT+CNMI?                              -> Lecture
 * AT+CNMI=<mode>[,<mt>[,<bm>[,<ds>[,<bisc>]]]] -> Écriture
 */
void handle_cmd_cnmi(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    char buf[64];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CNMI: (0-2),(0-2),(0),(0),(0)\r\nOK\r\n", 37);
            break;
        case AT_TYPE_READ:
            snprintf(buf, sizeof(buf), "+CNMI: %d,%d,%d,%d,%d\r\nOK\r\n",
                     cnmi_cfg.mode, cnmi_cfg.mt, cnmi_cfg.bm, cnmi_cfg.ds, cnmi_cfg.bisc);
            uart_write_bytes(UART_PORT, buf, strlen(buf));

            // Renvoie le status CNMI du module LTE
            if (LTE_send_at_cmd("AT+CNMI?", LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                #ifdef DBG_MODE
                ESP_LOGI("handle_cmd_cnmi", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                #endif
                uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
            }
            else {
                send_error(100, "ERROR: No/Bad response from LTE module");
            }
            break;
        case AT_TYPE_SET:
            // ecriture de parametre en interne
            if (argc >= 1) {
                int mode = atoi(argv[0]);
                int mt   = (argc >= 2) ? atoi(argv[1]) : cnmi_cfg.mt;
                int bm   = (argc >= 3) ? atoi(argv[2]) : cnmi_cfg.bm;
                int ds   = (argc >= 4) ? atoi(argv[3]) : cnmi_cfg.ds;
                int bisc = (argc >= 5) ? atoi(argv[4]) : cnmi_cfg.bisc;

                // Validation minimale des plages usuelles
                if (mode >= 0 && mode <= 2 && mt >= 0 && mt <= 2) {
                    cnmi_cfg.mode = mode;
                    cnmi_cfg.mt   = mt;
                    cnmi_cfg.bm   = bm;
                    cnmi_cfg.ds   = ds;
                    cnmi_cfg.bisc = bisc;
                } else {
                    send_error(21, "invalid index");
                }
            } else {
                send_error(50, "incorrect parameters");
            }

            // ecriture de parametre dans le module LTE
            if (argc >= 1) {
                if (argc >= 1 && argc <= 5) {
                    // Plages max autorisées pour chaque paramètre (mode, mt, bm, ds, bfr)
                    const int max_limits[] = {2, 3, 2, 2, 1};
                    bool valid = true;

                    // Validation des arguments
                    for (int i = 0; i < argc; i++) {
                        int val = atoi(argv[i]);
                        if (val < 0 || val > max_limits[i]) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) {
                        send_error(21, "invalid index");
                    } else {
                        // Construction dynamique du message AT+CNMI
                        int len = snprintf(buf, sizeof(buf), "AT+CNMI=%s", argv[0]);
                        for (int i = 1; i < argc && len < sizeof(buf); i++) {
                            len += snprintf(buf + len, sizeof(buf) - len, ",%s", argv[i]);
                        }
                        // Envoi et vérification de la réponse
                        if (LTE_send_at_cmd_resp(buf, "OK", 2000)) {
                            uart_write_bytes(UART_PORT, "\r\nOK\r\n", 6);
                        }                        
                        else {
                            send_error(100, "ERROR: No/Bad response from LTE module");
                        }
                    }
                } 
                else {
                    send_error(50, "incorrect parameters");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
            break;
        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* --- Handler AT+CMGL (List Messages) ---
 * Syntaxe :
 * AT+CMGL=?             -> Test
 * AT+CMGL[=<stat>]      -> Lister (stat: "REC UNREAD", "REC READ", "ALL", ou 0-4 en mode PDU)
 */
void handle_cmd_cmgl(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    
    char buf[256];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMGL: (\"REC UNREAD\",\"REC READ\",\"STO UNSENT\",\"STO SENT\",\"ALL\")\r\nOK\r\n", 71);
            break;

        case AT_TYPE_EXEC:
        case AT_TYPE_SET: {
            // Renvoie la liste en provenance du module LTE
            if (argc >= 1) {
                char *arg = argv[0];
                int len = snprintf(buf, sizeof(buf), "AT+CMGL=%s", argv[0]);
                if (LTE_send_at_cmd(buf, LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_cmgl", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                    #endif
                    uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
                }
                else {
                    send_error(100, "ERROR: No/Bad response from LTE module");
                }
            }
            else {
                send_error(50, "incorrect parameters");
            }

            /*
            // Filtrage selon le statut demandé (par défaut "REC UNREAD" ou "ALL")
            char filter[32] = "ALL";
            if (argc >= 1) {
                char *arg = argv[0];
                if (arg[0] == '"') arg++;
                size_t len = strlen(arg);
                if (len > 0 && arg[len - 1] == '"') arg[len - 1] = '\0';
                
                // Si la valeur est numérique (0: UNREAD, 1: READ, 4: ALL)
                if (strcmp(arg, "0") == 0) strcpy(filter, "REC UNREAD");
                else if (strcmp(arg, "1") == 0) strcpy(filter, "REC READ");
                else if (strcmp(arg, "4") == 0) strcpy(filter, "ALL");
                else strncpy(filter, arg, sizeof(filter) - 1);
            }

            for (size_t i = 0; i < sms_db_len; i++) {
                if (!sms_db[i].used) continue;

                if (strcmp(filter, "ALL") == 0 || strcasecmp(filter, sms_db[i].stat) == 0) {
                    snprintf(buf, sizeof(buf), "+CMGL: %d,\"%s\",\"%s\",,\"%s\"\r\n%s\r\n",
                             sms_db[i].index, sms_db[i].stat, sms_db[i].sender, sms_db[i].date, sms_db[i].body);
                    uart_write_bytes(UART_PORT, buf, strlen(buf));
                }
            }
            uart_write_bytes(UART_PORT, "OK\r\n", 4);
            */
            break;
        }

        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CMGR / AT+GMGR (Read Message) ---
 * Syntaxe :
 * AT+CMGR=?         -> Test
 * AT+CMGR=<index>   -> Lire le SMS à l'index donné
 */
void handle_cmd_cmgr(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    
    char buf[256];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMGR: (1-50)\r\nOK\r\n", 20);
            break;

        case AT_TYPE_SET:        
            // Renvoie le message en provenance du module LTE
            if (argc >= 1) {
                char *arg = argv[0];
                int len = snprintf(buf, sizeof(buf), "AT+CMGR=%s", argv[0]);
                if (LTE_send_at_cmd(buf, LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_cmgr", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                    #endif
                    uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
                }
                else {
                    send_error(100, "ERROR: No/Bad response from LTE module");
                }
            }
            else {
                send_error(50, "incorrect parameters");
            }

            /*
            if (argc >= 1) {
                int target_idx = atoi(argv[0]);
                bool found = false;

                for (size_t i = 0; i < sms_db_len; i++) {
                    if (sms_db[i].used && sms_db[i].index == target_idx) {
                        snprintf(buf, sizeof(buf), "+CMGR: \"%s\",\"%s\",,\"%s\"\r\n%s\r\nOK\r\n",
                                 sms_db[i].stat, sms_db[i].sender, sms_db[i].date, sms_db[i].body);
                        uart_write_bytes(UART_PORT, buf, strlen(buf));
                        
                        // Passage automatique du statut de UNREAD à READ après consultation
                        if (strcmp(sms_db[i].stat, "REC UNREAD") == 0) {
                            sms_db[i].stat = "REC READ";
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    send_error(321, "invalid memory index");
                }
            } 
            else {
                send_error(50, "incorrect parameters");
            }
                */
            break;

        default:
            send_error(3, "operation not allowed");
            break;
    }
}

/* --- Handler AT+CMGD (Delete Message) ---
 * Syntaxe :
 * AT+CMGD=?                    -> Test
 * AT+CMGD=<index>[,<delflag>]  -> Supprimer
 * 
 * Valeurs de delflag (3GPP TS 27.005) :
 * 0 : Supprime uniquement le message à l'index spécifié (par défaut)
 * 1 : Supprime tous les messages "REC READ"
 * 2 : Supprime tous les messages "REC READ" et "STO SENT"
 * 3 : Supprime tous les messages "REC READ", "STO SENT" et "STO UNSENT"
 * 4 : Supprime TOUS les messages
 */
void handle_cmd_cmgd(at_cmd_type_t type, int argc, char **argv) {
    char LTE_response_buf[UART_BUF_SIZE];
    size_t LTE_response_len = 0;
    uint32_t LTE_timeout_ms = 2000;
    char buf[64];

    switch (type) {
        case AT_TYPE_TEST:
            uart_write_bytes(UART_PORT, "+CMGD: (1-50),(0-4)\r\nOK\r\n", 25);
            break;

        case AT_TYPE_SET:
            // Renvoie le status en provenance du module LTE
            if (argc >= 1) {
                char *arg = argv[0];
                int len = snprintf(buf, sizeof(buf), "AT+CMGD=%s", argv[0]);
                if (LTE_send_at_cmd(buf, LTE_response_buf, sizeof(LTE_response_buf), &LTE_response_len, LTE_timeout_ms)) {
                    #ifdef DBG_MODE
                    ESP_LOGI("handle_cmd_cmgd", "Recv str: %s, len: %d", LTE_response_buf, (int) LTE_response_len);
                    #endif
                    uart_write_bytes(UART_PORT, LTE_response_buf, LTE_response_len);  
                }
                else {
                    send_error(100, "ERROR: No/Bad response from LTE module");
                }
            }
            else {
                send_error(50, "incorrect parameters");
            }

            /*
            if (argc >= 1) {
                int target_idx = atoi(argv[0]);
                int delflag = (argc >= 2) ? atoi(argv[1]) : 0;

                if (delflag < 0 || delflag > 4) {
                    send_error(21, "invalid index");
                    return;
                }

                bool deleted_any = false;

                for (size_t i = 0; i < sms_db_len; i++) {
                    if (!sms_db[i].used) continue;

                    bool should_delete = false;

                    if (delflag == 0) {
                        if (sms_db[i].index == target_idx) should_delete = true;
                    } else if (delflag == 1) {
                        if (strcmp(sms_db[i].stat, "REC READ") == 0) should_delete = true;
                    } else if (delflag == 2) {
                        if (strcmp(sms_db[i].stat, "REC READ") == 0 ||
                            strcmp(sms_db[i].stat, "STO SENT") == 0) should_delete = true;
                    } else if (delflag == 3) {
                        if (strcmp(sms_db[i].stat, "REC READ") == 0 ||
                            strcmp(sms_db[i].stat, "STO SENT") == 0 ||
                            strcmp(sms_db[i].stat, "STO UNSENT") == 0) should_delete = true;
                    } else if (delflag == 4) {
                        should_delete = true;
                    }

                    if (should_delete) {
                        sms_db[i].used = false;
                        deleted_any = true;
                    }
                }

                if (delflag == 0 && !deleted_any) {
                    send_error(321, "invalid memory index");
                } else {
                    uart_write_bytes(UART_PORT, "OK\r\n", 4);
                }
            } else {
                send_error(50, "incorrect parameters");
            }
            */
            break;

        default:
            send_error(3, "operation not allowed");
            break;
    }
}


/* UNIQUEMENT POUR LES TESTS  INTERNE*/
/* --- Helper d'émission d'URC lors de la réception d'un SMS --- */
/*
void notify_new_sms(int index, const char *sender, const char *text) {
    char urc[128];

    // mt = 1 : Notification de l'emplacement de stockage (+CMTI)
    if (cnmi_cfg.mt == 1) {
        snprintf(urc, sizeof(urc), "\r\n+CMTI: \"SM\",%d\r\n", index);
        uart_write_bytes(UART_PORT, urc, strlen(urc));
    } 
    // mt = 2 : Forward direct du contenu du SMS (+CMT)
    else if (cnmi_cfg.mt == 2) {
        snprintf(urc, sizeof(urc), "\r\n+CMT: \"%s\",,\"26/08/30,22:00:00+08\"\r\n%s\r\n", sender, text);
        uart_write_bytes(UART_PORT, urc, strlen(urc));
    }
    // mt = 0 : Pas de notification (les messages restent uniquement en mémoire)
}
*/
