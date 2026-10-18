import sys
import time
import serial

PORT = 'COM14'
BAUD = 115200
LOG_FILE = 'monitor.log'

print(f"[MONITOR STARTED] Opening {PORT} at {BAUD} baud...", flush=True)

with open(LOG_FILE, 'a', encoding='utf-8', errors='replace') as f:
    f.write(f"\n\n--- SERIAL MONITOR SESSION STARTED AT {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
    f.flush()

while True:
    ser = None
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        ser.reset_input_buffer()
        print(f"[CONNECTED] Connected to {PORT}. Waiting for logs...", flush=True)
        with open(LOG_FILE, 'a', encoding='utf-8', errors='replace') as f:
            f.write(f"[CONNECTED] {PORT}\n")
            f.flush()
            
        while True:
            raw = ser.readline()
            if raw:
                try:
                    line = raw.decode('utf-8', errors='replace').rstrip('\r\n')
                except Exception:
                    line = str(raw)
                print(line, flush=True)
                with open(LOG_FILE, 'a', encoding='utf-8', errors='replace') as f:
                    f.write(line + '\n')
                    f.flush()
    except serial.SerialException as e:
        print(f"[SERIAL DISCONNECTED / RECONNECTING] {e}", flush=True)
        time.sleep(1.5)
    except Exception as e:
        print(f"[ERROR] {e}", flush=True)
        time.sleep(1.5)
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
