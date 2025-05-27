#!/bin/bash
# Build script for Minigotchi with correct partition scheme

echo "Building Minigotchi ESP32 firmware with huge_app partition scheme..."
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app minigotchi-ESP32

echo ""
echo "If successful, you can upload with:"
echo "arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app minigotchi-ESP32"
