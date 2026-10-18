import re
import pathlib

sdk = pathlib.Path('sdkconfig')
if not sdk.exists():
    sdk = pathlib.Path('build/sdkconfig')
board_cfg = {
    'CONFIG_BOARD_TYPE_ESP32S3_CAM_ST7789': 'y',
    'CONFIG_USE_DEFAULT_MESSAGE_STYLE': 'y',
    'CONFIG_USE_WECHAT_MESSAGE_STYLE': 'n',
    'CONFIG_USE_MULTILINE_CHAT_MESSAGE': 'n',
    'CONFIG_USE_HOTSPOT_WIFI_PROVISIONING': 'y',
    'CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING': 'n',
    'CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED': 'y',
    'CONFIG_XIAOZHI_CAMERA_HMIRROR': 'n',
    'CONFIG_XIAOZHI_CAMERA_VFLIP': 'n',
}

txt = sdk.read_text()
for key, val in board_cfg.items():
    line = f'{key}={val}'
    pat = re.compile(r'^' + re.escape(key) + r'=.*$', re.MULTILINE)
    if pat.search(txt):
        txt = pat.sub(line, txt)
    else:
        txt += '\n' + line
sdk.write_text(txt)
print('Board config applied to build/sdkconfig')
