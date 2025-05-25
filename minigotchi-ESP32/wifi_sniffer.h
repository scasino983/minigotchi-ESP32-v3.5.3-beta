#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#include "esp_err.h"

esp_err_t wifi_sniffer_start(void);
esp_err_t wifi_sniffer_stop(void);
bool is_sniffer_running(void);

#endif // WIFI_SNIFFER_H
