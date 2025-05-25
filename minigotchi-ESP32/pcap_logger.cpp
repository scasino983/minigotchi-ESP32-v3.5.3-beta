#include "pcap_loger.h"
#include "config.h"       // For potential future use (e.g. SD_CS_PIN if moved here)
#include "minigotchi.h"   // For Minigotchi::mood access (for Serial prints)

#include <SD.h>           // Arduino SD Card library
#include <SPI.h>          // Required for SD library if not implicitly included elsewhere
#include "freertos/FreeRTOS.h" // For FreeRTOS types
#include "freertos/semphr.h"   // For semaphores (mutexes)
#include <sys/time.h>     // For gettimeofday() (ESP32 supports this)

// Buffer settings
#define PCAP_BUFFER_SIZE 4096 // Size of the RAM buffer before flushing to SD

// Static (file scope) variables
static uint8_t pcap_ram_buffer[PCAP_BUFFER_SIZE];
static size_t pcap_buffer_offset = 0;
static File current_pcap_file; // Using Arduino SD File object
static char current_pcap_filename[MAX_PCAP_FILE_NAME_LENGTH];
static bool pcap_file_is_open = false;

static SemaphoreHandle_t pcap_mutex = NULL;

// Helper to get next file index
static int get_next_pcap_file_index(const char *base_path, const char *base_filename) {
    int max_index = -1;
    File pcapDir = SD.open(base_path);

    if (!pcapDir) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to open PCAP directory for indexing: " + String(base_path));
        return 0;
    }
    if (!pcapDir.isDirectory()) {
        Serial.println(Minigotchi::getMood().getBroken() + " " + String(base_path) + " is not a directory.");
        pcapDir.close();
        return 0;
    }

    File file = pcapDir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fname = file.name();
            if (fname.startsWith(String(base_filename) + "_")) {
                int underscore_idx = fname.lastIndexOf('_');
                int dot_idx = fname.lastIndexOf('.');
                if (underscore_idx != -1 && dot_idx != -1 && underscore_idx < dot_idx) {
                    String index_str = fname.substring(underscore_idx + 1, dot_idx);
                    if (index_str.length() > 0) {
                        bool isValidNumber = true;
                        for (size_t i = 0; i < index_str.length(); i++) {
                            if (!isDigit(index_str.charAt(i))) {
                                isValidNumber = false;
                                break;
                            }
                        }
                        if (isValidNumber) {
                            int index = index_str.toInt();
                            if (index > max_index) {
                                max_index = index;
                            }
                        }
                    }
                }
            }
        }
        file.close();
        file = pcapDir.openNextFile();
    }
    pcapDir.close();
    return max_index + 1;
}

esp_err_t pcap_logger_init(void) {
    if (pcap_mutex != NULL) {
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP logger already initialized.");
        return ESP_OK;
    }

    pcap_mutex = xSemaphoreCreateMutex();
    if (pcap_mutex == NULL) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to create PCAP mutex!");
        return ESP_FAIL;
    }

    if (!SD.exists(PCAP_DIR)) {
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP directory " + String(PCAP_DIR) + " not found, creating...");
        if (SD.mkdir(PCAP_DIR)) {
            Serial.println(Minigotchi::getMood().getHappy() + " PCAP directory created: " + String(PCAP_DIR));
        } else {
            Serial.println(Minigotchi::getMood().getBroken() + " Failed to create PCAP directory: " + String(PCAP_DIR));
            vSemaphoreDelete(pcap_mutex);
            pcap_mutex = NULL;
            return ESP_FAIL;
        }
    }
    Serial.println(Minigotchi::getMood().getHappy() + " PCAP Logger initialized.");
    pcap_buffer_offset = 0;
    pcap_file_is_open = false;
    return ESP_OK;
}

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
    if (pcap_mutex == NULL) {
      Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Logger not initialized (mutex is null). Call pcap_logger_init() first.");
      return ESP_FAIL;
    }

    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Could not take mutex for opening file.");
        return ESP_ERR_TIMEOUT;
    }

    if (pcap_file_is_open && current_pcap_file) {
        current_pcap_file.close();
        pcap_file_is_open = false;
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP: Closed existing file before opening new one.");
    }
    pcap_buffer_offset = 0;

    int next_index = get_next_pcap_file_index(PCAP_DIR, PCAP_BASE_FILENAME);
    snprintf(current_pcap_filename, MAX_PCAP_FILE_NAME_LENGTH,
             "%s/%s_%d.pcap", PCAP_DIR, PCAP_BASE_FILENAME, next_index);

    current_pcap_file = SD.open(current_pcap_filename, FILE_WRITE);
    if (!current_pcap_file) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to open new PCAP file: " + String(current_pcap_filename));
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    if (write_pcap_global_header_to_file(current_pcap_file) != ESP_OK) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to write global PCAP header to: " + String(current_pcap_filename));
        current_pcap_file.close();
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    pcap_file_is_open = true;
    Serial.println(Minigotchi::getMood().getHappy() + " Opened new PCAP file: " + String(current_pcap_filename));
    xSemaphoreGive(pcap_mutex);
    return ESP_OK;
}

esp_err_t pcap_logger_flush_buffer(void) {
    if (pcap_mutex == NULL) return ESP_FAIL;

    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Could not take mutex for flushing buffer.");
        return ESP_ERR_TIMEOUT;
    }

    if (pcap_buffer_offset == 0) {
        xSemaphoreGive(pcap_mutex);
        return ESP_OK;
    }
    if (!pcap_file_is_open || !current_pcap_file) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Flush called but file not properly open.");
        pcap_buffer_offset = 0;
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    size_t written = current_pcap_file.write(pcap_ram_buffer, pcap_buffer_offset);
    if (written != pcap_buffer_offset) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Failed to write complete buffer to SD. Written: " + String(written) + " of " + String(pcap_buffer_offset)); 
        xSemaphoreGive(pcap_mutex);
        return ESP_FAIL;
    }

    Serial.println(Minigotchi::getMood().getNeutral() + " Flushed " + String(written) + " bytes to " + String(current_pcap_filename)); // Use 'written' instead of 'pcap_buffer_offset' as offset is cleared before this line in some paths
    pcap_buffer_offset = 0;
    xSemaphoreGive(pcap_mutex);
    return ESP_OK;
}

void pcap_logger_close_file(void) {
    if (pcap_mutex == NULL) return;

    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Mutex timeout in close_file (before flush).");
        return;
    }

    if (pcap_buffer_offset > 0 && pcap_file_is_open) {
        xSemaphoreGive(pcap_mutex);
        Serial.println(Minigotchi::getMood().getNeutral() + " Flushing remaining PCAP buffer before closing file...");
        esp_err_t flush_err = pcap_logger_flush_buffer();
        if (flush_err != ESP_OK) {
             Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Error flushing buffer during close: " + String(esp_err_to_name(flush_err)));
        }
        if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) {
            Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Could not re-take mutex for closing file after flush. File may remain open.");
            return;
        }
    }

    if (current_pcap_file && pcap_file_is_open) {
        current_pcap_file.close();
        Serial.println(Minigotchi::getMood().getHappy() + " Closed PCAP file: " + String(current_pcap_filename));
    }
    pcap_file_is_open = false;
    xSemaphoreGive(pcap_mutex);
}

esp_err_t pcap_logger_write_packet(const void *packet_payload, size_t length) {
    if (pcap_mutex == NULL) return ESP_FAIL;
    if (packet_payload == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pcap_file_is_open) {
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP: File not open. Attempting to open new file before writing packet.");
        if (pcap_logger_open_new_file() != ESP_OK) {
            Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Failed to auto-open file. Packet not written.");
            return ESP_FAIL;
        }
    }

    if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Could not take mutex for writing packet.");
        return ESP_ERR_TIMEOUT;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    pcap_packet_header_t pkt_header;
    pkt_header.ts_sec = tv.tv_sec;
    pkt_header.ts_usec = tv.tv_usec;
    pkt_header.incl_len = RADIOTAP_HEADER_LEN + length;
    pkt_header.orig_len = RADIOTAP_HEADER_LEN + length;

    size_t total_packet_size_in_buffer = sizeof(pcap_packet_header_t) + RADIOTAP_HEADER_LEN + length;

    if (total_packet_size_in_buffer > PCAP_BUFFER_SIZE) {
        Serial.println(Minigotchi::getMood().getBroken() + " PCAP Error: Packet (" + String(length) + "b payload) too large for buffer (" + String(PCAP_BUFFER_SIZE) + "b). Will not write.");
        xSemaphoreGive(pcap_mutex);
        return ESP_ERR_NO_MEM;
    }

    if (pcap_buffer_offset + total_packet_size_in_buffer > PCAP_BUFFER_SIZE) {
        xSemaphoreGive(pcap_mutex);
        esp_err_t flush_err = pcap_logger_flush_buffer();
        if (flush_err != ESP_OK) {
            // Mutex should have been released by flush_buffer in case of error or success
            return flush_err;
        }
        // After successful flush, buffer is empty, re-take mutex to write current packet
        if (xSemaphoreTake(pcap_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
             Serial.println(Minigotchi::getMood().getBroken() + " PCAP: Could not re-take mutex after flushing.");
             return ESP_ERR_TIMEOUT;
        }
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
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP Logger already de-initialized or was not initialized.");
        return;
    }

    pcap_logger_close_file();

    if (pcap_mutex != NULL) {
        vSemaphoreDelete(pcap_mutex);
        pcap_mutex = NULL;
    }
    Serial.println(Minigotchi::getMood().getNeutral() + " PCAP Logger de-initialized.");
}
