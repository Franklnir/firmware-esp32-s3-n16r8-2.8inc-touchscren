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
echo [1/3] Building project...
call idf.py build
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo [2/3] Merging binaries...
call idf.py merge-bin
if errorlevel 1 (
    echo MERGE FAILED
    exit /b 1
)
echo [3/3] Copying factory binary...
if not exist "C:\Users\frank\Desktop\Paket_Firmware_ESP32_S3_N16R8_CAM" mkdir "C:\Users\frank\Desktop\Paket_Firmware_ESP32_S3_N16R8_CAM"
copy /y "build\merged-binary.bin" "C:\Users\frank\Desktop\Paket_Firmware_ESP32_S3_N16R8_CAM\esp32_s3_n16r8_cam_full_factory6.bin"
if errorlevel 1 (
    echo COPY FAILED
    exit /b 1
)
echo === SUCCESS: Factory binary esp32_s3_n16r8_cam_full_factory6.bin created ===
