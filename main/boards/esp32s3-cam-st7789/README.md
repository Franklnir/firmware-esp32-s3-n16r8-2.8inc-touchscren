# ESP32-S3 N16R8 CAM + ST7789 240x240 (7-Pin SPI)

Board custom untuk ESP32-S3 dengan camera OV2640 dan TFT ST7789 240x240 7-Pin (Tanpa CS).

## Spesifikasi
- Chip: ESP32-S3 (16MB Flash, 8MB PSRAM)
- Display: ST7789 240x240 SPI (7-Pin SPI, CS internal ground)
- Camera: OV2640 (built-in pada modul CAM)
- Mic: INMP441 / Codec ES8311
- Speaker: MAX98357A / NS4150B
- Backlight: PWM (GPIO 38) atau langsung 3.3V

## Pin Mapping TFT ST7789 7-Pin (SPI3)

| Pin Modul 7-Pin | GPIO ESP32-S3 | Keterangan & Panduan |
|-----------------|---------------|----------------------|
| 1. GND          | GND           | Ground daya |
| 2. VCC          | 5V (VIN) / 3.3V | **Disarankan 5V (VIN)** jika modul punya regulator LDO 3.3V onboard (662K) agar backlight terang |
| 3. SCL (SCLK)   | GPIO 19       | SPI3 Clock |
| 4. SDA (MOSI)   | GPIO 20       | SPI3 Data |
| 5. RES (RST)    | GPIO 21       | **Hardware Reset (Wajib terhubung)** untuk sinkronisasi SPI |
| 6. DC           | GPIO 47       | Data / Command |
| 7. BLK (LED)    | GPIO 38 / 3.3V | **Lampu Latar (Wajib terhubung)** ke GPIO 38 atau langsung pin 3.3V |

> **Catatan Penting Modul 7-Pin:**
> - Modul 7-Pin **TIDAK memiliki pin CS** (jalur CS sudah disolder ke GND secara internal pada PCB layar).
> - Port USB ESP32-S3: Pastikan kabel data dicolok ke port **UART** (CH340), **BUKAN** port native USB, karena GPIO 19 & 20 digunakan untuk USB native jika port USB dicolok.

## Pin Mapping TFT ST7789 8-Pin (1.69" 240x280 & 1.54" 240x240)

| Pin Modul 8-Pin | GPIO ESP32-S3 | Keterangan & Panduan |
|-----------------|---------------|----------------------|
| 1. GND          | GND           | Ground daya |
| 2. VCC          | 3.3V          | Power VCC 3.3V |
| 3. SCL (SCLK)   | GPIO 19       | SPI3 Clock |
| 4. SDA (MOSI)   | GPIO 20       | SPI3 Data |
| 5. RES (RST)    | GPIO 21       | Hardware Reset |
| 6. DC           | GPIO 47       | Data / Command |
| 7. CS           | GPIO 45       | Chip Select (Wajib) |
| 8. BLK (BL)     | GPIO 38       | Backlight PWM |

## Build & Flash
```bash
# Via batch file:
build_and_flash.bat
```
