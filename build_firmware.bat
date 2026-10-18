@echo off
call C:\Espressif\v5.5.1\esp-idf\export.bat
set "PATH=C:\Users\frank\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%"
python scripts\build.py lcdwiki-es3c28p --name lcdwiki-es3c28p
