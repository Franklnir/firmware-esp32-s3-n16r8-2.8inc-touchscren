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
        if any(x in desc for x in ['usb serial', 'ch34', 'cp210', 'uart', 'esp']) or '303A' in hwid or '1A86' in hwid or '0403' in hwid:
            ports.append((p.device, p.description))
    return ports

print("=== XiaoZhi ESP32-S3 Auto-Flasher ===", flush=True)
print("Standby menunggu koneksi USB ESP32...", flush=True)
print("-----------------------------------------------------------------", flush=True)

xiaozhi_path = os.path.abspath('build/xiaozhi.bin')
merged_path = os.path.abspath('build/merged-binary.bin')

if os.path.exists(merged_path) and os.path.getmtime(merged_path) >= os.path.getmtime(xiaozhi_path):
    bin_path = merged_path
    offset = "0x0"
else:
    bin_path = xiaozhi_path
    offset = "0x20000"

print(f"File biner siap: {bin_path} ({os.path.getsize(bin_path)} bytes)", flush=True)

success = False
for attempt in range(1, 601):
    candidate_ports = get_ports()
    if not candidate_ports:
        if attempt % 10 == 0:
            print(f"\n[Menunggu USB - {attempt}/600] Silakan colokkan kabel data USB ESP32...", flush=True)
        time.sleep(1.5)
        continue
    
    port_device, port_desc = candidate_ports[0]
    print(f"\n[DITEMUKAN!] Port {port_device} ({port_desc}) terdeteksi! Memulai flashing...", flush=True)

    idf_python = r"C:\Users\frank\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
    py_exec = idf_python if os.path.exists(idf_python) else sys.executable

    # Flash attempt with baud 460800 first, fallback to 115200
    for baud in ["460800", "230400", "115200"]:
        print(f"Mencoba flashing pada port {port_device} (Baud {baud})...", flush=True)
        cmd = [
            py_exec, "-m", "esptool",
            "--chip", "esp32s3",
            "-p", port_device,
            "-b", baud,
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            offset, bin_path
        ]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if "Hash of data verified." in res.stdout or "Leaving... Hard resetting" in res.stdout:
            print(f"\n==========================================", flush=True)
            print(f">>> FLASHING SELESAI 100%! <<<", flush=True)
            print(f"==========================================\n", flush=True)
            print(res.stdout, flush=True)
            success = True
            break
        else:
            print(f"Gagal di baud {baud}: {res.stdout[-150:]}", flush=True)
            time.sleep(1.0)
    
    if success:
        break
    time.sleep(2.0)

if not success:
    print("\n[TIMEOUT] Peranti tidak terdeteksi atau flashing gagal.", flush=True)
    sys.exit(1)
