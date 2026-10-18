@echo off
set MSYSTEM=
set MSYSCON=
set MINGW_PREFIX=
set MINGW_CHOST=
set MINGW_PACKAGE_PREFIX=
set MSYSTEM_CARCH=
set MSYSTEM_CHOST=
cd /d C:\Users\frank\Desktop\xiaozhi-esp32-main
call C:\esp\.espressif\v5.5.2\esp-idf\export.bat
set PORT=%1
if "%PORT%"=="" set PORT=COM25
echo Monitoring port %PORT%...
idf.py -p %PORT% monitor --no-reset
