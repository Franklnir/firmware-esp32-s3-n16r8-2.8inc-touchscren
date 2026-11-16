# Flash firmware dan aset tema (LCDWiki ES3C28P)

Tema 1 (robot/emoji) dan Tema 2 (chat) adalah tata letak dalam firmware yang sama.
Gambar ekspresi Tema 1 dan font disimpan di partisi `assets`. Mengganti tata letak
tidak membutuhkan flash ulang aset. Perintah suara: "ganti tema 1", "ganti tema 2".

## Build

Aktifkan lingkungan ESP-IDF sesuai kebijakan SDK di README, lalu jalankan:

```powershell
python scripts/build.py lcdwiki-es3c28p --name lcdwiki-es3c28p
```

Gunakan opsi aset default (`CONFIG_FLASH_DEFAULT_ASSETS=y`). Build menghasilkan
`build/generated_assets.bin`; inilah file `assets.bin` untuk konfigurasi tersebut.
Gunakan firmware dan aset dari build yang sama.

Varian board mengaktifkan `CONFIG_LV_USE_LODEPNG=y` karena ekspresi default
berformat PNG. Jika firmware lama dibangun dengan opsi ini mati, flash ulang
firmware juga; memperbarui aset saja tidak mengaktifkan pembaca PNG.

## Flash

Periksa daftar file tanpa menulis USB:

```powershell
python flash.py --dry-run
```

Flash seluruh paket ke port perangkat, misalnya COM9:

```powershell
python flash.py COM9
```

`flash.py` membaca `build/flasher_args.json`, menyertakan bootloader, tabel partisi,
OTA data, aplikasi, dan aset. Proses berhenti jika aset hilang, serta hanya melaporkan
sukses jika esptool selesai tanpa error. `flash.bat` dan `scripts/flash_board.ps1`
memakai alur yang sama. Python memerlukan pyserial dan esptool (tersedia di lingkungan IDF).

Untuk konfigurasi LCDWiki ES3C28P 16 MB dengan `partitions/v2/16m.csv`:

| File | Alamat flash |
| --- | --- |
| `build/bootloader/bootloader.bin` | `0x0` |
| `build/partition_table/partition-table.bin` | `0x8000` |
| `build/ota_data_initial.bin` | `0xd000` |
| `build/xiaozhi.bin` | `0x20000` |
| `build/generated_assets.bin` | `0x800000` |

Alamat untuk board atau tabel partisi lain harus mengikuti manifest build-nya.
Partisi aset konfigurasi ini berukuran 8 MB. Jangan menulis file aset ke alamat aplikasi.

Setelah flash, uji di perangkat: pilih Tema 1, pastikan ekspresi muncul saat berbicara,
pindah ke Tema 2 lalu kembali ke Tema 1, dan reboot untuk memeriksa penyimpanan pilihan.
Validasi file/build tidak menggantikan pengujian layar dan flash pada perangkat fisik.
