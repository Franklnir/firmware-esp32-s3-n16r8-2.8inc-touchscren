import asyncio
from bleak import BleakScanner, BleakClient

CHRONOS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
CHRONOS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

async def main():
    print("Scanning for 'Chronchi' BLE device...")
    device = await BleakScanner.find_device_by_name("Chronchi", timeout=10.0)
    if not device:
        print("Chronchi not found in scan! Trying direct scan...")
        devices = await BleakScanner.discover(timeout=5.0)
        for d in devices:
            if d.name and "chron" in d.name.lower():
                device = d
                break
    
    if not device:
        print("Device 'Chronchi' not found!")
        return

    print(f"Found {device.name} [{device.address}]! Connecting...")
    async with BleakClient(device) as client:
        print(f"Connected: {client.is_connected}")
        
        # Build Chronos notification packet:
        # Command 0x72 (Notification), Icon 0x0A (WhatsApp), State 0x02 (New notification)
        msg_str = "Budi: Halo XiaoZhi, apa kabar hari ini?"
        msg_bytes = msg_str.encode('utf-8')
        
        # Chronos packet format:
        # [0xAB, 0x00, total_len, 0xFF, 0x72, 0x80, icon, state, msg...]
        total_len = 5 + len(msg_bytes)
        pkt = bytearray([
            0xAB,
            (total_len >> 8) & 0xFF,
            total_len & 0xFF,
            0xFF,
            0x72, # cmd = notification
            0x80,
            0x0A, # WhatsApp icon
            0x02  # State = new notification
        ]) + msg_bytes

        print(f"Sending test WhatsApp notification ({len(pkt)} bytes): '{msg_str}'")
        await client.write_gatt_char(CHRONOS_RX_UUID, pkt, response=False)
        print("Notification sent successfully! Waiting 15s to observe response...")
        await asyncio.sleep(15)

if __name__ == "__main__":
    asyncio.run(main())
