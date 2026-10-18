@echo off
set MSYSTEM=
set MSYSCON=
set MINGW_PREFIX=
set MINGW_CHOST=
set MINGW_PACKAGE_PREFIX=
set MSYSTEM_CARCH=
set MSYSTEM_CHOST=
cd /d C:\Users\frank\Desktop\xiaozhi-esp32-main
call C:\esp\.espressif\v5.5.2\esp-idf\export.bat >nul 2>&1
idf.py -p COM13 monitor --no-reset 2>&1 | findstr /V "FB-SIZE cam_hal"
