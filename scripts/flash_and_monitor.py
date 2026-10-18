import os
import sys
import time
import serial
import subprocess

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM25'
BAUD = 460800
MERGED_BIN = os.path.abspath('build/merged-binary.bin')

print(f"=== XiaoZhi ESP32-S3 Auto-Flasher on {PORT} ===", flush=True)
print("----------------------------------------------------------------", flush=True)
print("Petunjuk Flash:", flush=True)
print("1. TAHAN tombol BOOT (GPIO 0) pada ESP32-S3.", flush=True)
print("2. Tekan dan LEPAS tombol RST (Reset) 1 kali (sambil tetap menahan tombol BOOT).", flush=True)
print("3. LEPAS tombol BOOT.", flush=True)
print("----------------------------------------------------------------", flush=True)
print("Menunggu ESP32-S3 masuk ke Download Mode...", flush=True)

max_attempts = 40
success = False

for attempt in range(1, max_attempts + 1):
    print(f"\r[Percobaan {attempt}/{max_attempts}] Menghubungkan ke {PORT}...", end='', flush=True)
    
    # Try with --before no_reset first (if user already held BOOT+RST)
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "-p", PORT,
        "-b", str(BAUD),
        "--connect-attempts", "1",
        "--before", "no_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "0x0", MERGED_BIN
    ]
    
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if "Hash of data verified." in res.stdout or "Leaving... Hard resetting" in res.stdout:
        print(f"\n\n>>> FLASHING BERHASIL! (Attempt {attempt}) <<<\n", flush=True)
        print(res.stdout, flush=True)
        success = True
        break
    
    # Try with --before default_reset
    cmd_def = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "-p", PORT,
        "-b", str(BAUD),
        "--connect-attempts", "1",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "0x0", MERGED_BIN
    ]
    res_def = subprocess.run(cmd_def, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if "Hash of data verified." in res_def.stdout or "Leaving... Hard resetting" in res_def.stdout:
        print(f"\n\n>>> FLASHING BERHASIL (Auto-Reset)! (Attempt {attempt}) <<<\n", flush=True)
        print(res_def.stdout, flush=True)
        success = True
        break
        
    time.sleep(1.0)

if not success:
    print("\n\n[GAGAL] Tidak dapat terhubung ke ESP32-S3.", flush=True)
    print("Silakan pastikan kabel USB tersambung dan tahan tombol BOOT saat menekan tombol RST.", flush=True)
    sys.exit(1)

print("\n=== Membuka Serial Monitor 115200 (Monitoring Tombol & Sistem) ===", flush=True)
print("(Silakan tekan Tombol 1, Tombol 2, atau Tombol 3 sekarang...)\n", flush=True)

try:
    ser = serial.Serial(PORT, 115200, timeout=0.5)
    start_t = time.time()
    while time.time() - start_t < 60:  # monitor for 60 seconds
        line = ser.readline()
        if line:
            txt = line.decode('utf-8', errors='ignore').strip()
            if any(k in txt for k in ["Button", "volume", "Volume", "Battery", "Chronchi", "InitializeButtons", "WiFi", "Audio", "YouTube"]):
                print(f"[SERIAL] {txt}", flush=True)
    ser.close()
except Exception as e:
    print(f"Serial Monitor note: {e}", flush=True)
