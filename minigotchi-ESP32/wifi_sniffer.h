#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "wifi_frames.h"
#include "pcap_logger.h"

// External interface for WiFi sniffing functionality
esp_err_t wifi_sniffer_start(void);
esp_err_t wifi_sniffer_stop(void);
bool is_sniffer_running(void);

// Called by channel hopper to set the current channel - completely separate from sniffing
esp_err_t wifi_sniffer_set_channel(uint8_t channel);

// Declare the callback function so it can be referenced internally
void wifi_promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type);

#endif // WIFI_SNIFFER_H
