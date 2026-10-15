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

#ifndef AT_HANDLER_H_
#define AT_HANDLER_H_


#include "hw_config.h"
#include "LTE_handler.h"

extern QueueHandle_t SFY_uart_queue;

#define UART_PORT SFY_UART_PORT_NUM

#define CTRL_Z             0x1A
#define ESC_KEY            0x1B

static bool echo_enabled = true; // Variable globale pour gérer l'état de l'écho (activé par défaut)
static int cmee_mode = 0;
static char sms_recipient[32] = {0}; // Stockage temporaire du numéro destinataire
static char sfy_cflo_state[16] = {0}; // Stockage de l'état du capteur CFLO 


extern int sfy_sledsav_enabled; // Économie d'énergie LED (0: Désactivé, 1: Activé)
extern int sfy_sled_mode;   // Mode de la LED (0: OFF, 1: ON, 2: BLINK)
extern int sfy_sled_timer;  // timer clignotement LED (x100ms)


// Types de commandes AT
typedef enum {
    AT_TYPE_EXEC,    // AT+CMD
    AT_TYPE_TEST,    // AT+CMD=?
    AT_TYPE_READ,    // AT+CMD?
    AT_TYPE_SET      // AT+CMD=val1,val2
} at_cmd_type_t;

// Modes du parseur
typedef enum {
    AT_MODE_COMMAND = 1,
    AT_MODE_DATA = 2,
    AT_MODE_SMS_TEXT = 3
} at_mode_t;

static at_mode_t current_mode = AT_MODE_COMMAND;

// Structure pour conserver la configuration CNMI
typedef struct {
    int mode;   // Buffer / Forwarding mode (0-3)
    int mt;     // SMS-DELIVER reporting (0-3)
    int bm;     // CBM reporting (0-3)
    int ds;     // Status report (0-2)
    int bisc;   // Buffer memory full result code (0-1)
} cnmi_config_t;

static cnmi_config_t cnmi_cfg = { .mode = 2, .mt = 1, .bm = 0, .ds = 0, .bisc = 0 };


// Structure de callback pour les commandes
typedef void (*at_handler_t)(at_cmd_type_t type, int argc, char **argv);

typedef struct {
    const char *name;
    at_handler_t handler;
} at_command_t;


at_mode_t get_current_mode(void);
void set_current_mode(at_mode_t mode);

void send_error(int err_code, const char *err_msg);
void handle_raw_data(uint8_t *data, size_t len);
void handle_sms_input(uint8_t *data, size_t len);
void notify_new_sms(int index, const char *sender, const char *text);

// Prototype des handlers
void handle_cmd_ccid(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cflo(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_sled(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_sledsav(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_ipr(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_ifc(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cgsn(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_csq(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cgmr(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cpin(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmee(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_creg(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cops(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmgf(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmgs(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cnmi(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmgl(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmgr(at_cmd_type_t type, int argc, char **argv);
void handle_cmd_cmgd(at_cmd_type_t type, int argc, char **argv);


// Table des commandes supportées
static const at_command_t cmd_table[] = {
    { "#CCID",   handle_cmd_ccid },     // Commande pour obtenir l'ID de la carte SIM
    { "#CFLO",   handle_cmd_cflo },     // Commande pour appliquer le contrôle de flux 
    { "#SLED",   handle_cmd_sled },     // Commande pour obtenir ou définir l'état de la led SLED
	{ "#SLEDSAV", handle_cmd_sledsav }, // Commande pour appliquer SLED
    { "+IPR",    handle_cmd_ipr },      // Commande pour obtenir ou définir le débit en bauds
    { "+IFC",    handle_cmd_ifc },      // Commande pour obtenir ou définir le flux de contrôle
    { "+CGSN",   handle_cmd_cgsn },     // Commande pour obtenir le numéro de série du module IMEI
    { "+CSQ",    handle_cmd_csq },      // Commande pour obtenir la qualité du signal
    { "+CGMR",   handle_cmd_cgmr },     // Commande pour obtenir la version du firmware
    { "+CPIN",   handle_cmd_cpin },     // Commande pour obtenir l'état du code PIN
    { "+CMEE",   handle_cmd_cmee },     // Commande pour obtenir ou définir le mode d'affichage des erreurs
    { "+CREG",   handle_cmd_creg },     // Commande pour obtenir l'état de l'enregistrement réseau
    { "+COPS",   handle_cmd_cops },     // Commande pour obtenir ou définir l'opérateur réseau
    { "+CMGF",   handle_cmd_cmgf },     // Commande pour obtenir ou définir le format des SMS
    { "+CMGS",   handle_cmd_cmgs },     // Commande pour envoyer un SMS
	{ "+CNMI",    handle_cmd_cnmi },    // Commande pour configurer la notification des SMS entrants
	{ "+CMGL",    handle_cmd_cmgl },    // Commande pour lister les SMS (AT+CMGL et alias AT+GMGL)
    { "+CMGR",    handle_cmd_cmgr },    // Commande pour lire un SMS (AT+CMGR et alias AT+GMGR)
	{ "+CMGD",    handle_cmd_cmgd },    // Commande pour supprimer un SMS (AT+CMGD et alias AT+GMGD)
    { "+GMGR",    handle_cmd_cmgr },    // Commande pour lire un SMS (alias AT+CMGR)
};
static const size_t cmd_table_len = sizeof(cmd_table) / sizeof(cmd_table[0]);



/* UNIQUEMENT POUR LES TESTS  INTERNE*/
/* --- Base de données SMS minimale de démonstration --- */
/*
typedef struct {
    int index;
    const char *stat;    // "REC UNREAD", "REC READ", "STO UNSENT", "STO SENT", "ALL"
    const char *sender;
    const char *date;
    const char *body;
    bool used;
} sms_item_t;

static sms_item_t sms_db[] = {
    { 1, "REC READ",   "+33612345678", "26/08/30,10:30:00+08", "Bonjour, ceci est un test", true },
    { 2, "REC UNREAD", "+33698765432", "26/08/30,11:15:00+08", "Alerte capteur ESP32",       true },
};
static const size_t sms_db_len = sizeof(sms_db) / sizeof(sms_db[0]);
*/

#endif /* AT_HANDLER_H_ */