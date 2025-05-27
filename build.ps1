# Build script for PowerShell
# Build Minigotchi with correct partition scheme

Write-Host "Building Minigotchi ESP32 firmware with huge_app partition scheme..." -ForegroundColor Green
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" minigotchi-ESP32

Write-Host ""
Write-Host "If successful, you can upload with:" -ForegroundColor Cyan
Write-Host "arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app minigotchi-ESP32" -ForegroundColor Yellow
