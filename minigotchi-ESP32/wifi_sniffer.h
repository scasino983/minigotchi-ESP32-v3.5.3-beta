#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the WiFi sniffer (promiscuous mode, PCAP logging)
esp_err_t wifi_sniffer_start(void);
// Stop the WiFi sniffer
esp_err_t wifi_sniffer_stop(void);
// Query if sniffer is running
bool is_sniffer_running(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SNIFFER_H
