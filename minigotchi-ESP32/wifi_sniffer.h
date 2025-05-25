#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#include "esp_err.h" // For esp_err_t type

// Starts WiFi promiscuous mode and packet capturing.
// Opens a new PCAP file via pcap_logger.
// Returns ESP_OK on success, or an error code on failure.
esp_err_t wifi_sniffer_start(void);

// Stops WiFi promiscuous mode and packet capturing.
// Closes the current PCAP file via pcap_logger.
// Returns ESP_OK on success, or an error code on failure.
esp_err_t wifi_sniffer_stop(void);

// Checks if the sniffer is currently active.
// Returns true if active, false otherwise.
bool is_sniffer_running(void);

#endif // WIFI_SNIFFER_H