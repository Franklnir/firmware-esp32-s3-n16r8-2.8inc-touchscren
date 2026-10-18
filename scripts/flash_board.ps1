# Auto-detect ESP32-S3 COM Port and Flash
Write-Host "Mencari port USB ESP32-S3..." -ForegroundColor Cyan

$port = $null
while (-not $port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count -gt 0) {
        $port = $ports[0]
        break
    }
    Write-Host "Menunggu perangkat ESP32 dicolokkan ke USB... (Tekan Ctrl+C untuk batal)" -ForegroundColor Yellow
    Start-Sleep -Seconds 2
}

Write-Host "Terdeteksi port: $port" -ForegroundColor Green
Write-Host "Memulai proses flashing..." -ForegroundColor Cyan

. C:\Espressif\v5.5.1\esp-idf\export.ps1
python -m esptool --chip esp32s3 -p $port -b 460800 --before default_reset --after hard_reset write_flash 0x0 build/merged-binary.bin

Write-Host "Selesai!" -ForegroundColor Green
