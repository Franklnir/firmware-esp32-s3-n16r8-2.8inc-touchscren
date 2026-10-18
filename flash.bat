@echo off
cd /d C:\Users\frank\Desktop\xiaozhi-esp32-main
call C:\esp\.espressif\v5.5.2\esp-idf\export.bat >nul 2>&1

echo ====================================================================
echo Flashing XiaoZhi ESP32-S3 (Baud Rate Stabil: 230400)
echo ====================================================================

python flash.py %1

pause
