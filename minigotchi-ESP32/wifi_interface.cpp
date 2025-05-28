#include "wifi_interface.h"

// Required for the implementations
#include "channel_hopper.h" // For channel_hopping_task_handle and potentially other getters if added
#include "pwnagotchi.h"     // For Pwnagotchi::stop_scan()
#include "deauth.h"         // For Deauth::is_running() and Deauth::stop()

// For C linkage when C++ classes are used by C-style function implementations
// This isn't strictly necessary here as Pwnagotchi and Deauth methods are static,
// but good practice if we were dealing with C++ objects directly in C functions.

#ifdef __cplusplus
extern "C" {
#endif

// --- Channel Hopping ---
bool is_channel_hopping() {
    // A basic check is if the task handle is not NULL.
    // For a more precise status, one might also check internal flags from channel_hopper.cpp
    // (e.g., task_should_exit, channel_hop_paused) if they were exposed via getters.
    return (channel_hopping_task_handle != NULL);
}

TaskHandle_t get_channel_hopper_task_handle() {
    return channel_hopping_task_handle;
}

// --- Pwnagotchi Scan ---
void pwnagotchi_scan_stop() {
    Pwnagotchi::stop_scan();
}

// --- Deauthentication ---
bool is_deauth_running() {
    return Deauth::is_running();
}

void deauth_stop() {
    Deauth::stop();
}

// For backward compatibility
bool is_channel_hopping_active() {
    return is_channel_hopping();
}

TaskHandle_t get_channel_hopping_task_handle() {
    return get_channel_hopper_task_handle();
}

void stop_pwnagotchi_scan() {
    pwnagotchi_scan_stop();
}

bool is_deauth_attack_running() {
    return is_deauth_running();
}

void stop_deauth_attack() {
    deauth_stop();
}

#ifdef __cplusplus
}
#endif
