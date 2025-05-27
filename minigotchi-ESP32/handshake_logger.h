#ifndef HANDSHAKE_LOGGER_H
#define HANDSHAKE_LOGGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// CSV file settings
#define MAX_CSV_FILE_NAME_LENGTH 64 // e.g., "/minigotchi_handshakes/handshakes_999.csv"
#define HANDSHAKE_CSV_DIR "/minigotchi_handshakes" // Directory for CSV files
#define HANDSHAKE_CSV_BASE_FILENAME "handshakes" // Base filename prefix

// Function declarations
esp_err_t handshake_logger_init(void); // Initialize the handshake logger (create mutex, directory)
esp_err_t handshake_logger_open_new_file(void); // Open a new CSV file for logging handshakes
void handshake_logger_close_file(void); // Close the current CSV file
esp_err_t handshake_logger_write_entry(const char* bssid, const char* station_mac, 
                                      const char* msg_type, uint8_t channel, 
                                      const char* ssid = nullptr); // Write a handshake entry to CSV
void handshake_logger_deinit(void); // Clean up resources
esp_err_t handshake_logger_get_total_handshakes(int* total_count); // Get the total number of handshakes

// Utility function to associate BSSID with SSID from scanning
esp_err_t handshake_logger_update_ssid_map(const char* bssid, const char* ssid);

#endif // HANDSHAKE_LOGGER_H
