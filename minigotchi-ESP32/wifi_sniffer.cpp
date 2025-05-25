#include "wifi_sniffer.h"
#include "pcap_logger.h"  // To write packets
#include "minigotchi.h"   // For Minigotchi::mood and Minigotchi::monStart/Stop
#include "esp_wifi.h"     // For esp_wifi_set_promiscuous_rx_cb, etc.
#include "esp_event.h"    // For esp_event_handler_register (if needed for other events)
#include "esp_log.h"      // For ESP_LOGI (optional, Serial prints are also fine)

static bool sniffer_is_active = false;
static const char *TAG_SNIFFER = "WIFI_SNIFFER"; // For ESP_LOG

// The promiscuous mode callback function
// This function will be called for every packet received when promiscuous mode is enabled.
static void wifi_promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!sniffer_is_active) {
        return; // Should not happen if callback is only registered when active
    }

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    
    // For now, let's try to log MGMT and DATA frames as per Ghost example reference
    // Later, this can be refined to specific types like EAPOL.
    if (type == WIFI_PKT_MGMT || type == WIFI_PKT_DATA) {
        // pkt->payload contains the 802.11 frame.
        // pkt->rx_ctrl.sig_len is the length of the payload.
        if (pkt->rx_ctrl.sig_len > 0) {
            esp_err_t err = pcap_logger_write_packet(pkt->payload, pkt->rx_ctrl.sig_len);
            if (err != ESP_OK) {
                // Log error (Serial print or ESP_LOGE) - avoid too much serial print in callback
                // For now, let it be silent to avoid flooding if there's an SD write issue.
                // Later, we can add a counter for failed writes.
            }
        }
    }
    // Other packet types (WIFI_PKT_CTRL, WIFI_PKT_MISC) are ignored for now.
}

esp_err_t wifi_sniffer_start(void) {
    if (sniffer_is_active) {
        Serial.println(Minigotchi::mood.getNeutral() + " WiFi sniffer already active.");
        return ESP_OK;
    }

    // Initialize PCAP file - this will open a new file or use an existing open one
    // It's better to open the file just before starting the sniffer.
    if (pcap_logger_open_new_file() != ESP_OK) {
        Serial.println(Minigotchi::mood.getBroken() + " Sniffer: Failed to open PCAP file.");
        return ESP_FAIL;
    }
    Serial.println(Minigotchi::mood.getIntense() + " Sniffer: New PCAP file opened.");


    // Put WiFi into monitor mode (using existing Minigotchi class method)
    Minigotchi::monStart(); // This calls esp_wifi_set_promiscuous(true) internally

    // Set a filter for packet types (MGMT and DATA)
    // This filter is applied *before* packets reach the promiscuous_rx_cb
    wifi_promiscuous_filter_t filter = {
        // Filter for all Management and Data frames
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA 
    };
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
    if (err != ESP_OK) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to set promiscuous filter. Error: " + String(esp_err_to_name(err)));
        Minigotchi::monStop(); // Revert monitor mode
        pcap_logger_close_file(); // Close the PCAP file
        return err;
    }
    Serial.println(Minigotchi::mood.getNeutral() + " Promiscuous filter set for MGMT and DATA frames.");

    // Register the promiscuous mode callback
    err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_callback);
    if (err != ESP_OK) {
        Serial.println(Minigotchi::mood.getBroken() + " Failed to set promiscuous RX callback. Error: " + String(esp_err_to_name(err)));
        Minigotchi::monStop(); // Revert monitor mode
        esp_wifi_set_promiscuous_filter(NULL); // Clear filter
        pcap_logger_close_file(); // Close the PCAP file
        return err;
    }
    
    // esp_wifi_set_promiscuous(true) is already called by Minigotchi::monStart()
    // If Minigotchi::monStart() doesn't call it, uncomment the line below.
    // ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));


    sniffer_is_active = true;
    Serial.println(Minigotchi::mood.getHappy() + " WiFi Sniffer started successfully.");
    ESP_LOGI(TAG_SNIFFER, "WiFi Sniffer started successfully."); // Using ESP_LOG for example

    return ESP_OK;
}

esp_err_t wifi_sniffer_stop(void) {
    if (!sniffer_is_active) {
        Serial.println(Minigotchi::mood.getNeutral() + " WiFi sniffer not active.");
        return ESP_OK;
    }

    // Disable promiscuous mode and unregister callback
    esp_err_t err = esp_wifi_set_promiscuous(false); // This is also in Minigotchi::monStop()
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) { // Ignore error if wifi not started
         Serial.println(Minigotchi::mood.getBroken() + " Error stopping promiscuous mode: " + String(esp_err_to_name(err)));
    }
    esp_wifi_set_promiscuous_rx_cb(NULL); // Deregister callback
    esp_wifi_set_promiscuous_filter(NULL); // Clear filter
    
    Minigotchi::monStop(); // Puts interface back to STA mode (or preferred default)

    pcap_logger_close_file(); // Flushes buffer and closes current PCAP file

    sniffer_is_active = false;
    Serial.println(Minigotchi::mood.getHappy() + " WiFi Sniffer stopped.");
    ESP_LOGI(TAG_SNIFFER, "WiFi Sniffer stopped.");
    return ESP_OK;
}

bool is_sniffer_running(void){
    return sniffer_is_active;
}
