# Minigotchi ESP32 Build Instructions

## Important: Partition Scheme

The Minigotchi firmware requires the `huge_app` partition scheme due to its size. Without this specific partition scheme, compilation will fail.

## Building and Uploading the Project

### Using Build and Upload Scripts

For convenience, we've included build and upload scripts for different platforms:

#### Building:
- **Windows Command Line**: Run `build.bat`
- **Windows PowerShell**: Run `.\build.ps1`
- **Linux/Mac**: Run `./build.sh` (make it executable first with `chmod +x build.sh`)

#### Uploading:
- **Windows Command Line**: Run `upload.bat`
- **Windows PowerShell**: Run `.\upload.ps1`
- **Linux/Mac**: Run `./upload.sh` (make it executable first with `chmod +x upload.sh`)

The upload scripts will:
1. Detect available COM ports
2. Let you select which port to use
3. Compile the firmware with the correct partition scheme
4. Upload the firmware to your ESP32
5. Offer to open a serial monitor

### Manual Build and Upload

If you prefer to build manually, use:

```
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .\minigotchi-ESP32
```

### Uploading to ESP32

After a successful build, upload to your ESP32 device with:

```
arduino-cli upload -p COM_PORT --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .\minigotchi-ESP32
```

Replace `COM_PORT` with your actual serial port (e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux).

## Channel Hopping Improvements

The channel hopping mechanism has been significantly improved to fix reliability issues. See `CHANNEL_HOPPING.md` for detailed documentation.

### Key Improvements

1. **WiFi State Management:**
   - Fixed issues with ESP32 WiFi state transitions during channel switching
   - Properly handles monitor mode toggling to prevent WiFi driver crashes
   - Added appropriate delays between state changes to allow the hardware to stabilize

2. **Adaptive Timing:**
   - Implemented dynamic timing that adjusts based on success/failure rates
   - Prevents excessive channel switching that can cause ESP32 WiFi instability
   - Gradually increases intervals after failures and decreases them after successful hops

3. **Error Recovery:**
   - Added robust error handling to prevent system lockups
   - Implements graceful failure recovery with automatic WiFi reinitialization
   - Tracks and reports channel hopping performance for troubleshooting

4. **Task Management:**
   - Improved FreeRTOS task management to prevent resource conflicts
   - Added proper task signaling and cleanup to prevent memory leaks
   - Uses correct core assignments to prevent CPU contention

### Why It Works

The previous channel hopping implementation had several issues:

1. **ESP32 WiFi Driver Sensitivity:** The ESP32 WiFi driver is sensitive to rapid state changes. The new implementation adds proper delays and state verification.

2. **Promiscuous Mode Management:** Properly preserves the promiscuous mode callback during channel switching, preventing callback loss.

3. **Resource Contention:** The improved task management prevents resource conflicts between WiFi operations and other system tasks.

4. **Adaptive Strategy:** The dynamic timing adjustments allow the system to find an optimal hopping rate based on the specific hardware and environment conditions.

## Frame Sending Improvements

The beacon frame sending mechanism has been significantly improved to fix stability issues and prevent crashes:

### Key Improvements

1. **Proper WiFi Mode Management:**
   - Added full state saving and restoration when switching between monitor mode and AP mode
   - Fixed issue where monitor mode wasn't properly disabled before sending frames
   - Added appropriate delays between WiFi state transitions

2. **Memory Management:**
   - Added proper cleanup of allocated memory for frames
   - Implemented frame size limitations based on available heap memory
   - Fixed memory leaks by ensuring frame buffers are always freed

3. **Error Handling:**
   - Added comprehensive error checking for WiFi mode transitions
   - Added memory availability checks before attempting to send frames
   - Implemented graceful error recovery to prevent crashes

4. **Sniffer Integration:**
   - Properly stops and restarts the WiFi sniffer when sending frames
   - Ensures frame sending doesn't interfere with channel hopping
   - Reduces number of packets sent based on available system resources

### Why It Works

The previous frame sending implementation had several issues:

1. **WiFi Mode Conflicts:** The ESP32 WiFi driver requires proper mode transitions between monitor mode and AP mode.
2. **Memory Leaks:** Previous implementation didn't properly free all allocated memory.
3. **Too Aggressive:** Sending too many packets too quickly could overload the WiFi driver.
4. **Incomplete State Restoration:** Failed to properly restore WiFi state after sending frames.

The new implementation addresses all these issues with a more robust approach to state management and resource handling.

## Troubleshooting

If you encounter build errors, ensure:

1. You're using the `huge_app` partition scheme
2. All required libraries are installed
3. You have the correct ESP32 board package installed

### Upload Issues

If you're having trouble uploading the firmware to your ESP32, try the following:

1.  **Check COM Port:**
    *   Ensure you have selected the correct COM port for your ESP32.
    *   You can usually find the correct port in your operating system's Device Manager (Windows) or by listing `/dev/tty*` devices (Linux/Mac).
    *   The upload scripts attempt to auto-detect ports, but manual selection might be necessary.

2.  **Bootloader Mode:**
    *   Make sure your ESP32 is in bootloader/flashing mode. For many boards, this involves:
        1.  Holding down the `BOOT` or `FLASH` button.
        2.  Pressing and releasing the `RESET` or `EN` button.
        3.  Releasing the `BOOT` or `FLASH` button.
    *   Some boards automatically enter bootloader mode when an upload is initiated.

3.  **USB Cable & Port:**
    *   Try a different USB cable. Some cables are power-only and don't support data transfer.
    *   Try a different USB port on your computer.

4.  **Drivers:**
    *   Ensure you have the correct USB-to-Serial drivers installed for your ESP32's chip (e.g., CP210x, CH340/CH341).

5.  **External Power (if applicable):**
    *   If your ESP32 is part of a larger circuit or has many peripherals, ensure it's receiving adequate power, potentially from an external power supply.

6.  **Close Serial Monitor:**
    *   Ensure no other program (like Arduino IDE's Serial Monitor, PuTTY, etc.) is connected to the COM port, as this can block the upload process.

7.  **`esptool.py` Issues (Manual Upload):**
    *   If uploading manually with `arduino-cli` (which uses `esptool.py` internally), ensure `esptool.py` is working correctly. You might need to update it or check its dependencies.

> **Note:** Channel hopping files do not affect the upload process. If you cannot upload the compiled firmware, focus on the checklist above—especially COM port selection, USB cable, drivers, and bootloader mode. If issues persist, try rebooting your computer and disconnecting other USB devices.