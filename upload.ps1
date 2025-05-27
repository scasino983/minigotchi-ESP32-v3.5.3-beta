# Upload script for Minigotchi ESP32
# This script uploads the compiled firmware to an ESP32 device

# Define colors for better readability
$Green = [ConsoleColor]::Green
$Cyan = [ConsoleColor]::Cyan
$Yellow = [ConsoleColor]::Yellow
$Red = [ConsoleColor]::Red

# Function to get available COM ports
function Get-ComPorts {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    return $ports
}

# Get available COM ports
$comPorts = Get-ComPorts
if ($comPorts.Count -eq 0) {
    Write-Host "No COM ports found. Please connect your ESP32 device." -ForegroundColor $Red
    exit 1
}

# Display available COM ports
Write-Host "Available COM ports:" -ForegroundColor $Cyan
for ($i = 0; $i -lt $comPorts.Count; $i++) {
    Write-Host "[$i] $($comPorts[$i])" -ForegroundColor $Yellow
}

# Ask for COM port selection
$selection = -1
if ($comPorts.Count -eq 1) {
    $selection = 0
    Write-Host "Only one COM port available. Automatically selecting $($comPorts[0])." -ForegroundColor $Green
} else {
    while ($selection -lt 0 -or $selection -ge $comPorts.Count) {
        try {
            $selection = [int](Read-Host "Select COM port by number")
        } catch {
            Write-Host "Invalid input. Please enter a number." -ForegroundColor $Red
        }
    }
}

$selectedPort = $comPorts[$selection]
Write-Host "Selected COM port: $selectedPort" -ForegroundColor $Green

# Compile and upload with the correct partition scheme
Write-Host "Compiling and uploading Minigotchi ESP32 firmware with huge_app partition scheme..." -ForegroundColor $Green

# Try to compile first
$compileResult = arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" minigotchi-ESP32
if ($LASTEXITCODE -ne 0) {
    Write-Host "Compilation failed. Please fix the errors before uploading." -ForegroundColor $Red
    Write-Host $compileResult
    exit 1
}

Write-Host "Compilation successful. Uploading to $selectedPort..." -ForegroundColor $Green

# Upload the compiled firmware
$uploadResult = arduino-cli upload -p $selectedPort --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" minigotchi-ESP32
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload failed. Make sure your ESP32 is properly connected and the correct port is selected." -ForegroundColor $Red
    Write-Host $uploadResult
    exit 1
}

Write-Host "Upload successful!" -ForegroundColor $Green
Write-Host "You can now open the serial monitor to see debug output:" -ForegroundColor $Cyan
Write-Host "arduino-cli monitor -p $selectedPort -c baudrate=115200" -ForegroundColor $Yellow

# Offer to open the serial monitor
$openMonitor = Read-Host "Do you want to open the serial monitor now? (y/n)"
if ($openMonitor -eq "y" -or $openMonitor -eq "Y") {
    Write-Host "Opening serial monitor on $selectedPort at 115200 baud..." -ForegroundColor $Cyan
    arduino-cli monitor -p $selectedPort -c baudrate=115200
}
