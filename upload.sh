#!/bin/bash
# Upload script for Minigotchi ESP32
# This script uploads the compiled firmware to an ESP32 device

# Define colors for better readability
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to get available serial ports
get_serial_ports() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        ls /dev/cu.* 2>/dev/null | grep -v Bluetooth
    else
        # Linux
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
    fi
}

# Get available serial ports
PORTS=($(get_serial_ports))

if [ ${#PORTS[@]} -eq 0 ]; then
    echo -e "${RED}No serial ports found. Please connect your ESP32 device.${NC}"
    exit 1
fi

# Display available serial ports
echo -e "${CYAN}Available serial ports:${NC}"
for i in "${!PORTS[@]}"; do
    echo -e "${YELLOW}[$i] ${PORTS[$i]}${NC}"
done

# Ask for serial port selection
if [ ${#PORTS[@]} -eq 1 ]; then
    SELECTION=0
    echo -e "${GREEN}Only one serial port available. Automatically selecting ${PORTS[0]}.${NC}"
else
    read -p "Select serial port by number: " SELECTION
    
    # Validate selection
    if ! [[ "$SELECTION" =~ ^[0-9]+$ ]] || [ "$SELECTION" -ge ${#PORTS[@]} ]; then
        echo -e "${RED}Invalid selection.${NC}"
        exit 1
    fi
fi

SELECTED_PORT=${PORTS[$SELECTION]}
echo -e "${GREEN}Selected serial port: $SELECTED_PORT${NC}"

# Make sure script is executable
chmod +x ./build.sh

# Compile and upload with the correct partition scheme
echo -e "${GREEN}Compiling and uploading Minigotchi ESP32 firmware with huge_app partition scheme...${NC}"

# Try to compile first
./build.sh
if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed. Please fix the errors before uploading.${NC}"
    exit 1
fi

echo -e "${GREEN}Compilation successful. Uploading to $SELECTED_PORT...${NC}"

# Upload the compiled firmware
arduino-cli upload -p "$SELECTED_PORT" --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" minigotchi-ESP32
if [ $? -ne 0 ]; then
    echo -e "${RED}Upload failed. Make sure your ESP32 is properly connected and the correct port is selected.${NC}"
    exit 1
fi

echo -e "${GREEN}Upload successful!${NC}"
echo -e "${CYAN}You can now open the serial monitor to see debug output:${NC}"
echo -e "${YELLOW}arduino-cli monitor -p $SELECTED_PORT -c baudrate=115200${NC}"

# Offer to open the serial monitor
read -p "Do you want to open the serial monitor now? (y/n) " OPEN_MONITOR
if [[ "$OPEN_MONITOR" =~ ^[Yy]$ ]]; then
    echo -e "${CYAN}Opening serial monitor on $SELECTED_PORT at 115200 baud...${NC}"
    arduino-cli monitor -p "$SELECTED_PORT" -c baudrate=115200
fi
