[Console]::Title = "ESP-Hole Live Serial Monitor (COM7)"
Clear-Host
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "       ESP-HOLE LIVE SERIAL MONITOR (115200 BAUD)" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""

$portName = "COM7"
$baudRate = 115200

$port = New-Object System.IO.Ports.SerialPort $portName, $baudRate
# Both DTR and RTS must be false so GPIO0 is not held in download mode!
$port.DtrEnable = $false
$port.RtsEnable = $false

try {
    $port.Open()
    Write-Host "[CONNECTED TO $portName at $baudRate baud]" -ForegroundColor Green
    Write-Host "Press the physical EN (or RST) button on your ESP32 once to see the IP!`n" -ForegroundColor Yellow
} catch {
    Write-Host "[ERROR] Could not open $portName! Make sure Arduino IDE is CLOSED so the port is free." -ForegroundColor Red
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit
}

try {
    while ($true) {
        if ($port.BytesToRead -gt 0) {
            $data = $port.ReadExisting()
            Write-Host -NoNewline $data
        } else {
            Start-Sleep -Milliseconds 25
        }
    }
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
