import os
import sys
import time
import subprocess
import json
from pathlib import Path
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

build_dir = Path(__file__).resolve().parent / 'build'
try:
    manifest = json.loads((build_dir / 'flasher_args.json').read_text())
    if manifest['extra_esptool_args']['chip'] != 'esp32s3':
        raise ValueError('Build ini bukan untuk ESP32-S3.')
    assets = manifest.get('assets')
    if not assets or manifest['flash_files'].get(assets['offset']) != assets['file']:
        raise ValueError('Build tidak menyertakan assets.bin. Build ulang dengan aset aktif.')
    flash_files = []
    for offset, filename in sorted(manifest['flash_files'].items(), key=lambda item: int(item[0], 0)):
        path = (build_dir / filename).resolve()
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f'File flash tidak ada atau kosong: {path}')
        flash_files.extend([offset, str(path)])
        print(f'{offset}: {path.name} ({path.stat().st_size} bytes)', flush=True)
except (OSError, KeyError, ValueError) as error:
    sys.exit(f'Paket flash tidak valid: {error}')

if '--dry-run' in sys.argv:
    print('Pemeriksaan file selesai; firmware dan aset akan di-flash bersama. USB tidak ditulis.')
    sys.exit(0)

requested_port = sys.argv[1] if len(sys.argv) > 1 else None

success = False
for attempt in range(1, 601):
    candidate_ports = [(requested_port, 'port pilihan')] if requested_port else get_ports()
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
            *manifest['write_flash_args'],
            *flash_files
        ]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if res.returncode == 0:
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
