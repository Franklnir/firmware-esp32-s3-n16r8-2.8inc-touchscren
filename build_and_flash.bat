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
idf.py reconfigure
idf.py build
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
set PORT=%1
if "%PORT%"=="" set PORT=COM13
echo Using port %PORT%...

idf.py -p %PORT% -b 460800 flash
if errorlevel 1 (
    echo Retrying flash at 230400 baud...
    idf.py -p %PORT% -b 230400 flash
    if errorlevel 1 (
        echo Retrying flash at 115200 baud...
        idf.py -p %PORT% -b 115200 flash
        if errorlevel 1 (
            echo FLASH FAILED
            exit /b 1
        )
    )
)
REM Flash assets.bin to spiffs partition at 0x800000
if exist build\generated_assets.bin (
    echo Flashing assets.bin to 0x800000...
    python.exe -m esptool --chip esp32s3 --port %PORT% --baud 460800 write_flash 0x800000 build\generated_assets.bin
    if errorlevel 1 (
        python.exe -m esptool --chip esp32s3 --port %PORT% --baud 230400 write_flash 0x800000 build\generated_assets.bin
        if errorlevel 1 (
            echo ASSETS FLASH FAILED
        ) else (
            echo Assets flashed successfully
        )
    ) else (
        echo Assets flashed successfully
    )
)
echo === DONE ===
