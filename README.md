Code du module ESP32 WROOM N16R8 sous ESP-IDF 6.0.2 et VSCODE

Support des commande AT envoyées par l'alamre somfy protexiom.


Interpretation et interface avec le module LTE



AT 

ATE0

#CCID   	// Commande pour obtenir l'ID de la carte SIM

#CFLO   	// Commande pour appliquer le contrôle de flux 

#SLED   	// Commande pour obtenir ou définir l'état de la led SLED

#SLEDSAV	// Commande pour appliquer SLED

+IPR    	// Commande pour obtenir ou définir le débit en bauds

+IFC    	// Commande pour obtenir ou définir le flux de contrôle

+CGSN   	// Commande pour obtenir le numéro de série du module IMEI

+CSQ    	// Commande pour obtenir la qualité du signal

+CGMR   	// Commande pour obtenir la version du firmware

+CPIN   	// Commande pour obtenir l'état du code PIN

+CMEE   	// Commande pour obtenir ou définir le mode d'affichage des erreurs

+CREG   	// Commande pour obtenir l'état de l'enregistrement réseau

+COPS   	// Commande pour obtenir ou définir l'opérateur réseau

+CMGF   	// Commande pour obtenir ou définir le format des SMS

+CMGS   	// Commande pour envoyer un SMS

+CNMI   	// Commande pour configurer la notification des SMS entrants

+CMGL   	// Commande pour lister les SMS (AT+CMGL et alias AT+GMGL)

+CMGR   	// Commande pour lire un SMS (AT+CMGR et alias AT+GMGR)

+CMGD   	// Commande pour supprimer un SMS (AT+CMGD et alias AT+GMGD)

+GMGR   	// Commande pour lire un SMS (alias AT+CMGR)



Support de la commande AT envoyées par Le module LTE.

+CMTI:    // Détection des notifications d'arrivée de SMS (URC: +CMTI: "ME", <index>)



Pas de support de la commande RING

Gestion du powerup du module LTE
