#ifndef WIFI_FRAMES_H
#define WIFI_FRAMES_H

#include <stdint.h>

// Simplified 802.11 MAC Header (common parts)
typedef struct {
    uint16_t frame_control;
    uint16_t duration_id;
    uint8_t addr1[6]; // Receiver MAC / Destination MAC
    uint8_t addr2[6]; // Transmitter MAC / Source MAC
    uint8_t addr3[6]; // BSSID (or SA/DA depending on ToDS/FromDS)
    uint16_t seq_ctrl;
} __attribute__((packed)) ieee80211_mac_hdr_t;

// LLC/SNAP Header ( precedes EAPOL for EtherType 0x888E)
typedef struct {
    uint8_t dsap; // Always 0xAA
    uint8_t ssap; // Always 0xAA
    uint8_t ctrl; // Always 0x03
    uint8_t oui[3]; // Organizationally Unique Identifier (0x00, 0x00, 0x00 for EtherType)
    uint16_t type; // EtherType (0x888E for EAPOL)
} __attribute__((packed)) llc_snap_hdr_t;
#define LLC_SNAP_HDR_LEN 8
#define ETHER_TYPE_EAPOL 0x888E // Already in network byte order (big-endian)

// EAPOL-Key Descriptor (simplified)
typedef struct {
    uint8_t descriptor_type;
    uint16_t key_info;      // Note: This field is little-endian in the frame
    uint16_t key_length;    // Note: This field is big-endian in the frame
    uint64_t replay_counter; // Note: This field is big-endian in the frame
    uint8_t key_nonce[32];
    uint8_t eapol_key_iv[16];
    uint8_t key_rsc[8];      
    uint8_t key_id[8];       
    uint8_t key_mic[16];
    uint16_t key_data_length; // Note: This field is big-endian in the frame
    // uint8_t key_data[]; // Variable length, handle by using key_data_length
} __attribute__((packed)) eapol_key_frame_t;

#define EAPOL_KEY_FRAME_MIN_LEN (1 + 2 + 2 + 8 + 32 + 16 + 8 + 8 + 16 + 2) // Size up to key_data_length field itself

// Key Information field bits (for use AFTER converting key_info from frame to host byte order e.g. ntohs())
#define KEY_INFO_KEY_DESCRIPTOR_VERSION_MASK    0x0007 // Bits 0-2
#define KEY_INFO_KEY_TYPE_PAIRWISE              0x0008 // Bit 3 (1=Pairwise, 0=Group)
#define KEY_INFO_INSTALL_FLAG                   0x0040 // Bit 6
#define KEY_INFO_ACK_FLAG                       0x0080 // Bit 7
#define KEY_INFO_MIC_FLAG                       0x0100 // Bit 8
#define KEY_INFO_SECURE_FLAG                    0x0200 // Bit 9
#define KEY_INFO_ERROR_FLAG                     0x0400 // Bit 10
#define KEY_INFO_REQUEST_FLAG                   0x0800 // Bit 11
#define KEY_INFO_ENCRYPTED_KEY_DATA_FLAG        0x1000 // Bit 12 (RSN)
#define KEY_INFO_SMK_MESSAGE_FLAG               0x2000 // Bit 13 (WPA3)

#endif // WIFI_FRAMES_H
