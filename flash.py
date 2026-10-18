import os
import sys
import time
import subprocess
import serial.tools.list_ports as lp

def get_ports():
    ports = []
    for p in lp.comports():
        desc = p.description.lower()
        hwid = p.hwid.upper()
        if 'bluetooth' in desc:
            continue
        if any(x in desc for x in ['usb serial', 'ch34', 'cp210', 'uart', 'esp']) or '303A' in hwid or '1A86' in hwid:
            ports.append(p.device)
    return ports

print("=== XiaoZhi ESP32-S3 Flasher ===", flush=True)
print("Jika ESP32 macet di tengah penulisan sebelumnya:", flush=True)
print("1. Cabut kabel USB lalu colokkan kembali sambil menahan tombol BOOT.", flush=True)
print("   ATAU: TAHAN tombol BOOT, tekan tombol RST 1x, lalu LEPAS tombol BOOT.", flush=True)
print("-----------------------------------------------------------------", flush=True)

xiaozhi_path = os.path.abspath('build/xiaozhi.bin')
merged_path = os.path.abspath('build/merged-binary.bin')

if os.path.exists(xiaozhi_path) and (not os.path.exists(merged_path) or os.path.getmtime(xiaozhi_path) > os.path.getmtime(merged_path)):
    bin_path = xiaozhi_path
    offset = "0x20000"
else:
    bin_path = merged_path
    offset = "0x0"

print(f"File biner: {bin_path} ({os.path.getsize(bin_path)} bytes)", flush=True)

success = False
for attempt in range(1, 201):
    candidate_ports = get_ports()
    if not candidate_ports:
        print(f"\r[Menunggu USB - {attempt}/200] Silakan colokkan kabel USB / tekan BOOT + RST...", end='', flush=True)
        time.sleep(1.5)
        continue
    
    port = sys.argv[1] if len(sys.argv) > 1 else candidate_ports[0]
    print(f"\r[Percobaan {attempt}/35] Mencoba koneksi ke {port} (Baud 115200)...", end='', flush=True)

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "-b", "115200",
        "--connect-attempts", "1",
        "--before", "no_reset",
        "--after", "hard_reset",
        "write_flash",
        offset, bin_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if "Hash of data verified." in res.stdout or "Leaving... Hard resetting" in res.stdout:
        print(f"\n\n==========================================", flush=True)
        print(f">>> FLASHING SELESAI 100%! <<<", flush=True)
        print(f"==========================================\n", flush=True)
        print(res.stdout, flush=True)
        success = True
        break
    elif "Compressed" in res.stdout and "Writing" in res.stdout:
        # Started writing but maybe got error, print details
        print(f"\nOutput: {res.stdout[-200:]}", flush=True)

    # Also try default_reset
    cmd_def = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "-p", port,
        "-b", "115200",
        "--connect-attempts", "1",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        offset, bin_path
    ]
    res_def = subprocess.run(cmd_def, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if "Hash of data verified." in res_def.stdout or "Leaving... Hard resetting" in res_def.stdout:
        print(f"\n\n==========================================", flush=True)
        print(f">>> FLASHING SELESAI 100%! <<<", flush=True)
        print(f"==========================================\n", flush=True)
        print(res_def.stdout, flush=True)
        success = True
        break

    time.sleep(1.0)

if not success:
    print("\n\n[GAGAL] Tidak dapat menulis ke flash. Silakan tekan tombol RST pada ESP32.", flush=True)
    sys.exit(1)
