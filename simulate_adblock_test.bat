@echo off
color 0b
title ESP-Hole Live Simulation Tester
echo ========================================================
echo        ESP-HOLE: LIVE SIMULATION TESTER
echo ========================================================
echo.
echo Your ESP-Hole is flashed and running on your Wi-Fi!
echo.
set /p ESP_IP="Enter your ESP32 IP address (e.g. 192.168.1.27): "

if "%ESP_IP%"=="" (
    echo [ERROR] No IP entered!
    pause
    exit /b 1
)

echo.
echo [1/3] Opening Live Web Dashboard in your browser...
start http://%ESP_IP%/

echo.
echo [2/3] Simulating Ad and Tracker Queries through ESP-Hole...
echo --------------------------------------------------------

echo [*] Testing AD DOMAIN: googleads.g.doubleclick.net ...
nslookup -timeout=2 googleads.g.doubleclick.net %ESP_IP%
echo.

echo [*] Testing AD DOMAIN: pagead2.googlesyndication.com ...
nslookup -timeout=2 pagead2.googlesyndication.com %ESP_IP%
echo.

echo [*] Testing AD DOMAIN: applovin.com ...
nslookup -timeout=2 applovin.com %ESP_IP%
echo.

echo [3/3] Simulating Clean Domain Queries through ESP-Hole...
echo --------------------------------------------------------

echo [*] Testing CLEAN DOMAIN: google.com ...
nslookup -timeout=3 google.com %ESP_IP%
echo.

echo [*] Testing CLEAN DOMAIN: wikipedia.org ...
nslookup -timeout=3 wikipedia.org %ESP_IP%
echo.

echo ========================================================
echo  TEST COMPLETE!
echo  Check your browser at http://%ESP_IP%/ to see:
echo   - Total Queries: 5
echo   - Ads Blocked: 3 (Resolved to 0.0.0.0)
echo   - Block Rate: 60%%
echo ========================================================
echo.
pause
