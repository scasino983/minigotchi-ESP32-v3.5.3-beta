#include "wifi_sniffer.h"
#include "pcap_logger.h"  // To write packets
#include "minigotchi.h"   // For Minigotchi::getMood() and Minigotchi::monStart/Stop
#include "esp_wifi.h"     // For esp_wifi_set_promiscuous_rx_cb, etc.
#include "esp_event.h"    // For esp_event_handler_register (if needed for other events)
#include "esp_log.h"      // For ESP_LOGx macros
#include <arpa/inet.h>    // For ntohs(), ntohl()
#include "channel_hopper.h" // For channel hopping control
#include "wifi_frames.h"   // For frame parsing
#include "handshake_logger.h" // For handshake CSV logging
#include <WiFi.h> // Include WiFi.h for WiFi.mode() calls

static bool sniffer_is_active = false;
static const char *TAG_SNIFFER = "WIFI_SNIFFER"; // For ESP_LOG

// Make the callback function visible in our file scope but not static
// so it can be referenced in wifi_sniffer_set_channel
void wifi_promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type);

// ntohll definition for 64-bit integers (if not provided by toolchain/system headers)
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntohll(uint64_t x) {
    return ((uint64_t)ntohl(x & 0xFFFFFFFF) << 32) | ntohl(x >> 32);
}
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t ntohll(uint64_t x) {
    return x;
}
#else
#error "Byte order not defined for ntohll"
#endif

// The promiscuous mode callback function - separated from channel hopping
void wifi_promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!sniffer_is_active || !buf) {
        return; 
    }

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (type == WIFI_PKT_MGMT || type == WIFI_PKT_DATA) {
        if (len > 0) {
            esp_err_t err = pcap_logger_write_packet(payload, len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_SNIFFER, "Failed to write packet to PCAP. Error: %s", esp_err_to_name(err));
            }
        }
    }

    if (type == WIFI_PKT_DATA) {
        if (len < sizeof(ieee80211_mac_hdr_t) + LLC_SNAP_HDR_LEN + 4) { 
            return;
        }

        ieee80211_mac_hdr_t *mac_hdr = (ieee80211_mac_hdr_t *)payload;
        uint16_t frame_control_host = ntohs(mac_hdr->frame_control); 
        
        uint8_t frame_type = (frame_control_host >> 2) & 0x3;     // Bits 2-3 
        uint8_t frame_subtype = (frame_control_host >> 4) & 0xF; // Bits 4-7

        if (frame_type != 2 || (frame_subtype != 0 && frame_subtype != 8)) { 
            return;
        }

        uint8_t *data_ptr = payload + sizeof(ieee80211_mac_hdr_t);
        int len_remaining = len - sizeof(ieee80211_mac_hdr_t);

        if (frame_subtype == 8) { // QoS Data
            if (len_remaining < 2) return;
            data_ptr += 2; 
            len_remaining -= 2;
        }
        
        if (len_remaining < LLC_SNAP_HDR_LEN + 4) { 
            return;
        }

        llc_snap_hdr_t *llc_snap = (llc_snap_hdr_t *)data_ptr;
        if (llc_snap->dsap == 0xAA && llc_snap->ssap == 0xAA && llc_snap->ctrl == 0x03 &&
            llc_snap->oui[0] == 0x00 && llc_snap->oui[1] == 0x00 && llc_snap->oui[2] == 0x00 &&
            ntohs(llc_snap->type) == ETHER_TYPE_EAPOL) {
            
            uint8_t *eapol_frame_ptr = data_ptr + LLC_SNAP_HDR_LEN;
            len_remaining -= LLC_SNAP_HDR_LEN;

            if (len_remaining < 4) return; 

            uint8_t eapol_packet_type = eapol_frame_ptr[1]; 
            if (eapol_packet_type == 0x03) { // EAPOL-Key
                if (len_remaining < 4 + EAPOL_KEY_FRAME_MIN_LEN) {
                    ESP_LOGV(TAG_SNIFFER, "EAPOL-Key packet too short for full EAPOL Key header.");
                    return;
                }

                eapol_key_frame_t *key_frame = (eapol_key_frame_t *)(eapol_frame_ptr + 4);
                uint16_t key_info = ntohs(key_frame->key_info); 
                uint64_t replay_counter_host = ntohll(key_frame->replay_counter);

                char sa_str[18], da_str[18], bssid_str[18];
                bool to_ds = (frame_control_host & 0x0100);
                bool from_ds = (frame_control_host & 0x0200);

                if (!to_ds && !from_ds) {
                    snprintf(da_str, sizeof(da_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr1[0], mac_hdr->addr1[1], mac_hdr->addr1[2], mac_hdr->addr1[3], mac_hdr->addr1[4], mac_hdr->addr1[5]);
                    snprintf(sa_str, sizeof(sa_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr2[0], mac_hdr->addr2[1], mac_hdr->addr2[2], mac_hdr->addr2[3], mac_hdr->addr2[4], mac_hdr->addr2[5]);
                    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr3[0], mac_hdr->addr3[1], mac_hdr->addr3[2], mac_hdr->addr3[3], mac_hdr->addr3[4], mac_hdr->addr3[5]);
                } else if (!to_ds && from_ds) { 
                    snprintf(da_str, sizeof(da_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr1[0], mac_hdr->addr1[1], mac_hdr->addr1[2], mac_hdr->addr1[3], mac_hdr->addr1[4], mac_hdr->addr1[5]); 
                    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr2[0], mac_hdr->addr2[1], mac_hdr->addr2[2], mac_hdr->addr2[3], mac_hdr->addr2[4], mac_hdr->addr2[5]); 
                    snprintf(sa_str, sizeof(sa_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr3[0], mac_hdr->addr3[1], mac_hdr->addr3[2], mac_hdr->addr3[3], mac_hdr->addr3[4], mac_hdr->addr3[5]); 
                } else if (to_ds && !from_ds) { 
                    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr1[0], mac_hdr->addr1[1], mac_hdr->addr1[2], mac_hdr->addr1[3], mac_hdr->addr1[4], mac_hdr->addr1[5]); 
                    snprintf(sa_str, sizeof(sa_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr2[0], mac_hdr->addr2[1], mac_hdr->addr2[2], mac_hdr->addr2[3], mac_hdr->addr2[4], mac_hdr->addr2[5]); 
                    snprintf(da_str, sizeof(da_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac_hdr->addr3[0], mac_hdr->addr3[1], mac_hdr->addr3[2], mac_hdr->addr3[3], mac_hdr->addr3[4], mac_hdr->addr3[5]); 
                } else { 
                     strncpy(sa_str, "WDS_SA?", sizeof(sa_str));
                     strncpy(da_str, "WDS_DA?", sizeof(da_str));
                     strncpy(bssid_str, "WDS_BSSID?", sizeof(bssid_str));
                }

                String eapol_msg_type = "EAPOL-Key (Unknown)";
                bool is_pairwise = (key_info & KEY_INFO_KEY_TYPE_PAIRWISE);
                bool has_mic = (key_info & KEY_INFO_MIC_FLAG);                bool has_ack = (key_info & KEY_INFO_ACK_FLAG);
                bool is_install = (key_info & KEY_INFO_INSTALL_FLAG);
                
                if (is_pairwise && !has_mic && has_ack) eapol_msg_type = "M1 (AP to STA)"; 
                else if (is_pairwise && has_mic && !has_ack) eapol_msg_type = "M2 or M4 (STA to AP)"; 
                else if (is_pairwise && has_mic && has_ack && is_install) eapol_msg_type = "M3 (AP to STA)"; 
                
                ESP_LOGI(TAG_SNIFFER, "EAPOL-Key! SA: %s, DA: %s, BSSID: %s, Type: %s, KeyInfo: 0x%04X, ReplayCounter: %llu",
                    sa_str, da_str, bssid_str, eapol_msg_type.c_str(), key_info, replay_counter_host);
                
                // Log handshake to CSV file - determine BSSID based on frame direction
                const char* handshake_bssid = bssid_str;
                const char* station_mac = (eapol_msg_type.startsWith("M1") || eapol_msg_type.startsWith("M3")) ? 
                                           da_str : sa_str;  // Station MAC is DA for M1/M3, SA for M2/M4
                
                // Get current channel from the packet's rx_ctrl
                uint8_t current_channel = pkt->rx_ctrl.channel;
                
                // Log the handshake to the CSV file
                handshake_logger_write_entry(handshake_bssid, station_mac, 
                                           eapol_msg_type.c_str(), current_channel);
            }
        }
    }
}

esp_err_t wifi_sniffer_start(void) {
    if (sniffer_is_active) {
        Serial.println(Minigotchi::getMood().getNeutral() + " WiFi sniffer already active.");
        return ESP_OK;
    }

    // Verify WiFi state before starting sniffer
    wifi_mode_t current_mode;
    esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
    
    if (mode_err != ESP_OK) {
        Serial.printf("%s Cannot start sniffer - WiFi not in good state: %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(mode_err));
        
        // If WiFi is not initialized, initialize it
        if (mode_err == ESP_ERR_WIFI_NOT_INIT) {
            Serial.println(Minigotchi::getMood().getIntense() + " WiFi not initialized, initializing now...");
            
            // Initialize with default configuration
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            esp_err_t init_err = esp_wifi_init(&cfg);
            
            if (init_err != ESP_OK) {
                Serial.printf("%s Failed to initialize WiFi: %s\n", 
                             Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(init_err));
                return ESP_FAIL;
            }
            
            // Start WiFi with retry
            for (int retry = 0; retry < 3; retry++) {
                esp_err_t start_err = esp_wifi_start();
                if (start_err == ESP_OK) {
                    break;
                }
                
                if (retry == 2) {
                    Serial.printf("%s Failed to start WiFi after multiple attempts: %s\n", 
                                 Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(start_err));
                    esp_wifi_deinit();
                    return ESP_FAIL;
                }
                
                delay(100 * (retry + 1));
            }
        } else {
            // Some other error, try to recover
            Serial.println(Minigotchi::getMood().getIntense() + " Attempting WiFi recovery...");
            
            // Try to stop WiFi if it might be started
            esp_wifi_stop();
            delay(100);
            
            // Try to deinitialize WiFi if it might be initialized
            esp_wifi_deinit();
            delay(150);
            
            // Initialize with default configuration
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            esp_err_t init_err = esp_wifi_init(&cfg);
            
            if (init_err != ESP_OK) {
                Serial.printf("%s Failed to reinitialize WiFi: %s\n", 
                             Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(init_err));
                return ESP_FAIL;
            }
            
            // Start WiFi
            esp_err_t start_err = esp_wifi_start();
            if (start_err != ESP_OK) {
                Serial.printf("%s Failed to restart WiFi: %s\n", 
                             Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(start_err));
                esp_wifi_deinit();
                return ESP_FAIL;
            }
        }
        
        // Set to STA mode explicitly for sniffer
        esp_err_t sta_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (sta_err != ESP_OK) {
            Serial.printf("%s Failed to set WiFi to STA mode: %s\n", 
                         Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(sta_err));
            esp_wifi_stop();
            esp_wifi_deinit();
            return ESP_FAIL;
        }
        
        delay(150);
    } else if (current_mode != WIFI_MODE_STA) {
        // WiFi is initialized but not in STA mode, which is required for sniffer
        Serial.printf("%s WiFi in incorrect mode for sniffer (%d), setting to STA mode...\n", 
                     Minigotchi::getMood().getIntense().c_str(), current_mode);
        
        esp_err_t sta_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (sta_err != ESP_OK) {
            Serial.printf("%s Failed to set WiFi to STA mode: %s\n", 
                         Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(sta_err));
            return ESP_FAIL;
        }
        
        delay(150);
    }

    // Initialize PCAP logger
    Serial.println(Minigotchi::getMood().getIntense() + " Attempting to open PCAP file for sniffer...");
    if (pcap_logger_open_new_file() != ESP_OK) {
        Serial.println(Minigotchi::getMood().getBroken() + " Sniffer: Failed to open PCAP file.");
        return ESP_FAIL;
    }
    Serial.println(Minigotchi::getMood().getHappy() + " Sniffer: New PCAP file opened.");
    
    // Initialize handshake CSV logger
    Serial.println(Minigotchi::getMood().getIntense() + " Initializing handshake CSV logger...");
    if (handshake_logger_init() != ESP_OK) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to initialize handshake logger.");
        pcap_logger_close_file();
        return ESP_FAIL;
    }
    if (handshake_logger_open_new_file() != ESP_OK) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to open handshake CSV file.");
        pcap_logger_close_file();
        return ESP_FAIL;
    }
    Serial.println(Minigotchi::getMood().getHappy() + " Handshake CSV logger initialized and file opened.");

    // Start monitor mode with retry
    bool monitor_started = false;
    for (int retry = 0; retry < 3; retry++) {
        if (Minigotchi::monStart()) {
            monitor_started = true;
            break;
        }
        
        Serial.printf("%s Failed to start monitor mode (attempt %d)\n", 
                     Minigotchi::getMood().getBroken().c_str(), retry+1);
        
        if (retry < 2) {
            delay(150 * (retry + 1));
        }
    }
    
    if (!monitor_started) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to start monitor mode after multiple attempts.");
        pcap_logger_close_file();
        handshake_logger_close_file();
        return ESP_FAIL;
    }

    // Set promiscuous filter with retry
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA 
    };
    
    esp_err_t filter_err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
        filter_err = esp_wifi_set_promiscuous_filter(&filter);
        if (filter_err == ESP_OK) {
            break;
        }
        
        Serial.printf("%s Failed to set promiscuous filter (attempt %d): %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), retry+1, esp_err_to_name(filter_err));
        
        if (retry < 2) {
            delay(100 * (retry + 1));
        }
    }
    
    if (filter_err != ESP_OK) {
        Serial.printf("%s Failed to set promiscuous filter after multiple attempts: %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(filter_err));
        Minigotchi::monStop(); 
        pcap_logger_close_file();
        handshake_logger_close_file();
        return filter_err;
    }
    
    Serial.println(Minigotchi::getMood().getNeutral() + " Promiscuous filter set for MGMT and DATA frames.");

    // Set promiscuous callback with retry
    esp_err_t cb_err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
        cb_err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_callback);
        if (cb_err == ESP_OK) {
            break;
        }
        
        Serial.printf("%s Failed to set promiscuous RX callback (attempt %d): %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), retry+1, esp_err_to_name(cb_err));
        
        if (retry < 2) {
            delay(100 * (retry + 1));
        }
    }
    
    if (cb_err != ESP_OK) {
        Serial.printf("%s Failed to set promiscuous RX callback after multiple attempts: %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(cb_err));
        Minigotchi::monStop(); 
        esp_wifi_set_promiscuous_filter(NULL);
        pcap_logger_close_file();
        handshake_logger_close_file();
        return cb_err;
    }
    
    // Verify we're actually in promiscuous mode
    bool is_promiscuous = false;
    esp_wifi_get_promiscuous(&is_promiscuous);
    if (!is_promiscuous) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to enter promiscuous mode despite successful calls.");
        esp_wifi_set_promiscuous_rx_cb(NULL);
        Minigotchi::monStop();
        pcap_logger_close_file();
        handshake_logger_close_file();
        return ESP_FAIL;
    }
    
    sniffer_is_active = true;
    Serial.println(Minigotchi::getMood().getHappy() + " WiFi Sniffer started successfully.");
    ESP_LOGI(TAG_SNIFFER, "WiFi Sniffer started successfully.");

    // Start channel hopping task separately
    start_channel_hopping();

    return ESP_OK;
}

esp_err_t wifi_sniffer_stop(void) {
    if (!sniffer_is_active) {
        Serial.println(Minigotchi::getMood().getNeutral() + " WiFi sniffer not active.");
        return ESP_OK;
    }

    // Mark sniffer as inactive first to prevent callback processing
    sniffer_is_active = false;
    Serial.println(Minigotchi::getMood().getHappy() + " WiFi Sniffer stopped.");
    ESP_LOGI(TAG_SNIFFER, "WiFi Sniffer stopped.");

    // Stop the channel hopping task first
    stop_channel_hopping();

    // Disable callbacks before doing anything else
    esp_err_t cb_err = esp_wifi_set_promiscuous_rx_cb(NULL);
    if (cb_err != ESP_OK && cb_err != ESP_ERR_WIFI_NOT_STARTED && cb_err != ESP_ERR_WIFI_NOT_INIT) {
        Serial.printf("%s Error clearing promiscuous callback: %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(cb_err));
    }
    
    // Disable promiscuous mode with retries
    bool promisc_disabled = false;
    for (int retry = 0; retry < 3; retry++) {
        esp_err_t promisc_err = esp_wifi_set_promiscuous(false);
        if (promisc_err == ESP_OK || promisc_err == ESP_ERR_WIFI_NOT_STARTED || promisc_err == ESP_ERR_WIFI_NOT_INIT) {
            promisc_disabled = true;
            break;
        }
        
        Serial.printf("%s Failed to disable promiscuous mode (attempt %d): %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), retry+1, esp_err_to_name(promisc_err));
        delay(100 * (retry + 1));
    }
    
    if (!promisc_disabled) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to disable promiscuous mode after multiple attempts.");
        // Continue with cleanup anyway
    }
    
    // Clear promiscuous filter
    esp_err_t filter_err = esp_wifi_set_promiscuous_filter(NULL);
    if (filter_err != ESP_OK && filter_err != ESP_ERR_WIFI_NOT_STARTED && filter_err != ESP_ERR_WIFI_NOT_INIT) {
        Serial.printf("%s Error clearing promiscuous filter: %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(filter_err));
    }
    
    // Close files
    pcap_logger_close_file();
    handshake_logger_close_file();
    
    // Get current WiFi state
    wifi_mode_t mode;
    esp_err_t mode_err = esp_wifi_get_mode(&mode);
    
    // Check if WiFi is initialized and in a valid state
    bool wifi_initialized = (mode_err == ESP_OK);
    bool need_reset = false;
    
    if (!wifi_initialized) {
        if (mode_err == ESP_ERR_WIFI_NOT_INIT) {
            Serial.println(Minigotchi::getMood().getIntense() + " WiFi not initialized, will reinitialize...");
            need_reset = true;
        } else {
            Serial.printf("%s WiFi in unknown state: %s, performing reset...\n", 
                         Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(mode_err));
            need_reset = true;
        }
    } else if (mode != WIFI_MODE_STA) {
        Serial.printf("%s WiFi in incorrect mode: %d, resetting to STA mode...\n", 
                     Minigotchi::getMood().getBroken().c_str(), mode);
        need_reset = true;
    }
    
    // If WiFi needs to be reset, do it thoroughly
    if (need_reset) {
        // Try to stop WiFi if it might be started
        if (mode_err != ESP_ERR_WIFI_NOT_INIT) {
            esp_wifi_stop();
            delay(100);
        }
        
        // Try to deinitialize WiFi if it might be initialized
        if (mode_err != ESP_ERR_WIFI_NOT_INIT) {
            esp_wifi_deinit();
            delay(150);
        }
        
        // Initialize with default configuration
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t init_err = esp_wifi_init(&cfg);
        
        if (init_err != ESP_OK) {
            Serial.printf("%s Failed to initialize WiFi: %s\n", 
                         Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(init_err));
            return ESP_FAIL;
        }
        
        // Start WiFi
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK) {
            Serial.printf("%s Failed to start WiFi: %s\n", 
                         Minigotchi::getMood().getBroken().c_str(), esp_err_to_name(start_err));
            return ESP_FAIL;
        }
        
        delay(150);
    }
    
    // Set to STA mode for general operation, with retries
    bool sta_set = false;
    for (int retry = 0; retry < 3; retry++) {
        esp_err_t sta_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (sta_err == ESP_OK) {
            sta_set = true;
            break;
        }
        
        Serial.printf("%s Failed to set WiFi to STA mode (attempt %d): %s\n", 
                     Minigotchi::getMood().getBroken().c_str(), retry+1, esp_err_to_name(sta_err));
        delay(100 * (retry + 1));
    }
    
    if (!sta_set) {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to set WiFi to STA mode after multiple attempts.");
        return ESP_FAIL;
    }
    
    delay(150);
    
    // Double-check that promiscuous mode is really off
    bool is_promiscuous = false;
    esp_wifi_get_promiscuous(&is_promiscuous);
    if (is_promiscuous) {
        Serial.println(Minigotchi::getMood().getBroken() + " WARNING: Still in promiscuous mode after cleanup!");
        esp_wifi_set_promiscuous(false);
        delay(100);
        
        // Check again
        esp_wifi_get_promiscuous(&is_promiscuous);
        if (is_promiscuous) {
            Serial.println(Minigotchi::getMood().getBroken() + " CRITICAL: Unable to exit promiscuous mode!");
        }
    }
    
    Serial.println(Minigotchi::getMood().getHappy() + " WiFi Sniffer fully stopped and WiFi reinitialized.");
    ESP_LOGI(TAG_SNIFFER, "WiFi Sniffer fully stopped.");
    return ESP_OK;
}

bool is_sniffer_running(void){
    return sniffer_is_active;
}

// New function to set the channel without affecting sniffing state
esp_err_t wifi_sniffer_set_channel(uint8_t channel) {
    static uint8_t last_successful_channel = 1; // Default to channel 1
    
    // Sanity check the channel
    if (channel < 1 || channel > 13) {
        ESP_LOGE(TAG_SNIFFER, "Invalid channel %d requested. Using last known good channel %d.", 
                 channel, last_successful_channel);
        channel = last_successful_channel;
    }
    
    ESP_LOGI(TAG_SNIFFER, "Attempting to switch to channel %d", channel);
    
    // Save current state before making changes
    bool was_promiscuous = false;
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    
    // Get current state with error checking
    esp_err_t err_promiscuous = esp_wifi_get_promiscuous(&was_promiscuous);
    esp_err_t err_mode = esp_wifi_get_mode(&current_mode);
    
    if (err_promiscuous != ESP_OK || err_mode != ESP_OK) {
        ESP_LOGE(TAG_SNIFFER, "Failed to get WiFi state. Reinitializing WiFi...");
        
        // Full reinitialization
        WiFi.mode(WIFI_OFF);
        delay(100);
        esp_wifi_deinit();
        delay(100);
        
        // Initialize WiFi with default configuration
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        delay(100);
        
        // Reset to STA mode
        WiFi.mode(WIFI_STA);
        delay(100);
        
        // Set the saved promiscuous state
        was_promiscuous = false;
        current_mode = WIFI_MODE_STA;
    }
    
    // We need a clean temporary state for channel switching
    if (was_promiscuous) {
        esp_wifi_set_promiscuous(false);
        delay(20);
    }
    
    // Ensure in STA mode for channel switch
    if (current_mode != WIFI_MODE_STA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        delay(20);
    }
    
    // Try up to 3 times to set the channel
    esp_err_t result = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        
        if (result == ESP_OK) {
            // Verify the change
            delay(20);
            uint8_t new_channel;
            wifi_second_chan_t second;
            esp_wifi_get_channel(&new_channel, &second);
            
            if (new_channel == channel) {
                ESP_LOGI(TAG_SNIFFER, "Successfully set channel %d on attempt %d", channel, attempt);
                last_successful_channel = channel;
                break;
            } else {
                ESP_LOGW(TAG_SNIFFER, "Channel verification failed. Set: %d, Actual: %d", channel, new_channel);
                result = ESP_FAIL;
            }
        }
        
        if (result != ESP_OK && attempt < 3) {
            ESP_LOGW(TAG_SNIFFER, "Channel switch attempt %d failed. Retrying...", attempt);
            delay(50 * attempt);  // Increasing backoff
        }
    }
    
    // Restore previous WiFi state
    if (current_mode != WIFI_MODE_STA) {
        esp_wifi_set_mode(current_mode);
        delay(20);
    }
    
    if (was_promiscuous) {
        esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_callback);
        esp_wifi_set_promiscuous(true);
        delay(20);
    }
    
    return result;
}

// External function declarations from channel_hopper.cpp
extern esp_err_t start_channel_hopping();
extern void stop_channel_hopping();
