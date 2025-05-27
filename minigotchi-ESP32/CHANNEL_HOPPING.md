# Channel Hopping Implementation Details

## Overview
This document provides details about the improved channel hopping implementation for the Minigotchi ESP32 firmware.

## Key Improvements

### 1. Adaptive Channel Hopping
- **Dynamic Timing:** The channel hopping interval automatically adjusts based on success/failure rates
- **Backoff Strategy:** Gradually increases interval between hops after failures
- **Recovery Mechanism:** Implements a recovery strategy after multiple consecutive failures

### 2. Robust Channel Switching
- **Multiple Attempts:** Makes multiple attempts to switch channels with verification
- **Clean WiFi State Management:** Properly handles WiFi mode transitions
- **Verification:** Verifies that channel switches actually happened

### 3. Improved Error Handling
- **Detailed Error Logging:** Better error reporting with ESP error codes
- **Error Recovery:** Automatically attempts to recover from failures
- **Graceful Degradation:** Can temporarily pause channel hopping when the system is under stress

### 4. Status Monitoring
- **Statistics Tracking:** Tracks successful and failed channel hops
- **Visual Feedback:** Shows channel hopping status on the display
- **Success Rate Calculation:** Calculates and displays channel hopping success rate

## Technical Implementation Details

### Root Causes of Channel Hopping Issues

The previous channel hopping implementation had several critical issues:

1. **Improper WiFi State Management:**
   - The ESP32 WiFi driver is sensitive to rapid state transitions
   - Previous implementation was turning monitor mode on/off too quickly
   - No verification that channel switches actually succeeded

2. **Callback Management Issues:**
   - In `wifi_sniffer_set_channel()`, the code incorrectly attempted to get the current promiscuous callback
   - The ESP-IDF doesn't provide an API to retrieve the current callback (`esp_wifi_get_promiscuous_rx_cb` doesn't exist)
   - This led to undefined behavior when trying to restore the callback after channel switching

3. **Task Coordination Problems:**
   - Poor synchronization between channel hopping task and main WiFi operations
   - No graceful task termination mechanism
   - Insufficient error handling when tasks failed to start or stop

4. **Fixed Timing Regardless of Success Rate:**
   - Previous implementation used fixed timing regardless of channel switching success
   - No adaptation to hardware capabilities or environmental conditions
   - Could cause system instability through excessive failed attempts

### Solutions Implemented

1. **Improved WiFi State Management:**
   - Added proper state preservation and restoration in `wifi_sniffer_set_channel()`
   - Implemented proper delays between WiFi state transitions (50ms minimum)
   - Added verification checks to confirm channel switches actually succeeded

2. **Proper Callback Handling:**
   - Made `wifi_promiscuous_rx_callback` visible at file scope by removing `static` qualifier
   - Directly re-registered the known callback after channel switching
   - Eliminated the need for the non-existent `esp_wifi_get_promiscuous_rx_cb()` function

3. **Task Management:**
   - Implemented proper signaling for task termination via atomic flags
   - Added timeout-based task cleanup to prevent resource leaks
   - Increased task stack size from 3072 to 4096 bytes for stability

4. **Adaptive Channel Hopping Strategy:**
   - Implemented an adaptive interval system (500ms to 2000ms)
   - Increases interval by 100ms after each failure
   - Decreases interval after successful hops
   - Added "recovery mode" after consecutive failures

5. **Switch Channel Implementation:**
   - Rewrote `Channel::switchChannel()` with retry logic (up to 3 attempts)
   - Added proper verification via `Channel::checkChannel()`
   - Improved error reporting with ESP-IDF error codes

## Usage Tips

1. **Monitor Success Rate:** Keep an eye on the channel hopping success rate displayed on screen. If it drops below 70%, consider adjusting the MIN_HOP_INTERVAL_MS setting.

2. **Optimal Channel List:** For best results, use only the non-overlapping 2.4GHz channels (1, 6, 11) if targeting specific networks. For wider scanning, include more channels.

3. **Recovery:** If channel hopping becomes erratic, the system will automatically attempt to recover. If problems persist, power cycle the device.

## Configuration Parameters

The following parameters can be adjusted in the channel_hopper.cpp file:

```cpp
// Configurable parameters for channel hopping
static const uint32_t MIN_HOP_INTERVAL_MS = 500;     // Minimum time between channel hops
static const uint32_t MAX_HOP_INTERVAL_MS = 2000;    // Maximum time between channel hops
static const uint32_t ADAPTIVE_HOP_INCREASE_MS = 100; // Increase interval by this much on failure
```

## Debugging

If channel hopping issues occur:

1. Check the Serial monitor for specific error codes
2. Monitor the success rate percentage 
3. Look for patterns in failures (e.g., specific channels that fail more often)
4. Observe if failures increase after extended operation (could indicate heat issues)

## Future Improvements

Potential areas for future improvement:

1. Channel prioritization based on access point detection
2. Machine learning for optimal channel sequence and timing
3. Configurable channel list through WebUI
4. Power management optimization during channel hopping
