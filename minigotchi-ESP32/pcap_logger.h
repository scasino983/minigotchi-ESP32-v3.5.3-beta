#ifndef PCAP_LOGGER_H
#define PCAP_LOGGER_H

#include "esp_err.h" // For esp_err_t
#include <stdint.h>  // For standard integer types
#include <stddef.h>  // For size_t

// PCAP Magic Number
#define PCAP_MAGIC_NUMBER 0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

// Data Link Type for 802.11 + Radiotap header
#define DLT_IEEE802_11_RADIO 127 // Link type for raw 802.11 frames with radiotap

#define MAX_PCAP_FILE_NAME_LENGTH 64 // e.g., "/minigotchi_pcaps/eapolscan_999.pcap"
#define PCAP_DIR "/minigotchi_pcaps" 
#define PCAP_BASE_FILENAME "eapolscan"

// PCAP global header structure
typedef struct {
  uint32_t magic_number;
  uint16_t version_major;
  uint16_t version_minor;
  int32_t  thiszone;       // GMT to local correction (usually 0)
  uint32_t sigfigs;        // Accuracy of timestamps (usually 0)
  uint32_t snaplen;        // Max length of captured packets (typically 65535)
  uint32_t network;        // Data link type
} pcap_global_header_t;

// PCAP packet header structure
typedef struct {
  uint32_t ts_sec;         // Timestamp seconds
  uint32_t ts_usec;        // Timestamp microseconds
  uint32_t incl_len;       // Number of octets of packet saved in file
  uint32_t orig_len;       // Actual length of packet (on the wire)
} pcap_packet_header_t;

// Radiotap constants
#define RADIOTAP_HEADER_LEN 8 // Minimal radiotap header (version, pad, len, present_flags)

// Public function declarations
esp_err_t pcap_logger_init(void); // Initializes mutex, checks/creates PCAP directory
esp_err_t pcap_logger_open_new_file(void);
void pcap_logger_close_file(void);
esp_err_t pcap_logger_write_packet(const void *packet_payload, size_t length);
esp_err_t pcap_logger_flush_buffer(void); // Made public for explicit flush if needed
void pcap_logger_deinit(void); // Cleans up (closes file, deletes mutex)

#endif // PCAP_LOGGER_H
