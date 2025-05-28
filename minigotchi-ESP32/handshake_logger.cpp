#include "handshake_logger.h"
#include "config.h"       // For SD_CS_PIN (if defined there) or other configs
#include "minigotchi.h"   // For Minigotchi::getMood() access
#include "wifi_frames.h"  // For EAPOL message types
#include "display_variables.h" // For display variables

#include <SD.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <sys/time.h>     // For gettimeofday()
#include <map>
#include <string>

// Static variables
static File current_csv_file;
static char current_csv_filename[MAX_CSV_FILE_NAME_LENGTH];
static bool csv_file_is_open = false;
static SemaphoreHandle_t csv_mutex = NULL;

// Map to store BSSID to SSID relationships
static std::map<std::string, std::string> bssid_to_ssid_map;

// Global counter for handshakes captured in current session
static int handshake_count = 0;

// Helper function to get next file index
static int get_next_csv_file_index(const char *base_path, const char *base_filename) {
    int max_index = -1;
    File csvDir = SD.open(base_path);

    if (!csvDir) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to open handshake CSV directory for indexing: " + String(base_path));
        return 0; 
    }
    if (!csvDir.isDirectory()) {
        Serial.println(Minigotchi::getMood().getBroken() + " " + String(base_path) + " is not a directory.");
        csvDir.close();
        return 0;
    }

    File file = csvDir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fname = file.name();
            // Ensure filename starts with base_filename + "_" before trying to extract index
            if (fname.startsWith(String(base_filename) + "_")) {
                int index;
                int underscore_idx = fname.lastIndexOf('_');
                int dot_idx = fname.lastIndexOf('.');
                if (underscore_idx != -1 && dot_idx != -1 && underscore_idx < dot_idx) {
                    String index_str = fname.substring(underscore_idx + 1, dot_idx);
                    index = index_str.toInt(); // Convert string to int
                    if (index > max_index) {
                        max_index = index;
                    }
                }
            }
        }
        file.close(); // Close the file handle
        file = csvDir.openNextFile();
    }
    csvDir.close(); // Close the directory handle
    return max_index + 1;
}

esp_err_t handshake_logger_init(void) {
    if (csv_mutex != NULL) {
        Serial.println(Minigotchi::getMood().getNeutral() + " Handshake logger already initialized.");
        return ESP_OK; 
    }

    csv_mutex = xSemaphoreCreateMutex();
    if (csv_mutex == NULL) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to create handshake CSV mutex!");
        return ESP_FAIL;
    }

    if (!SD.exists(HANDSHAKE_CSV_DIR)) {
        Serial.println(Minigotchi::getMood().getNeutral() + " Handshake CSV directory " + String(HANDSHAKE_CSV_DIR) + " not found, creating...");
        if (SD.mkdir(HANDSHAKE_CSV_DIR)) {
            Serial.println(Minigotchi::getMood().getHappy() + " Handshake CSV directory created: " + String(HANDSHAKE_CSV_DIR));
        } else {
            Serial.println(Minigotchi::getMood().getBroken() + " Failed to create handshake CSV directory: " + String(HANDSHAKE_CSV_DIR));
            vSemaphoreDelete(csv_mutex);
            csv_mutex = NULL;
            return ESP_FAIL;
        }
    }
    Serial.println(Minigotchi::getMood().getHappy() + " Handshake logger initialized.");
    return ESP_OK;
}

esp_err_t handshake_logger_open_new_file(void) {
    if (csv_file_is_open) {
        handshake_logger_close_file(); 
    }

    if (xSemaphoreTake(csv_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " Handshake CSV: Could not take mutex for opening file.");
        return ESP_ERR_TIMEOUT;
    }

    int next_index = get_next_csv_file_index(HANDSHAKE_CSV_DIR, HANDSHAKE_CSV_BASE_FILENAME);
    snprintf(current_csv_filename, MAX_CSV_FILE_NAME_LENGTH,
             "%s/%s_%d.csv", HANDSHAKE_CSV_DIR, HANDSHAKE_CSV_BASE_FILENAME, next_index);

    current_csv_file = SD.open(current_csv_filename, FILE_WRITE);
    if (!current_csv_file) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to open new handshake CSV file: " + String(current_csv_filename));
        xSemaphoreGive(csv_mutex);
        return ESP_FAIL;
    }

    // Write CSV header
    if (current_csv_file.println("timestamp,bssid,station_mac,ssid,message_type,channel") == 0) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to write header to handshake CSV file: " + String(current_csv_filename));
        current_csv_file.close();
        xSemaphoreGive(csv_mutex);
        return ESP_FAIL;
    }

    csv_file_is_open = true;
    Serial.println(Minigotchi::getMood().getHappy() + " Opened new handshake CSV file: " + String(current_csv_filename));
    xSemaphoreGive(csv_mutex);
    return ESP_OK;
}

void handshake_logger_close_file(void) {
    if (!csv_file_is_open) return;

    if (xSemaphoreTake(csv_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) { 
        Serial.println(Minigotchi::getMood().getBroken() + " Handshake CSV: Could not take mutex for closing file.");
        return;
    }
    
    if (current_csv_file) {
        current_csv_file.close();
        Serial.println(Minigotchi::getMood().getHappy() + " Closed handshake CSV file: " + String(current_csv_filename));
    }
    csv_file_is_open = false;
    xSemaphoreGive(csv_mutex);
}

esp_err_t handshake_logger_update_ssid_map(const char* bssid, const char* ssid) {
    if (!bssid || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(csv_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Update the BSSID to SSID mapping
    bssid_to_ssid_map[std::string(bssid)] = std::string(ssid);
    
    xSemaphoreGive(csv_mutex);
    return ESP_OK;
}

esp_err_t handshake_logger_write_entry(const char* bssid, const char* station_mac, 
                                      const char* msg_type, uint8_t channel, 
                                      const char* ssid) {
    if (!bssid || !station_mac || !msg_type) {
        return ESP_ERR_INVALID_ARG;
    }

    // If file isn't open, try to open one
    if (!csv_file_is_open) {
        Serial.println(Minigotchi::getMood().getNeutral() + " Handshake CSV: File not open, attempting to open new file before writing entry.");
        if (handshake_logger_open_new_file() != ESP_OK) {
            Serial.println(Minigotchi::getMood().getBroken() + " Handshake CSV: Failed to auto-open file. Entry not written.");
            return ESP_FAIL;
        }
    }

    if (xSemaphoreTake(csv_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " Handshake CSV: Could not take mutex for writing entry.");
        return ESP_ERR_TIMEOUT;
    }

    // Get timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // Look up SSID in the map if not provided
    std::string found_ssid;
    if (ssid) {
        found_ssid = ssid;
    } else {
        std::string bssid_str(bssid);
        auto it = bssid_to_ssid_map.find(bssid_str);
        if (it != bssid_to_ssid_map.end()) {
            found_ssid = it->second;
        } else {
            found_ssid = "unknown";
        }
    }
    
    // Format: timestamp,bssid,station_mac,ssid,message_type,channel
    String entry = String(tv.tv_sec) + "," + 
                   String(bssid) + "," + 
                   String(station_mac) + "," + 
                   String(found_ssid.c_str()) + "," + 
                   String(msg_type) + "," + 
                   String(channel);
                   
    if (current_csv_file.println(entry) == 0) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to write entry to handshake CSV file.");
        xSemaphoreGive(csv_mutex);
        return ESP_FAIL;
    }
    
    // Flush immediately to ensure data is written to SD card
    current_csv_file.flush();
    
    Serial.println(Minigotchi::getMood().getHappy() + " Recorded handshake with BSSID: " + String(bssid) + 
                  ", SSID: " + String(found_ssid.c_str()) + ", Type: " + String(msg_type));
      // Increment the handshake count
    handshake_count++;
    
    // Update the global handshake count for display
    handshakeCount = handshake_count;
    
    xSemaphoreGive(csv_mutex);
    return ESP_OK;
}

// Function to get total handshakes (for now just returns the session count)
esp_err_t handshake_logger_get_total_handshakes(int* total_count) {
    if (total_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For now, return the in-memory counter, but could be enhanced to count from files
    *total_count = handshake_count;
    return ESP_OK;
}

void handshake_logger_deinit(void) {
    if (csv_mutex == NULL) { 
        Serial.println(Minigotchi::getMood().getNeutral() + " Handshake logger already de-initialized or was not initialized.");
        return;
    }

    // Try to take the mutex to ensure exclusive access for deinitialization
    if (xSemaphoreTake(csv_mutex, pdMS_TO_TICKS(6000)) == pdTRUE) {
        if (csv_file_is_open && current_csv_file) {
            current_csv_file.close();
            csv_file_is_open = false;
        }
        
        // Clear SSID map
        bssid_to_ssid_map.clear();
        
        xSemaphoreGive(csv_mutex);
        vSemaphoreDelete(csv_mutex);
        csv_mutex = NULL;
        Serial.println(Minigotchi::getMood().getNeutral() + " Handshake logger de-initialized.");
    } else {
        Serial.println(Minigotchi::getMood().getBroken() + " Handshake logger could not be de-initialized properly (mutex timeout).");
    }
}
