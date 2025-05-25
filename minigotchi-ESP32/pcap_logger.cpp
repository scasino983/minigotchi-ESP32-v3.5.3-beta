#include "pcap_logger.h"
#include "config.h"       // For SD_CS_PIN (if defined there) or other configs
#include "minigotchi.h"   // For Minigotchi::mood access

#include <SD.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <sys/time.h>     // For gettimeofday()
// For opendir, readdir, closedir used in get_next_pcap_file_index.
// These might require specific handling or an alternative approach in Arduino ESP32
// if full POSIX directory functions aren't available directly or via SD library.
// The SD library's File::openNextFile() is a more idiomatic Arduino way.
#include <dirent.h> 


// Buffer settings
#define PCAP_BUFFER_SIZE 4096 // Size of the RAM buffer before flushing to SD

// Static (file scope) variables
static uint8_t pcap_ram_buffer[PCAP_BUFFER_SIZE];
static size_t pcap_buffer_offset = 0;
static File current_pcap_file; // Using Arduino SD File object
static char current_pcap_filename[MAX_PCAP_FILE_NAME_LENGTH];
static bool pcap_file_is_open = false;

static SemaphoreHandle_t pcap_mutex = NULL;

// Helper to get next file index (adapted from Ghost ESP32 example, using SD library methods)
static int get_next_pcap_file_index(const char *base_path, const char *base_filename) {
    int max_index = -1;
    File pcapDir = SD.open(base_path);

    if (!pcapDir) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to open PCAP directory for indexing: " + String(base_path));
        return 0; 
    }
    if (!pcapDir.isDirectory()) {
        Serial.println(Minigotchi::mood.getBroken() + " " + String(base_path) + " is not a directory.");
        pcapDir.close();
        return 0;
    }

    File file = pcapDir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fname = file.name();
            // Ensure filename starts with base_filename + "_" before trying sscanf
            if (fname.startsWith(String(base_filename) + "_")) {
                int index;
                // Example: file.name() could be "eapolscan_0.pcap". We need to extract "0".
                // sscanf is tricky with Arduino String. A more robust parse might be needed.
                // For now, trying a simple approach:
                // Find the last underscore and part after it.
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
        file = pcapDir.openNextFile();
    }
    pcapDir.close(); // Close the directory handle
    return max_index + 1;
}

esp_err_t pcap_logger_init(void) {
    if (pcap_mutex != NULL) {
        Serial.println(Minigotchi::mood.getNeutral() + " PCAP logger already initialized.");
        return ESP_OK; 
    }

    pcap_mutex = xSemaphoreCreateMutex();
    if (pcap_mutex == NULL) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to create PCAP mutex!");
        return ESP_FAIL;
    }

    if (!SD.exists(PCAP_DIR)) {
        Serial.println(Minigotchi::mood.getNeutral() + " PCAP directory " + String(PCAP_DIR) + " not found, creating...");
        if (SD.mkdir(PCAP_DIR)) {
            Serial.println(Minigotchi::mood.getHappy() + " PCAP directory created: " + String(PCAP_DIR));
        } else {
            Serial.println(Minigotchi::mood.getBroken() + " Failed to create PCAP directory: " + String(PCAP_DIR));
            vSemaphoreDelete(pcap_mutex);
            pcap_mutex = NULL;
            return ESP_FAIL;
        }
    }
    Serial.println(Minigotchi::mood.getHappy() + " PCAP Logger initialized.");
    return ESP_OK;
}

// Renamed to avoid conflict if pcap_write_global_header is also global elsewhere
static esp_err_t write_pcap_global_header_to_file(File f) {
    if (!f) return ESP_FAIL;

    pcap_global_header_t header;
    header.magic_number = PCAP_MAGIC_NUMBER;
    header.version_major = PCAP_VERSION_MAJOR;
    header.version_minor = PCAP_VERSION_MINOR;
    header.thiszone = 0;
    header.sigfigs = 0;
    header.snaplen = 65535; 
    header.network = DLT_IEEE802_11_RADIO;

    size_t written = f.write((const uint8_t*)&header, sizeof(pcap_global_header_t));
    return (written == sizeof(pcap_global_header_t)) ? ESP_OK : ESP_FAIL;
}

esp_err_t pcap_logger_open_new_file(void) {
    if (pcap_file_is_open) {
        pcap_logger_close_file(); 
    }

    if (xSemaphoreTake(pcap_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not take mutex for opening file.");
        return ESP_ERR_TIMEOUT;
    }

    int next_index = get_next_pcap_file_index(PCAP_DIR, PCAP_BASE_FILENAME);
    snprintf(current_pcap_filename, MAX_PCAP_FILE_NAME_LENGTH,
             "%s/%s_%d.pcap", PCAP_DIR, PCAP_BASE_FILENAME, next_index);

    current_pcap_file = SD.open(current_pcap_filename, FILE_WRITE);
    if (!current_pcap_file) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to open new PCAP file: " + String(current_pcap_filename));
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    if (write_pcap_global_header_to_file(current_pcap_file) != ESP_OK) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to write global PCAP header to: " + String(current_pcap_filename));
        current_pcap_file.close();
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    pcap_buffer_offset = 0; 
    pcap_file_is_open = true;
    Serial.println(Minigotchi::mood.getHappy() + " Opened new PCAP file: " + String(current_pcap_filename));
    xSemaphoreGive(pcap_mutex);
    return ESP_OK;
}

// Forward declaration for use in close_file if buffer needs flushing
esp_err_t pcap_logger_flush_buffer(void); 

void pcap_logger_close_file(void) {
    if (!pcap_file_is_open) return;

    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) { // Increased timeout
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not take mutex for closing file.");
        return;
    }
    
    if (pcap_buffer_offset > 0) {
        xSemaphoreGive(pcap_mutex); // Release mutex before calling flush
        Serial.println(Minigotchi::mood.getNeutral() + " Flushing remaining PCAP buffer before closing file...");
        esp_err_t flush_err = pcap_logger_flush_buffer();
        if (flush_err != ESP_OK) {
             Serial.println(Minigotchi::mood.getBroken() + " PCAP: Error flushing buffer during close: " + String(esp_err_to_name(flush_err)));
        }
        if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) { // Re-acquire
            Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not re-take mutex for closing file after flush.");
            // Fall through to try and close file anyway, but log error
        }
    }

    if (current_pcap_file) {
        current_pcap_file.close();
        Serial.println(Minigotchi::mood.getHappy() + " Closed PCAP file: " + String(current_pcap_filename));
    }
    pcap_file_is_open = false;
    // Only give mutex if it was successfully taken and not given up by an error path
    if (pcap_mutex != NULL && xSemaphoreGetMutexHolder(pcap_mutex) == xTaskGetCurrentTaskHandle()) {
       xSemaphoreGive(pcap_mutex);
    }
}

esp_err_t pcap_logger_flush_buffer(void) {
    if (pcap_buffer_offset == 0) { // Also check if file is open
        return ESP_OK; 
    }
    if (!pcap_file_is_open) { // Added check
        Serial.println(Minigotchi::mood.getNeutral() + " PCAP: Flush called but file not open.");
        return ESP_OK;
    }


    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) { // Increased timeout
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not take mutex for flushing buffer.");
        return ESP_ERR_TIMEOUT;
    }

    if (!current_pcap_file) { 
         Serial.println(Minigotchi::mood.getBroken() + " PCAP: Flush error, file not actually open object.");
         xSemaphoreGive(pcap_mutex);
         return ESP_FAIL;
    }

    size_t written = current_pcap_file.write(pcap_ram_buffer, pcap_buffer_offset);
    if (written != pcap_buffer_offset) {
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Failed to write complete buffer to SD. Written: " + String(written) + " of " + String(pcap_buffer_offset));
        pcap_buffer_offset = 0; 
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }
    
    // current_pcap_file.flush(); // SD.h File.flush() can be time-consuming; use if essential for immediate write

    Serial.println(Minigotchi::mood.getNeutral() + " Flushed " + String(pcap_buffer_offset) + " bytes to " + String(current_pcap_filename));
    pcap_buffer_offset = 0;
    xSemaphoreGive(pcap_mutex);
    return ESP_OK;
}

esp_err_t pcap_logger_write_packet(const void *packet_payload, size_t length) {
    if (packet_payload == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // If file isn't open, try to open one. This makes it more robust if capture starts before explicit open.
    if (!pcap_file_is_open) {
        Serial.println(Minigotchi::mood.getNeutral() + " PCAP: File not open, attempting to open new file before writing packet.");
        if (pcap_logger_open_new_file() != ESP_OK) {
            Serial.println(Minigotchi::mood.getBroken() + " PCAP: Failed to auto-open file. Packet not written.");
            return ESP_FAIL;
        }
    }


    if (xSemaphoreTake(pcap_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not take mutex for writing packet.");
        return ESP_ERR_TIMEOUT;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL); 

    pcap_packet_header_t pkt_header; // Corrected Typo: pcap_packet_header_t
    pkt_header.ts_sec = tv.tv_sec;
    pkt_header.ts_usec = tv.tv_usec;
    pkt_header.incl_len = RADIOTAP_HEADER_LEN + length; 
    pkt_header.orig_len = RADIOTAP_HEADER_LEN + length;

    size_t total_packet_size_in_buffer = sizeof(pcap_packet_header_t) + RADIOTAP_HEADER_LEN + length;

    if (pcap_buffer_offset + total_packet_size_in_buffer > PCAP_BUFFER_SIZE) {
        xSemaphoreGive(pcap_mutex); 
        esp_err_t flush_err = pcap_logger_flush_buffer();
        if (flush_err != ESP_OK) {
            // If flush fails, re-take mutex before returning to maintain consistent state for caller
            if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                 Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not re-take mutex after failed flush in write_packet.");
            }
            return flush_err; 
        }
        if (xSemaphoreTake(pcap_mutex, portMAX_DELAY) != pdTRUE) { 
             Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not re-take mutex after flushing.");
             return ESP_ERR_TIMEOUT;
        }
    }
    
    if (total_packet_size_in_buffer > PCAP_BUFFER_SIZE) {
        Serial.println(Minigotchi::mood.getBroken() + " PCAP Error: Packet too large for buffer (" + String(total_packet_size_in_buffer) + " bytes).");
        xSemaphoreGive(pcap_mutex);
        return ESP_ERR_NO_MEM;
    }

    memcpy(pcap_ram_buffer + pcap_buffer_offset, &pkt_header, sizeof(pcap_packet_header_t));
    pcap_buffer_offset += sizeof(pcap_packet_header_t);

    uint8_t radiotap_header[RADIOTAP_HEADER_LEN] = {0x00, 0x00, RADIOTAP_HEADER_LEN, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(pcap_ram_buffer + pcap_buffer_offset, radiotap_header, RADIOTAP_HEADER_LEN);
    pcap_buffer_offset += RADIOTAP_HEADER_LEN;

    memcpy(pcap_ram_buffer + pcap_buffer_offset, packet_payload, length);
    pcap_buffer_offset += length;

    xSemaphoreGive(pcap_mutex);
    return ESP_OK;
}

void pcap_logger_deinit(void) {
    if (pcap_mutex == NULL) { 
        Serial.println(Minigotchi::mood.getNeutral() + " PCAP Logger already de-initialized or was not initialized.");
        return;
    }

    // Try to take the mutex to ensure exclusive access for deinitialization
    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(6000)) == pdTRUE) {
        // Call close_file. It will use the mutex internally (give, flush, take, give).
        // Since we hold the mutex here, close_file's initial take will fail if it's the same task,
        // or wait if it's a different task (which shouldn't happen here).
        // It's better if close_file does not expect the mutex to be held by the caller.
        // The version of pcap_logger_close_file used here correctly handles its own mutex needs by
        // releasing before flush and re-acquiring.
        
        // Release the mutex before calling pcap_logger_close_file, as pcap_logger_close_file will try to take it.
        xSemaphoreGive(pcap_mutex);
        pcap_logger_close_file(); 
        
        // Re-take the mutex to safely delete it.
        if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) { // Short timeout, should be free
            if (pcap_mutex != NULL) { // Check if it's still valid (it should be)
                 vSemaphoreDelete(pcap_mutex);
                 pcap_mutex = NULL;
            }
            Serial.println(Minigotchi::mood.getNeutral() + " PCAP Logger de-initialized.");
        } else {
            Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not re-take mutex for final deletion in deinit. Mutex NOT deleted.");
            // pcap_mutex is not NULLed here, as it wasn't deleted.
        }
    } else {
        Serial.println(Minigotchi::mood.getBroken() + " PCAP: Could not take mutex for deinit. File may not be closed. Mutex NOT deleted.");
    }
}
