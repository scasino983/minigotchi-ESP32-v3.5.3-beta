@echo off
REM Upload script for Minigotchi ESP32
REM This script uploads the compiled firmware to an ESP32 device

echo Finding available COM ports...
setlocal EnableDelayedExpansion
set i=0
set comList=

for /f "tokens=1" %%a in ('mode ^| findstr "COM"') do (
    set port=%%a
    set port=!port::=!
    if not "!port!"=="" (
        echo [!i!] !port!
        set comList[!i!]=!port!
        set /a i+=1
    )
)

set /a comCount=i
if %comCount% EQU 0 (
    echo No COM ports found. Please connect your ESP32 device.
    exit /b 1
)

if %comCount% EQU 1 (
    echo Only one COM port available. Automatically selecting !comList[0]!.
    set comSelect=0
) else (
    set /p comSelect=Select COM port by number: 
)

set selectedPort=!comList[%comSelect%]!
echo Selected COM port: %selectedPort%

echo Compiling and uploading Minigotchi ESP32 firmware with huge_app partition scheme...

REM Try to compile first
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app minigotchi-ESP32
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed. Please fix the errors before uploading.
    exit /b 1
)

echo Compilation successful. Uploading to %selectedPort%...

REM Upload the compiled firmware
arduino-cli upload -p %selectedPort% --fqbn esp32:esp32:esp32:PartitionScheme=huge_app minigotchi-ESP32
if %ERRORLEVEL% NEQ 0 (
    echo Upload failed. Make sure your ESP32 is properly connected and the correct port is selected.
    exit /b 1
)

echo Upload successful!
echo You can now open the serial monitor to see debug output:
echo arduino-cli monitor -p %selectedPort% -c baudrate=115200

set /p openMonitor=Do you want to open the serial monitor now? (y/n) 
if /i "%openMonitor%"=="y" (
    echo Opening serial monitor on %selectedPort% at 115200 baud...
    arduino-cli monitor -p %selectedPort% -c baudrate=115200
)
