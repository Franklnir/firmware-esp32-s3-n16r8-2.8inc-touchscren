# LCD Wiki ES3C28P / ES3N28P

2.8 寸 ESP32-S3 智能显示模块 (2.8inch ESP32-S3 Display)

资料文档: [https://www.lcdwiki.com/2.8inch_ESP32-S3_Display](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) / [中文文档](https://www.lcdwiki.com/zh/2.8inch_ESP32-S3_Display)

## 产品规格

| 项目 | 参数 |
|------|------|
| SKU / 型号 | ES3C28P (带电容触摸) / ES3N28P (不带触摸) |
| 主控芯片 | ESP32-S3 (Xtensa LX7 双核 240MHz, ESP32-S3-WROOM-1-N16R8) |
| 存储 | 16MB QSPI Flash + 8MB OPI PSRAM |
| 无线 | Wi-Fi 2.4GHz 802.11b/g/n, 蓝牙 5.0 BLE |
| 工作电压 | 5V (USB Type-C 供电) |
| 电池接口 | 支持 3.7V 锂电池，带充放电管理 |

### 显示屏

| 项目 | 参数 |
|------|------|
| 屏幕尺寸 | 2.8" IPS TFT |
| 分辨率 | 240×320 像素 |
| 色彩 | 65K RGB565 |
| 驱动 IC | ILI9341 / ILI9341V |
| 接口 | 4线 SPI (SPI3_HOST) |
| 触摸屏 | FT6336G 电容触摸 (I2C 地址 0x38, ES3C28P 支持) |
| 背光 | PWM 调光控制 (GPIO45) |

### 音频

| 项目 | 参数 |
|------|------|
| 编解码芯片 | ES8311 (I2C 0x18 + I2S) |
| 功放芯片 | FM8002E (低电平使能 PA) |
| 麦克风 | 板载硅麦 MEMS 麦克风 |
| 采样率 | 输入/输出 24000Hz |

### 外设与指示灯

| 项目 | 型号/引脚 |
|------|-----------|
| RGB LED | WS2812 单线可寻址彩灯 (IO42) |
| 按键 | BOOT (IO0), RESET (EN) |
| 电池电压检测 | ADC1 通道 8 (IO9) |
| MicroSD 卡槽 | SDIO 4-bit (IO38, IO40, IO39, IO41, IO48, IO47) |
| 扩展接口 | 1.25mm 4P (IO2, IO3, IO14, IO21) |
| 串口调试 | TX0 (IO43), RX0 (IO44) |

---

## 完整引脚分配表

| GPIO | 功能 | 说明 |
|------|------|------|
| IO0 | BOOT 按键 | 启动时进入下载模式；运行时单击切换对话状态 |
| IO1 | PA 使能 | 音频功放使能 (低电平使能) |
| IO4 | I2S MCLK | 音频主时钟 |
| IO5 | I2S BCLK | 音频位时钟 |
| IO6 | I2S DIN | 音频输入 (MEMS 麦克风 -> ESP32-S3) |
| IO7 | I2S WS | 音频左右声道时钟 (LRC) |
| IO8 | I2S DOUT | 音频输出 (ESP32-S3 -> ES8311 喇叭) |
| IO9 | BAT ADC | 电池电压检测 (ADC1_CH8) |
| IO10 | LCD CS | 液晶屏片选 (低电平有效) |
| IO11 | LCD MOSI | 液晶屏 SPI 数据输入 |
| IO12 | LCD SCK | 液晶屏 SPI 时钟 |
| IO13 | LCD MISO | 液晶屏 SPI 数据输出 |
| IO15 | I2C SCL | 音频解码器与触摸屏共享 I2C 时钟 |
| IO16 | I2C SDA | 音频解码器与触摸屏共享 I2C 数据 |
| IO17 | 触摸 INT | 电容触摸中断 (低有效) |
| IO38 | MicroSD CLK | SDIO 时钟线 (SD Clock) |
| IO39 | MicroSD D0 | SDIO 数据线 0 (SD Data 0) |
| IO40 | MicroSD CMD | SDIO 命令线 (SD Command) |
| IO41 | MicroSD D1 | SDIO 数据线 1 (SD Data 1) |
| IO42 | RGB LED | WS2812 单颗 RGB 彩灯 |
| IO45 | LCD BL | 背光 PWM 控制 (高电平点亮) |
| IO46 | LCD DC | 液晶屏数据/命令选择 (RS) |
| IO47 | MicroSD D3 | SDIO 数据线 3 (SD Data 3) |
| IO48 | MicroSD D2 | SDIO 数据线 2 (SD Data 2) |
| EN | 硬件复位 | 与液晶屏复位共用 |

---

## 触摸屏无按键操作指南 (ES3C28P - Buttonless Touchscreen Operation)

此板型已全面适配无按键手势交互，彻底无需物理 BOOT 按键：
- **开机即摸 / 开机长按 (>1.5秒)**: 开机启动过程中触摸屏幕直接进入 Wi-Fi 配网热点模式（无需按压物理 BOOT 按键）。
- **运行中长按屏幕 (>2.5秒)**: 随时长按屏幕直接触发进入 Wi-Fi 配网模式。
- **播报时单击屏幕**: AI 说话/播放回答时轻触屏幕，立即打断播报 (Stop Speaking / Interrupt)。
- **空闲时单击屏幕**: 切换对话状态（唤醒/开始/暂停交谈）。
- **双击屏幕**: 强制进入监听录音状态 (Start Listening)。

*(注：板载物理 BOOT 按键 IO0 依然保留作为硬件备用)*

## Web 配网门户集成 (Wi-Fi Portal Integration)

在 Web 配网门户界面（192.168.4.1）中选择 **"2.8inch ESP32-S3 Display (ES3C28P Touch / ILI9341)"** 时：
- **音频选项自动锁定**: 自动选择并锁定为 `duplex_es8311` (ES8311 全双工 + FM8002E 功放)，防止误改。
- **摄像头选项自动禁用**: 自动选择并锁定为 `none`，释放所有相关引脚资源给板载外设。
- **引脚冲突检测自动放行**: 保证所有板载硬件引脚零冲突完美协同。

---

## 智能充电指示灯 (Smart WS2812 Charging LED Indicator)

板载 WS2812 RGB LED (IO42) telah terintegrasi dengan monitor baterai otomatis:
- **Sedang Mengisi Daya (Charging)**: LED RGB berkedip warna **Merah terang (Blinking Red)** setiap 500ms.
- **Baterai Penuh (Full Charged ~100%)**: LED RGB menyala konstan / solid **Hijau (atau Biru)** tanpa berkedip.
- **Operasi Baterai Normal / Charger Dicabut**: LED padam otomatis saat idle (hemat daya) dan hanya aktif mengikuti animasi interaksi XiaoZhi (Mendengarkan, Berbicara).
- **Kustomisasi Warna Penuh**: Warna baterai penuh dapat dipilih antara Hijau (default) atau Biru via tool MCP `self.system.set_charging_full_led_color`.

---

## 编译与烧录

使用 XiaoZhi 统一构建脚本一键编译：

```bash
# 编译并生成固件安装包
python scripts/build.py lcdwiki-es3c28p --zip

# 或指定语言 (如中文)
python scripts/build.py lcdwiki-es3c28p --language zh-CN --zip
```

手动 ESP-IDF 编译：

```bash
idf.py set-target esp32s3
idf.py menuconfig
# 进入: Xiaozhi Assistant -> Board Type -> 选择 "LCDWiki ES3C28P 2.8-inch ESP32-S3"
idf.py build flash monitor
```
