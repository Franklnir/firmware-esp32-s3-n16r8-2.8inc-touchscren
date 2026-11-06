#include "application.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "led/circular_strip.h"
#include "mcp_server.h"
#include "wifi_board.h"
#include "youtube_audio_poller.h"
#include "settings.h"
#include "chronchi/chronchi_display.h"
#include "chronchi/chronchi_alert.h"
#include "nova_display.h"
#include <esp_lvgl_port.h>
#if CONFIG_BT_ENABLED
#include "chronchi/chronchi_ble.h"
#endif
#include <esp_mac.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <driver/temperature_sensor.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>

#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <dirent.h>
#include <sys/stat.h>

#define TAG "LCDWikiES3C28P"

class SdCardManager {
public:
    SdCardManager() = default;
    ~SdCardManager() {
        Unmount();
    }

    bool Mount(bool format_if_unformatted = true) {
        if (mounted_) return true;

        ESP_LOGI("SdCard", "Initializing MicroSD Card on SDIO 4-bit (CLK:%d, CMD:%d, D0:%d, D1:%d, D2:%d, D3:%d)...",
                 SDMMC_PIN_CLK, SDMMC_PIN_CMD, SDMMC_PIN_D0, SDMMC_PIN_D1, SDMMC_PIN_D2, SDMMC_PIN_D3);

        // Aktifkan internal pull-up resistor pada jalur bus SDMMC untuk integritas sinyal ESP32-S3
        gpio_pullup_en(SDMMC_PIN_CMD);
        gpio_pullup_en(SDMMC_PIN_D0);
        gpio_pullup_en(SDMMC_PIN_D1);
        gpio_pullup_en(SDMMC_PIN_D2);
        gpio_pullup_en(SDMMC_PIN_D3);

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20MHz standard

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.clk = SDMMC_PIN_CLK;
        slot_config.cmd = SDMMC_PIN_CMD;
        slot_config.d0  = SDMMC_PIN_D0;
        slot_config.d1  = SDMMC_PIN_D1;
        slot_config.d2  = SDMMC_PIN_D2;
        slot_config.d3  = SDMMC_PIN_D3;
        slot_config.width = 4;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = format_if_unformatted,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false
        };

        // Percobaan 1: 4-bit SDIO Mode (High Speed 20MHz)
        esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card_);
        if (ret != ESP_OK) {
            ESP_LOGW("SdCard", "4-bit SD mount failed (%s), fallback ke 1-bit mode...", esp_err_to_name(ret));
            slot_config.width = 1;
            ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card_);
        }
        if (ret != ESP_OK) {
            ESP_LOGW("SdCard", "1-bit SD mount failed (%s), fallback ke low freq probing (400kHz)...", esp_err_to_name(ret));
            host.max_freq_khz = SDMMC_FREQ_PROBING;
            slot_config.width = 1;
            ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card_);
        }

        if (ret == ESP_OK && card_) {
            mounted_ = true;
            uint64_t cap_mb = ((uint64_t)card_->csd.capacity * card_->csd.sector_size) / (1024 * 1024);
            ESP_LOGI("SdCard", "MicroSD mounted successfully! Name: %s, Cap: %llu MB, Width: %d-bit",
                     card_->cid.name, (unsigned long long)cap_mb, (int)slot_config.width);

            // Buat folder khusus musik di SD card
            mkdir("/sdcard/music", 0777);

            // Tulis file selamat datang untuk memverifikasi kemampuan tulis ke MicroSD
            FILE* f = fopen("/sdcard/welcome.txt", "w");
            if (f) {
                fprintf(f, "MicroSD SDIO 4-Bit aktif di Nova OS!\nKapasitas: %llu MB\n", (unsigned long long)cap_mb);
                fclose(f);
            }
            return true;
        }

        ESP_LOGW("SdCard", "MicroSD card not detected or mount failed: %s", esp_err_to_name(ret));
        card_ = nullptr;
        mounted_ = false;
        return false;
    }

    bool Format() {
        Unmount();
        ESP_LOGI("SdCard", "Memformat MicroSD ke FAT32...");
        return Mount(true);
    }

    void Unmount() {
        if (mounted_ || card_) {
            if (card_) {
                esp_vfs_fat_sdcard_unmount("/sdcard", card_);
                card_ = nullptr;
            }
            mounted_ = false;
            ESP_LOGI("SdCard", "MicroSD card unmounted");
        }
    }

    SdCardStatus GetStatus() {
        SdCardStatus st;
        if (!mounted_) {
            Mount();
        }

        st.mounted = mounted_;
        st.inserted = mounted_;
        if (!mounted_ || !card_) {
            st.error = "Tidak ada kartu terpasang";
            return st;
        }

        snprintf(st.card_name, sizeof(st.card_name), "%s", card_->cid.name);
        
        uint64_t total_bytes = 0, free_bytes = 0;
        if (esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes) == ESP_OK) {
            st.total_mb = total_bytes / (1024 * 1024);
            st.free_mb = free_bytes / (1024 * 1024);
        } else {
            st.total_mb = ((uint64_t)card_->csd.capacity * card_->csd.sector_size) / (1024 * 1024);
            st.free_mb = st.total_mb;
        }

        int count = 0;
        // Pindai folder /sdcard/music terlebih dahulu untuk lagu-lagu tersimpan
        DIR* mdir = opendir("/sdcard/music");
        if (mdir) {
            struct dirent* entry;
            while ((entry = readdir(mdir)) != nullptr) {
                if (entry->d_name[0] == '.') continue;
                count++;
                if (st.files.size() < 64) {
                    std::string path = std::string("/sdcard/music/") + entry->d_name;
                    struct stat s;
                    size_t sz = 0;
                    if (stat(path.c_str(), &s) == 0) {
                        sz = s.st_size;
                    }
                    std::string disp_name = std::string("music/") + entry->d_name;
                    st.files.push_back({disp_name, sz});
                }
            }
            closedir(mdir);
        }

        DIR* dir = opendir("/sdcard");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] == '.') continue;
                if (strcmp(entry->d_name, "music") == 0) continue; // Sudah dipindai di atas
                count++;
                if (st.files.size() < 64) {
                    std::string path = std::string("/sdcard/") + entry->d_name;
                    struct stat s;
                    size_t sz = 0;
                    if (stat(path.c_str(), &s) == 0) {
                        sz = s.st_size;
                    }
                    st.files.push_back({entry->d_name, sz});
                }
            }
            closedir(dir);
            st.file_count = count;
        }
        return st;
    }

private:
    sdmmc_card_t* card_ = nullptr;
    bool mounted_ = false;
};

class TouchDriver {
public:
    TouchDriver() : dev_(nullptr) {}

    bool WriteReg(uint8_t reg, uint8_t val) {
        if (!dev_) return false;
        uint8_t buf[2] = {reg, val};
        return i2c_master_transmit(dev_, buf, 2, 20) == ESP_OK;
    }

    bool Init(i2c_master_bus_handle_t bus, uint8_t addr) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
            .scl_wait_us = 0,
            .flags = {},
        };
        if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) return false;

        // Inisialisasi parameter register FT6336 agar responsif & sensitif
        WriteReg(0x00, 0x00); // DEV_MODE: Normal operating mode
        WriteReg(0x80, 22);   // THGROUP: Touch threshold lebih sensitif untuk sentuhan ringan
        WriteReg(0x85, 20);   // THDIFF: Filter threshold
        WriteReg(0x86, 0x00); // CTRL: Tetap aktif (hindari masuk sleep/idle premature)
        WriteReg(0x87, 10);   // TIME_ENTER_MONITOR: 10s sebelum monitor idle
        WriteReg(0x88, 12);   // PERIODACTIVE: 12ms active scan cycle (~80Hz)
        return true;
    }

    bool Read(bool &touched, uint16_t &x, uint16_t &y) {
        touched = false;
        x = y = 0;
        if (!dev_) return false;

        uint8_t reg = 0x02;
        uint8_t buf[5];
        esp_err_t err = i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 20);
        if (err != ESP_OK) {
            // Immediate retry once on transient I2C arbitration / stretch
            err = i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 20);
            if (err != ESP_OK) return false;
        }

        uint8_t points = buf[0] & 0x0F;
        uint8_t event_flag = (buf[1] >> 6) & 0x03;

        // Sentuhan valid jika points >= 1, atau event_flag = Down (0) / Contact (2)
        bool has_touch = (points >= 1 && points <= 2) || (points == 0 && (event_flag == 0 || event_flag == 2));
        if (!has_touch) return true;

        uint16_t raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
        uint16_t raw_y = ((buf[3] & 0x0F) << 8) | buf[4];

        // Basic bounds check
        if (raw_x > 350 || raw_y > 350) return true;

        touched = true;
        x = raw_x;
        y = raw_y;
        return true;
    }

private:
    i2c_master_dev_handle_t dev_;
};

class LCDWikiBatteryMonitor {
public:
    LCDWikiBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float r_upper, float r_lower)
        : adc_unit_(adc_unit), adc_channel_(adc_channel),
          divider_ratio_((r_upper + r_lower) / r_lower) {

        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = adc_unit_,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        esp_err_t err = adc_oneshot_new_unit(&init_config, &adc_handle_);
        if (err != ESP_OK) {
            ESP_LOGE("LCDWikiBattery", "Failed to init ADC unit: %s", esp_err_to_name(err));
            return;
        }

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_oneshot_config_channel(adc_handle_, adc_channel_, &chan_config);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = adc_unit_,
            .chan = adc_channel_,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle_) == ESP_OK) {
            calibrated_ = true;
            ESP_LOGI("LCDWikiBattery", "ADC Curve Fitting calibration initialized");
        }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = adc_unit_,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle_) == ESP_OK) {
            calibrated_ = true;
            ESP_LOGI("LCDWikiBattery", "ADC Line Fitting calibration initialized");
        }
#endif
        if (!calibrated_) {
            ESP_LOGW("LCDWikiBattery", "ADC calibration not available, using linear approximation");
        }

        // Restore persistent battery level & calibration from NVS
        Settings b_set("battery", true);
        saved_soc_ = b_set.GetInt("last_soc", -1);
        saved_mv_ = b_set.GetInt("last_mv", -1);
        int cali_x1000 = b_set.GetInt("cali_x1000", 1000);
        if (cali_x1000 >= 500 && cali_x1000 <= 2000) {
            cali_factor_ = cali_x1000 / 1000.0f;
        }
        if (saved_soc_ >= 0) {
            ESP_LOGI("LCDWikiBattery", "Restored persistent battery from NVS: %d%% (%dmV), CaliFactor=%.3f",
                     saved_soc_, saved_mv_, cali_factor_);
        }

        Update(true);
    }

    ~LCDWikiBatteryMonitor() {
        SaveStateNow();
        if (cali_handle_) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            adc_cali_delete_scheme_curve_fitting(cali_handle_);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            adc_cali_delete_scheme_line_fitting(cali_handle_);
#endif
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }

    // Layer 1: Hardware Oversampling (32 samples) + ADC Factory Calibration + Divider + User Calibration Factor
    float ReadRawVoltageMv() {
        if (!adc_handle_) return 0.0f;
        int raw_sum = 0;
        int valid_samples = 0;
        for (int i = 0; i < 32; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc_handle_, adc_channel_, &raw) == ESP_OK) {
                raw_sum += raw;
                valid_samples++;
            }
        }
        if (valid_samples == 0) return 0.0f;
        int raw_avg = raw_sum / valid_samples;

        int pin_mv = 0;
        if (calibrated_ && cali_handle_) {
            adc_cali_raw_to_voltage(cali_handle_, raw_avg, &pin_mv);
        } else {
            pin_mv = (raw_avg * 3100) / 4095;
        }

        return (float)pin_mv * divider_ratio_ * cali_factor_;
    }

    // Layer 3: Physical Li-Ion Non-Linear Lookup Table (11-point piecewise linear interpolation)
    static int CalculateLiIonPercentage(int v_mv) {
        static const struct {
            int mv;
            int pct;
        } kVoltTable[11] = {
            { 3300,   0 },
            { 3500,   5 },
            { 3600,  10 },
            { 3680,  20 },
            { 3740,  30 },
            { 3800,  45 },
            { 3870,  60 },
            { 3950,  75 },
            { 4050,  88 },
            { 4150,  96 },
            { 4200, 100 }
        };

        if (v_mv <= kVoltTable[0].mv) return 0;
        if (v_mv >= kVoltTable[10].mv) return 100;

        for (int i = 0; i < 10; i++) {
            if (v_mv >= kVoltTable[i].mv && v_mv <= kVoltTable[i + 1].mv) {
                int delta_v = kVoltTable[i + 1].mv - kVoltTable[i].mv;
                int delta_p = kVoltTable[i + 1].pct - kVoltTable[i].pct;
                float ratio = (float)(v_mv - kVoltTable[i].mv) / (float)delta_v;
                return kVoltTable[i].pct + (int)std::round(ratio * delta_p);
            }
        }
        return 0;
    }

    // Layer 4A: Dynamic Charger IR Drop Compensation (tapering saat mendekati 4.20V pada fase CV)
    static int GetCompensatedVoltageMv(int v_mv, bool charging) {
        if (!charging) return v_mv;
        const int kMaxOffsetMv = 150; // Rata-rata lonjakan IR Drop saat arus cas masuk (~0.15V)
        if (v_mv >= 4200) return v_mv; // Saat 4.20V (CV penuh), arus cas mendekati 0
        if (v_mv > 4000) {
            // Tapering bertahap antara 4000mV s/d 4200mV
            float scale = (4200.0f - (float)v_mv) / 200.0f;
            return v_mv - (int)(kMaxOffsetMv * scale);
        }
        return v_mv - kMaxOffsetMv;
    }

    void Update(bool force = false) {
        if (!adc_handle_) return;
        std::lock_guard<std::mutex> lock(mutex_);

        uint32_t now = esp_timer_get_time() / 1000;
        if (!force && last_update_ms_ > 0 && (now - last_update_ms_) < 100) {
            return;
        }
        last_update_ms_ = now;

        // Layer 1: Baca tegangan rata-rata oversampled
        float raw_vbat_mv = ReadRawVoltageMv();
        if (raw_vbat_mv <= 0.0f) return;

        // Inisialisasi awal saat boot
        if (!initial_detected_) {
            initial_detected_ = true;
            fast_voltage_mv_ = raw_vbat_mv;
            slow_voltage_mv_ = raw_vbat_mv;
            baseline_voltage_mv_ = raw_vbat_mv;
            last_voltage_mv_ = (int)slow_voltage_mv_;

            // Di atas 4220mV saat boot biasanya ada charger aktif yang menaikkan rel
            is_charging_ = (raw_vbat_mv >= 4220.0f);

            int comp_v = GetCompensatedVoltageMv((int)slow_voltage_mv_, is_charging_);
            int init_pct = CalculateLiIonPercentage(comp_v);

            // Anti-sag boot: Jika tersimpan di NVS dan selisih wajar, pertahankan level tersimpan
            if (saved_soc_ >= 0 && saved_soc_ <= 100) {
                if (!is_charging_) {
                    if (init_pct <= saved_soc_ && init_pct >= saved_soc_ - 15) {
                        displayed_percentage_ = saved_soc_;
                    } else {
                        displayed_percentage_ = init_pct;
                    }
                } else {
                    displayed_percentage_ = std::max(init_pct, saved_soc_);
                }
            } else {
                displayed_percentage_ = init_pct;
            }

            last_jump_check_ms_ = now;
            last_percent_step_ms_ = now;
            ESP_LOGI("LCDWikiBattery", "Initial battery state: Vbat=%dmV, %s, SoC=%d%%",
                     (int)slow_voltage_mv_, is_charging_ ? "Charging" : "Discharging", displayed_percentage_);
            return;
        }

        // Layer 2: Dual EMA Filter
        // Fast filter (alpha=0.40): Responsif untuk mendeteksi step jump colok/cabut charger
        fast_voltage_mv_ = (0.40f * raw_vbat_mv) + (0.60f * fast_voltage_mv_);
        // Slow filter (alpha=0.08): Sangat halus & stabil untuk tampilan UI
        slow_voltage_mv_ = (0.08f * raw_vbat_mv) + (0.92f * slow_voltage_mv_);
        last_voltage_mv_ = (int)slow_voltage_mv_;

        // Layer 4B: Deteksi Lonjakan Colok / Cabut Charger (setiap 1.5 detik)
        if (now - last_jump_check_ms_ >= 1500) {
            last_jump_check_ms_ = now;
            float delta_v = fast_voltage_mv_ - baseline_voltage_mv_;

            if (!is_charging_) {
                // Lonjakan >= 35mV dalam 1.5s atau tegangan mendekati jenuh charger (>= 4180mV) -> Charger Dicolok!
                if (delta_v >= 35.0f || slow_voltage_mv_ >= 4180.0f) {
                    is_charging_ = true;
                    last_percent_step_ms_ = now;
                    ESP_LOGI("LCDWikiBattery", "⚡ Charger connected! DeltaV=+%.1fmV, Vbat=%dmV",
                             delta_v, (int)slow_voltage_mv_);
                }
            } else {
                // Penurunan drastis <= -30mV dalam 1.5s atau tegangan drop -> Charger Dicabut!
                if (delta_v <= -30.0f || slow_voltage_mv_ < 3400.0f) {
                    is_charging_ = false;
                    last_percent_step_ms_ = now;
                    ESP_LOGI("LCDWikiBattery", "🔋 Charger disconnected! DeltaV=%.1fmV, Vbat=%dmV",
                             delta_v, (int)slow_voltage_mv_);
                }
            }
            baseline_voltage_mv_ = fast_voltage_mv_;
        }

        // Layer 4C: Slew-Rate Limiter (Anti-Lonjakan Persentase)
        int comp_v = GetCompensatedVoltageMv((int)slow_voltage_mv_, is_charging_);
        int target_perc = CalculateLiIonPercentage(comp_v);

        if (is_charging_) {
            // SAAT DICAS: Persentase TIDAK BOLEH turun, dan hanya boleh naik maksimal +1% tiap 15 detik
            if (target_perc > displayed_percentage_) {
                if (now - last_percent_step_ms_ >= 15000) {
                    displayed_percentage_++;
                    last_percent_step_ms_ = now;
                }
            }
        } else {
            // SAAT PAKAI BATERAI: Persentase TIDAK BOLEH naik sendiri.
            // Turun maksimal 1% tiap 10 detik untuk meredam voltage sag saat Wi-Fi tx atau audio keras
            if (target_perc < displayed_percentage_) {
                if (now - last_percent_step_ms_ >= 10000) {
                    displayed_percentage_--;
                    last_percent_step_ms_ = now;
                }
            } else if (target_perc > displayed_percentage_ + 4) {
                // Recovery jika beban berat berakhir: naik sangat lambat (30 detik per 1%)
                if (now - last_percent_step_ms_ >= 30000) {
                    displayed_percentage_++;
                    last_percent_step_ms_ = now;
                }
            }
        }
        displayed_percentage_ = std::clamp(displayed_percentage_, 0, 100);

        // NVS Persistence (simpan setiap 60s jika persentase berubah)
        if (displayed_percentage_ >= 0 && displayed_percentage_ != last_saved_soc_) {
            if (last_nvs_save_ms_ == 0 || (now - last_nvs_save_ms_ >= 60000)) {
                last_nvs_save_ms_ = now;
                last_saved_soc_ = displayed_percentage_;
                Settings b_set("battery", true);
                b_set.SetInt("last_soc", displayed_percentage_);
                b_set.SetInt("last_mv", (int)slow_voltage_mv_);
            }
        }
    }

    void SaveStateNow() {
        if (displayed_percentage_ >= 0) {
            Settings b_set("battery", true);
            b_set.SetInt("last_soc", displayed_percentage_);
            b_set.SetInt("last_mv", (int)slow_voltage_mv_);
            last_saved_soc_ = displayed_percentage_;
            ESP_LOGI("LCDWikiBattery", "Saved battery state to NVS: %d%% (%dmV)", displayed_percentage_, (int)slow_voltage_mv_);
        }
    }

    bool IsCharging() {
        Update();
        return is_charging_;
    }

    bool IsDischarging() {
        return !IsCharging();
    }

    int GetBatteryLevel() {
        Update();
        return std::clamp(displayed_percentage_, 0, 100);
    }

    int GetVoltageMv() const {
        return last_voltage_mv_;
    }

    int GetRawAdc() const {
        if (!adc_handle_) return 0;
        int raw = 0;
        adc_oneshot_read(adc_handle_, adc_channel_, &raw);
        return raw;
    }

    void SetCalibrationFactor(float factor) {
        if (factor >= 0.5f && factor <= 2.0f) {
            cali_factor_ = factor;
            Settings b_set("battery", true);
            b_set.SetInt("cali_x1000", (int)(factor * 1000.0f + 0.5f));
            ESP_LOGI("LCDWikiBattery", "Calibration factor updated: %.4f", factor);
        }
    }

    float GetCalibrationFactor() const {
        return cali_factor_;
    }

private:
    adc_unit_t adc_unit_;
    adc_channel_t adc_channel_;
    float divider_ratio_;
    float cali_factor_ = 1.0f;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t cali_handle_ = nullptr;
    bool calibrated_ = false;

    float fast_voltage_mv_ = 0.0f;
    float slow_voltage_mv_ = 0.0f;
    float baseline_voltage_mv_ = 0.0f;
    int last_voltage_mv_ = 0;

    int displayed_percentage_ = -1;
    bool is_charging_ = false;
    bool initial_detected_ = false;

    uint32_t last_update_ms_ = 0;
    uint32_t last_jump_check_ms_ = 0;
    uint32_t last_percent_step_ms_ = 0;
    uint32_t last_nvs_save_ms_ = 0;
    int last_saved_soc_ = -1;
    int saved_soc_ = -1;
    int saved_mv_ = -1;
    std::mutex mutex_;
};

class LCDWikiLed : public CircularStrip {
public:
    LCDWikiLed(gpio_num_t gpio, uint16_t max_leds) : CircularStrip(gpio, max_leds) {
        Settings b_set("battery", true);
        use_blue_for_full_ = (b_set.GetInt("full_color", 0) == 1);
    }

    void SetBatteryMonitor(LCDWikiBatteryMonitor* monitor) {
        battery_monitor_ = monitor;
    }

    void SetFullChargeColor(bool use_blue) {
        use_blue_for_full_ = use_blue;
        Settings b_set("battery", true);
        b_set.SetInt("full_color", use_blue ? 1 : 0);
        if (current_visual_ == kVisualFull) {
            current_visual_ = kVisualNone;
            UpdateChargingLed();
        }
    }

    bool IsUsingBlueForFull() const {
        return use_blue_for_full_;
    }

    void OnStateChanged() override {
        auto& app = Application::GetInstance();
        auto device_state = app.GetDeviceState();
        if (device_state == kDeviceStateIdle) {
            UpdateChargingLed();
            return;
        }

        // Interaksi AI aktif (Mendengarkan, Berbicara, Menghubungkan, Konfigurasi Wi-Fi, dsb.):
        // Tampilkan animasi status standar XiaoZhi
        current_visual_ = kVisualNone;
        CircularStrip::OnStateChanged();
    }

    void CheckChargingLedUpdate() {
        auto& app = Application::GetInstance();
        auto device_state = app.GetDeviceState();
        if (device_state == kDeviceStateIdle) {
            UpdateChargingLed();
        }
    }

private:
    LCDWikiBatteryMonitor* battery_monitor_ = nullptr;
    enum ChargingVisualState {
        kVisualNone,
        kVisualCharging,
        kVisualFull
    };
    ChargingVisualState current_visual_ = kVisualNone;
    bool use_blue_for_full_ = false;

    void UpdateChargingLed() {
        if (!battery_monitor_) {
            if (current_visual_ != kVisualNone) {
                current_visual_ = kVisualNone;
                SetAllColor({ 0, 0, 0 });
            }
            return;
        }

        bool charging = battery_monitor_->IsCharging();
        int level = battery_monitor_->GetBatteryLevel();
        int mv = battery_monitor_->GetVoltageMv();
        bool is_full = charging && (level >= 100 || mv >= 4190);

        if (charging) {
            if (is_full) {
                if (current_visual_ != kVisualFull) {
                    current_visual_ = kVisualFull;
                    StripColor full_color = use_blue_for_full_ ? StripColor{ 0, 120, 255 } : StripColor{ 0, 255, 0 };
                    SetAllColor(full_color);
                    ESP_LOGI("LCDWikiLed", "🔋 Baterai Penuh (%d%%, %dmV) -> LED %s Solid (Tidak Kelap-kelip)",
                             level, mv, use_blue_for_full_ ? "BIRU" : "HIJAU");
                }
            } else {
                if (current_visual_ != kVisualCharging) {
                    current_visual_ = kVisualCharging;
                    // Terang maksimal: merah kelap-kelip (255, 0, 0) interval 500ms
                    Blink({ 255, 0, 0 }, 500);
                    ESP_LOGI("LCDWikiLed", "⚡ Baterai Sedang Diisi (%d%%, %dmV) -> LED MERAH Terang Kelap-kelip",
                             level, mv);
                }
            }
        } else {
            // Discharging / Tidak sedang diisi daya
            if (current_visual_ != kVisualNone) {
                current_visual_ = kVisualNone;
                SetAllColor({ 0, 0, 0 });
                ESP_LOGI("LCDWikiLed", "🔌 Charger dilepas / Operasi baterai normal -> LED Padam");
            }
        }
    }
};

class LCDWikiES3C28P_Board : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    int rotation_ = 0;
    i2c_master_bus_handle_t codec_i2c_bus_;
    TouchDriver touch_;
    LCDWikiBatteryMonitor* battery_monitor_ = nullptr;
    LCDWikiLed* led_ = nullptr;
    esp_timer_handle_t charging_led_timer_ = nullptr;
    esp_timer_handle_t double_tap_timer_ = nullptr;
    ChronchiDisplay* chronchi_display_ = nullptr;
    bool chronchi_mode_ = false;
    NovaDisplay* nova_display_ = nullptr;
    bool nova_mode_ = false;
    int boot_mode_ = 0;
    SdCardManager sd_card_manager_;

#if CONFIG_BT_ENABLED
    ChronchiBle* chronchi_ble_ = nullptr;
    TaskHandle_t ble_task_handle_ = nullptr;
    bool ble_connected_flag_ = false;
    bool ble_notif_flag_ = false;
    bool ble_nav_flag_ = false;
    bool ble_weather_flag_ = false;
    bool ble_status_flag_ = false;
    bool ble_music_flag_ = false;
    ChronchiNotification last_notif_;
    ChronchiNavigation last_nav_;
    int64_t last_nav_timestamp_us_ = 0;
    ChronchiWeather last_weather_;
    ChronchiPhoneStatus last_phone_status_;
    ChronchiMusic last_music_;
#endif

    TaskHandle_t haptic_task_handle_ = nullptr;
    bool haptic_enabled_ = true;
    QueueHandle_t sound_fx_queue_ = nullptr;
    temperature_sensor_handle_t temp_sensor_ = nullptr;

    TaskHandle_t wav_task_handle_ = nullptr;
    std::atomic<bool> wav_playing_{false};
    std::atomic<bool> wav_stop_requested_{false};
    std::atomic<bool> wav_repeat_mode_{false};
    std::string current_wav_path_ = "";

    virtual void StopWav() override {
        if (wav_playing_) {
            wav_stop_requested_ = true;
            int wait = 0;
            while (wav_playing_ && wait < 20) {
                vTaskDelay(pdMS_TO_TICKS(20));
                wait++;
            }
        }
    }

    virtual bool PlayWav(const std::string& path) override {
        StopWav();
        current_wav_path_ = path;
        wav_stop_requested_ = false;

        xTaskCreatePinnedToCore([](void* arg) {
            auto* self = static_cast<LCDWikiES3C28P_Board*>(arg);
            self->WavPlayerTask();
            vTaskDelete(NULL);
        }, "wav_player", 4096, this, 4, &wav_task_handle_, 1);
        return true;
    }

    virtual bool IsWavPlaying() override {
        return wav_playing_.load();
    }

    virtual void SetWavRepeat(bool repeat) override {
        wav_repeat_mode_.store(repeat);
    }

    virtual bool GetWavRepeat() override {
        return wav_repeat_mode_.load();
    }

    virtual bool IsSdCardMounted() override {
        return sd_card_manager_.GetStatus().mounted;
    }

    void WavPlayerTask() {
        FILE* f = fopen(current_wav_path_.c_str(), "rb");
        if (!f) {
            ESP_LOGW(TAG, "Gagal membuka berkas WAV: %s", current_wav_path_.c_str());
            return;
        }

        uint8_t header[44];
        if (fread(header, 1, 44, f) != 44) {
            fclose(f);
            return;
        }

        if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
            ESP_LOGW(TAG, "Bukan format RIFF/WAVE: %s", current_wav_path_.c_str());
            fclose(f);
            return;
        }

        uint16_t num_channels = *reinterpret_cast<uint16_t*>(&header[22]);
        uint32_t sample_rate = *reinterpret_cast<uint32_t*>(&header[24]);
        uint16_t bits_per_sample = *reinterpret_cast<uint16_t*>(&header[34]);

        ESP_LOGI(TAG, "Memutar audio WAV: %s (ch=%u, rate=%lu, bits=%u)",
                 current_wav_path_.c_str(), num_channels, (unsigned long)sample_rate, bits_per_sample);

        auto* codec = GetAudioCodec();
        if (!codec) {
            fclose(f);
            return;
        }

        codec->EnableOutput(true);
        wav_playing_ = true;

        auto display = GetDisplay();
        if (display) {
            display->StartMusicVisualizer();
        }

        constexpr size_t kChunkSamples = 512;
        std::vector<int16_t> pcm_buf(kChunkSamples);

        while (!wav_stop_requested_) {
            size_t read_samples = fread(pcm_buf.data(), sizeof(int16_t), kChunkSamples, f);
            if (read_samples == 0) {
                if (wav_repeat_mode_.load()) {
                    ESP_LOGI(TAG, "WAV selesai, mengulang kembali dari awal (Repeat Mode)");
                    fseek(f, 44, SEEK_SET);
                    continue;
                }
                break;
            }
            if (read_samples < kChunkSamples) {
                pcm_buf.resize(read_samples);
            }
            codec->OutputData(pcm_buf);
        }

        fclose(f);
        wav_playing_ = false;
        if (display) {
            display->StopMusicVisualizer();
        }
        ESP_LOGI(TAG, "Audio playback selesai: %s", current_wav_path_.c_str());
    }

    std::string ReadTextFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return "Gagal membaca isi berkas teks.";
        char buf[1024];
        size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[read_bytes] = '\0';
        return std::string(buf);
    }

    void SetTouchHaptic(bool enable) {
        haptic_enabled_ = enable;
        Settings settings("touch", true);
        settings.SetInt("haptic", enable ? 1 : 0);
        ESP_LOGI(TAG, "Touch haptic click %s", enable ? "enabled" : "disabled");
    }

    bool IsTouchHapticEnabled() const {
        return haptic_enabled_;
    }

    void TriggerHapticClick() {
        if (haptic_enabled_ && sound_fx_queue_ != nullptr) {
            int fx = 0;
            xQueueSend(sound_fx_queue_, &fx, 0);
        }
    }

    void TriggerGameSound(int fx_id) {
        if (sound_fx_queue_ != nullptr) {
            xQueueSend(sound_fx_queue_, &fx_id, 0);
        }
    }

    static void GenerateGameSound(int fx_id, std::vector<int16_t>& out_pcm) {
        constexpr float kSampleRate = 24000.0f;
        constexpr float kMaxAmp = 28000.0f;

        switch (fx_id) {
            case 1: { // SOUND_PADDLE_HIT (Arkanoid paddle bounce: 400Hz -> 900Hz chirp, 35ms)
                int num_samples = (int)(kSampleRate * 0.035f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 400.0f + 500.0f * frac;
                    float env = sinf(frac * (float)M_PI);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * env * sinf(phase));
                }
                break;
            }
            case 2: { // SOUND_BRICK_HIT (Brick destroyed: crisp bright ping, 1400Hz -> 1100Hz, 45ms)
                int num_samples = (int)(kSampleRate * 0.045f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 1400.0f - 300.0f * frac;
                    float env = expf(-frac * 4.5f);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * env * sinf(phase));
                }
                break;
            }
            case 3: { // SOUND_WALL_HIT (Wall bounce: 650Hz short pop, 25ms)
                int num_samples = (int)(kSampleRate * 0.025f);
                out_pcm.resize(num_samples);
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float env = sinf(frac * (float)M_PI);
                    float val = sinf(2.0f * (float)M_PI * 650.0f * ((float)i / kSampleRate));
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.8f * env * val);
                }
                break;
            }
            case 4: { // SOUND_LOSE_BALL (Ball lost / life lost: descending wobble 600Hz -> 180Hz, 220ms)
                int num_samples = (int)(kSampleRate * 0.22f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 600.0f - 420.0f * frac;
                    float env = (1.0f - frac);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.9f * env * sinf(phase));
                }
                break;
            }
            case 5: { // SOUND_WIN (Triumphant 4-note victory arpeggio: C5, E5, G5, C6)
                float notes[] = { 523.25f, 659.25f, 783.99f, 1046.50f };
                int note_dur = (int)(kSampleRate * 0.07f);
                int total = note_dur * 4;
                out_pcm.resize(total);
                for (int n = 0; n < 4; ++n) {
                    float freq = notes[n];
                    float phase = 0.0f;
                    for (int i = 0; i < note_dur; ++i) {
                        float frac = (float)i / (float)note_dur;
                        float env = sinf(frac * (float)M_PI);
                        phase += 2.0f * (float)M_PI * freq / kSampleRate;
                        out_pcm[n * note_dur + i] = (int16_t)(kMaxAmp * env * sinf(phase));
                    }
                }
                break;
            }
            case 6: { // SOUND_MINE_CLICK (Minesweeper uncover tile: 1200Hz to 800Hz, 18ms)
                int num_samples = (int)(kSampleRate * 0.018f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 1200.0f - 400.0f * frac;
                    float env = expf(-frac * 5.0f);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.7f * env * sinf(phase));
                }
                break;
            }
            case 7: { // SOUND_MINE_FLAG (Double chirp: 1800Hz 15ms -> pause 10ms -> 2200Hz 20ms)
                int dur1 = (int)(kSampleRate * 0.015f);
                int pause = (int)(kSampleRate * 0.010f);
                int dur2 = (int)(kSampleRate * 0.020f);
                int total = dur1 + pause + dur2;
                out_pcm.assign(total, 0);
                for (int i = 0; i < dur1; ++i) {
                    float env = sinf(((float)i / (float)dur1) * (float)M_PI);
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.8f * env * sinf(2.0f * (float)M_PI * 1800.0f * ((float)i / kSampleRate)));
                }
                for (int i = 0; i < dur2; ++i) {
                    float env = sinf(((float)i / (float)dur2) * (float)M_PI);
                    out_pcm[dur1 + pause + i] = (int16_t)(kMaxAmp * 0.8f * env * sinf(2.0f * (float)M_PI * 2200.0f * ((float)i / kSampleRate)));
                }
                break;
            }
            case 8: { // SOUND_MINE_EXPLODE (Retro explosion: noise burst + low rumble, 300ms)
                int num_samples = (int)(kSampleRate * 0.30f);
                out_pcm.resize(num_samples);
                uint32_t lfsr = 0xACE1u;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float env = expf(-frac * 4.0f);
                    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
                    float noise = (float)(int16_t)lfsr / 32768.0f;
                    float low_rumble = sinf(2.0f * (float)M_PI * 70.0f * ((float)i / kSampleRate));
                    float mix = 0.7f * noise + 0.3f * low_rumble;
                    out_pcm[i] = (int16_t)(kMaxAmp * env * mix);
                }
                break;
            }
            case 9: { // SOUND_CAR_STEER (Lane switch chirp: 850Hz -> 1250Hz slide, 35ms)
                int num_samples = (int)(kSampleRate * 0.035f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 850.0f + 400.0f * frac;
                    float env = sinf(frac * (float)M_PI);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.75f * env * sinf(phase));
                }
                break;
            }
            case 10: { // SOUND_CAR_SCORE (Pass car / score point: 880Hz 35ms then 1760Hz 45ms)
                int n1 = (int)(kSampleRate * 0.035f);
                int n2 = (int)(kSampleRate * 0.045f);
                out_pcm.resize(n1 + n2);
                for (int i = 0; i < n1; ++i) {
                    float env = sinf(((float)i / (float)n1) * (float)M_PI);
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.8f * env * sinf(2.0f * (float)M_PI * 880.0f * ((float)i / kSampleRate)));
                }
                for (int i = 0; i < n2; ++i) {
                    float env = sinf(((float)i / (float)n2) * (float)M_PI);
                    out_pcm[n1 + i] = (int16_t)(kMaxAmp * 0.8f * env * sinf(2.0f * (float)M_PI * 1760.0f * ((float)i / kSampleRate)));
                }
                break;
            }
            case 11: { // SOUND_CAR_CRASH (High-speed car crash: crunch + rumble, 350ms)
                int num_samples = (int)(kSampleRate * 0.35f);
                out_pcm.resize(num_samples);
                uint32_t lfsr = 0x55AAu;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float env = (1.0f - frac);
                    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0x002Du);
                    float noise = (float)(int16_t)lfsr / 32768.0f;
                    float rumble = sinf(2.0f * (float)M_PI * (120.0f - 80.0f * frac) * ((float)i / kSampleRate));
                    float mix = 0.75f * noise + 0.25f * rumble;
                    out_pcm[i] = (int16_t)(kMaxAmp * env * mix);
                }
                break;
            }
            case 201: { // NOTIF_CHIME (Uplifting melodic chime: E6 -> B6)
                int n1 = (int)(kSampleRate * 0.08f);
                int n2 = (int)(kSampleRate * 0.12f);
                out_pcm.resize(n1 + n2);
                for (int i = 0; i < n1; ++i) {
                    float frac = (float)i / (float)n1;
                    float env = expf(-frac * 3.5f);
                    float s1 = sinf(2.0f * (float)M_PI * 1318.5f * ((float)i / kSampleRate));
                    float s2 = 0.35f * sinf(2.0f * (float)M_PI * 2637.0f * ((float)i / kSampleRate));
                    out_pcm[i] = (int16_t)(kMaxAmp * env * (s1 + s2) * 0.75f);
                }
                for (int i = 0; i < n2; ++i) {
                    float frac = (float)i / (float)n2;
                    float env = expf(-frac * 3.0f);
                    float s1 = sinf(2.0f * (float)M_PI * 1975.5f * ((float)i / kSampleRate));
                    float s2 = 0.35f * sinf(2.0f * (float)M_PI * 3951.0f * ((float)i / kSampleRate));
                    out_pcm[n1 + i] = (int16_t)(kMaxAmp * env * (s1 + s2) * 0.75f);
                }
                break;
            }
            case 202: { // NOTIF_DINGDONG (Doorbell: C6 1046Hz -> G5 784Hz)
                int n1 = (int)(kSampleRate * 0.09f);
                int n2 = (int)(kSampleRate * 0.15f);
                out_pcm.resize(n1 + n2);
                for (int i = 0; i < n1; ++i) {
                    float frac = (float)i / (float)n1;
                    float env = expf(-frac * 3.0f);
                    float s = sinf(2.0f * (float)M_PI * 1046.5f * ((float)i / kSampleRate));
                    out_pcm[i] = (int16_t)(kMaxAmp * env * s * 0.85f);
                }
                for (int i = 0; i < n2; ++i) {
                    float frac = (float)i / (float)n2;
                    float env = expf(-frac * 2.5f);
                    float s = sinf(2.0f * (float)M_PI * 784.0f * ((float)i / kSampleRate));
                    out_pcm[n1 + i] = (int16_t)(kMaxAmp * env * s * 0.85f);
                }
                break;
            }
            case 203: { // NOTIF_WHISTLE (Chirpy smooth whistle: 1200Hz -> 2400Hz -> 1800Hz)
                int num_samples = (int)(kSampleRate * 0.15f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 1200.0f + 1200.0f * sinf(frac * (float)M_PI);
                    float env = sinf(frac * (float)M_PI);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.85f * env * sinf(phase));
                }
                break;
            }
            case 204: { // NOTIF_CRYSTAL (Glass harp 3 notes: C6 -> E6 -> G6)
                float notes[] = { 1046.5f, 1318.5f, 1568.0f };
                int dur = (int)(kSampleRate * 0.065f);
                out_pcm.resize(dur * 3);
                for (int n = 0; n < 3; ++n) {
                    float freq = notes[n];
                    for (int i = 0; i < dur; ++i) {
                        float frac = (float)i / (float)dur;
                        float env = expf(-frac * 2.8f);
                        float s = sinf(2.0f * (float)M_PI * freq * ((float)i / kSampleRate));
                        out_pcm[n * dur + i] = (int16_t)(kMaxAmp * 0.8f * env * s);
                    }
                }
                break;
            }
            case 205: { // NOTIF_BUBBLE (Liquid water pop sweep: 350Hz -> 1800Hz)
                int num_samples = (int)(kSampleRate * 0.065f);
                out_pcm.resize(num_samples);
                float phase = 0.0f;
                for (int i = 0; i < num_samples; ++i) {
                    float frac = (float)i / (float)num_samples;
                    float freq = 350.0f + 1450.0f * (frac * frac);
                    float env = expf(-frac * 4.0f) * sinf(frac * (float)M_PI * 0.5f);
                    phase += 2.0f * (float)M_PI * freq / kSampleRate;
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.95f * env * sinf(phase));
                }
                break;
            }
            case 206: { // NOTIF_RADAR (Futuristic dual electronic bleep: 1900Hz / 2400Hz)
                int d1 = (int)(kSampleRate * 0.035f);
                int pause = (int)(kSampleRate * 0.025f);
                int d2 = (int)(kSampleRate * 0.045f);
                out_pcm.assign(d1 + pause + d2, 0);
                for (int i = 0; i < d1; ++i) {
                    float frac = (float)i / (float)d1;
                    float env = sinf(frac * (float)M_PI);
                    out_pcm[i] = (int16_t)(kMaxAmp * 0.85f * env * sinf(2.0f * (float)M_PI * 1900.0f * ((float)i / kSampleRate)));
                }
                for (int i = 0; i < d2; ++i) {
                    float frac = (float)i / (float)d2;
                    float env = sinf(frac * (float)M_PI);
                    out_pcm[d1 + pause + i] = (int16_t)(kMaxAmp * 0.85f * env * sinf(2.0f * (float)M_PI * 2400.0f * ((float)i / kSampleRate)));
                }
                break;
            }
            default: {
                if (fx_id >= 100) {
                    int inst = (fx_id - 100) / 20;
                    int note = (fx_id - 100) % 20;
                    static const float kNoteFreqs[13] = {
                        261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f, // C4..C5
                        277.18f, 311.13f, 369.99f, 415.30f, 466.16f                            // C#4..A#4
                    };
                    float freq = (note >= 0 && note < 13) ? kNoteFreqs[note] : 440.0f;
                    int num_samples = (int)(kSampleRate * 0.22f); // 220ms
                    out_pcm.resize(num_samples);

                    if (inst == 0) { // Grand Piano (Fundamental + 2nd & 3rd harmonics + warm decay)
                        float phase1 = 0.0f, phase2 = 0.0f, phase3 = 0.0f;
                        for (int i = 0; i < num_samples; ++i) {
                            float frac = (float)i / (float)num_samples;
                            float env = expf(-frac * 3.5f);
                            if (i < 40) env *= ((float)i / 40.0f);
                            phase1 += 2.0f * (float)M_PI * freq / kSampleRate;
                            phase2 += 2.0f * (float)M_PI * (freq * 2.0f) / kSampleRate;
                            phase3 += 2.0f * (float)M_PI * (freq * 3.0f) / kSampleRate;
                            float wave = sinf(phase1) + 0.40f * sinf(phase2) + 0.15f * sinf(phase3);
                            out_pcm[i] = (int16_t)(kMaxAmp * 0.75f * env * wave);
                        }
                    } else if (inst == 1) { // 8-Bit Chiptune (Square Wave with NES envelope)
                        float phase = 0.0f;
                        for (int i = 0; i < num_samples; ++i) {
                            float frac = (float)i / (float)num_samples;
                            float env = expf(-frac * 2.5f);
                            if (i < 15) env *= ((float)i / 15.0f);
                            phase += freq / kSampleRate;
                            if (phase >= 1.0f) phase -= 1.0f;
                            float wave = (phase < 0.5f) ? 0.8f : -0.8f;
                            out_pcm[i] = (int16_t)(kMaxAmp * 0.65f * env * wave);
                        }
                    } else { // Organ / Akustik (Sine + soft vibrato)
                        float phase = 0.0f;
                        for (int i = 0; i < num_samples; ++i) {
                            float frac = (float)i / (float)num_samples;
                            float env = 1.0f - frac * 0.6f;
                            if (i < 50) env *= ((float)i / 50.0f);
                            if (i > num_samples - 200) env *= ((float)(num_samples - i) / 200.0f);
                            float vibrato = 2.0f * sinf(2.0f * (float)M_PI * 5.0f * ((float)i / kSampleRate));
                            phase += 2.0f * (float)M_PI * (freq + vibrato) / kSampleRate;
                            float wave = sinf(phase) + 0.35f * sinf(phase * 2.0f);
                            out_pcm[i] = (int16_t)(kMaxAmp * 0.70f * env * wave);
                        }
                    }
                }
                break;
            }
        }
    }

    void HapticClickTask() {
        // SC8002B onboard amplifier has a 1uF bypass capacitor on pin 2 (C41)
        // that requires ~40-50ms to charge smoothly upon enabling (AUDIO_EN / GPIO 1 = LOW).
        // If the amp was idle (PA pin is HIGH / muted), we output 1100 samples (46ms) of silence
        // so the bypass capacitor charges silently before playing the click sound.
        constexpr int kSilenceSamples = 1100;
        std::vector<int16_t> wake_silence(kSilenceSamples, 0);

        // Crisp smartphone-like tactile haptic tick (15ms damped tone at 24kHz)
        constexpr int kWaveformSamples = 360;
        constexpr int kPostSilence = 60;
        constexpr int kTotalSamples = kWaveformSamples + kPostSilence;
        constexpr float kSampleRate = 24000.0f;
        constexpr float kPeakAmp = 30000.0f;
        constexpr float kDecayTau = 45.0f;

        std::vector<int16_t> click_pcm(kTotalSamples, 0);
        for (int n = 0; n < kWaveformSamples; ++n) {
            float t = (float)n / kSampleRate;
            float freq = 1300.0f - (600.0f * ((float)n / (float)kWaveformSamples));
            float env = expf(-(float)n / kDecayTau);
            if (n < 4) {
                env *= ((float)n / 4.0f);
            }
            float val = kPeakAmp * env * sinf(2.0f * (float)M_PI * freq * t);
            if (n >= kWaveformSamples - 20) {
                val *= ((float)(kWaveformSamples - n) / 20.0f);
            }
            int32_t clamped = (int32_t)roundf(val);
            if (clamped > 32767) clamped = 32767;
            if (clamped < -32768) clamped = -32768;
            click_pcm[n] = (int16_t)clamped;
        }

        // Configure PA pin as input/output so we can check if it's currently active (LOW)
        gpio_set_direction(AUDIO_CODEC_PA_PIN, GPIO_MODE_INPUT_OUTPUT);

        int fx_id = 0;
        while (true) {
            if (sound_fx_queue_ == nullptr || xQueueReceive(sound_fx_queue_, &fx_id, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            if (fx_id == 0 && !haptic_enabled_) {
                continue;
            }
            auto codec = GetAudioCodec();
            if (!codec) {
                continue;
            }
            if (codec->output_volume() <= 0) {
                continue;
            }
            if (!codec->output_enabled()) {
                codec->EnableOutput(true);
            }

            // PA is active LOW on SC8002B.
            // If pin is HIGH (1), the amp was idle/in shutdown. Wake it up and charge bypass cap.
            bool pa_was_sleeping = (gpio_get_level(AUDIO_CODEC_PA_PIN) != 0);
            if (pa_was_sleeping) {
                gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
                codec->OutputData(wake_silence);
            }

            if (fx_id == 0) {
                codec->OutputData(click_pcm);
            } else {
                std::vector<int16_t> fx_pcm;
                GenerateGameSound(fx_id, fx_pcm);
                if (!fx_pcm.empty()) {
                    codec->OutputData(fx_pcm);
                }
            }
        }
    }

    void InitializeTemperatureSensor() {
        temperature_sensor_config_t temp_config = {
            .range_min = 10,
            .range_max = 80,
            .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT
        };
        if (temperature_sensor_install(&temp_config, &temp_sensor_) == ESP_OK) {
            temperature_sensor_enable(temp_sensor_);
            ESP_LOGI(TAG, "ESP32-S3 on-die temperature sensor initialized");
        }
    }

    float GetChipTemperature() {
        float temp_val = 0.0f;
        if (temp_sensor_) {
            temperature_sensor_get_celsius(temp_sensor_, &temp_val);
        }
        return temp_val;
    }

    void InitializeBatteryMonitor() {
        battery_monitor_ = new LCDWikiBatteryMonitor(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 200000.0f, 200000.0f);
    }

    void InitializeLed() {
        led_ = new LCDWikiLed(BUILTIN_LED_GPIO, 1);
        led_->SetBatteryMonitor(battery_monitor_);

        esp_timer_create_args_t chg_timer_args = {
            .callback = [](void* arg) {
                auto* board = static_cast<LCDWikiES3C28P_Board*>(arg);
                board->CheckChargingLed();
            },
            .arg = this,
            .name = "chg_led_tmr"
        };
        esp_timer_create(&chg_timer_args, &charging_led_timer_);
        esp_timer_start_periodic(charging_led_timer_, 1000000ULL); // Periksa status charging setiap 1.0 detik
    }

    void CheckChargingLed() {
        if (led_) {
            led_->CheckChargingLedUpdate();
        }
    }

    void StartDoubleTapTimer(uint32_t delay_ms = 260) {
        if (!double_tap_timer_) return;
        esp_timer_stop(double_tap_timer_);
        esp_timer_start_once(double_tap_timer_, delay_ms * 1000ULL);
    }

    void CancelDoubleTapTimer() {
        if (!double_tap_timer_) return;
        esp_timer_stop(double_tap_timer_);
    }

    void OnDoubleTapTriggered() {
        ESP_LOGI(TAG, "Touch Double-Tap confirmed -> Toggle Listening / Standby");
        Application::GetInstance().Schedule([]() {
            auto &app = Application::GetInstance();
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                YouTubeAudioPoller::GetInstance().Abort();
                return;
            }
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.AbortSpeaking(kAbortReasonNone);
                return;
            }
            app.ToggleChatState();
        });
    }

    virtual void RotateDisplay() override {
        if (!panel_) return;
        rotation_ = (rotation_ + 90) % 360;

        ApplyDisplayRotation();

        Settings settings("display", true);
        settings.SetInt("rotation", rotation_);
        if (!nova_mode_ && !chronchi_mode_ && display_) {
            display_->ShowNotification(std::string("Rotasi Layar ") + std::to_string(rotation_) + "°");
        }
        ESP_LOGI(TAG, "Rotated display to %d deg", rotation_);
    }

    void ApplyDisplayRotation() {
        if (!panel_) return;

        lv_display_rotation_t lv_rot = LV_DISPLAY_ROTATION_0;
        if (rotation_ == 90) lv_rot = LV_DISPLAY_ROTATION_90;
        else if (rotation_ == 180) lv_rot = LV_DISPLAY_ROTATION_180;
        else if (rotation_ == 270) lv_rot = LV_DISPLAY_ROTATION_270;

        lvgl_port_lock(0);
        lv_display_t* disp = lv_display_get_default();
        if (disp) {
            lv_display_set_rotation(disp, lv_rot);
        }

        if (nova_mode_ && nova_display_) {
            nova_display_->NavigateRoot(nova_display_->GetCurrentPage());
        } else if (chronchi_mode_ && chronchi_display_) {
            chronchi_display_->updateResolution();
        } else {
            if (display_) {
                display_->UpdateResolution();
            }
            lv_obj_invalidate(lv_screen_active());
        }
        lvgl_port_unlock();
    }

    void AdjustVolume(int delta) {
        auto codec = GetAudioCodec();
        if (codec) {
            int vol = std::clamp(codec->output_volume() + delta, 0, 100);
            codec->SetOutputVolume(vol);
            if (display_) {
                display_->ShowNotification(std::string("Vol: ") + std::to_string(vol) + "%");
            }
            ESP_LOGI(TAG, "AdjustVolume -> %d%%", vol);
        }
    }

    void SetVolume(int vol) {
        auto codec = GetAudioCodec();
        if (codec) {
            codec->SetOutputVolume(vol);
            if (display_) {
                display_->ShowNotification(vol == 0 ? "Muted" : (std::string("Vol: ") + std::to_string(vol) + "%"));
            }
            ESP_LOGI(TAG, "SetVolume -> %d%%", vol);
        }
    }

    virtual bool IsChronchiMode() override {
        if (chronchi_mode_ || nova_mode_) {
            return true;
        }
        return (boot_mode_ == 1 || boot_mode_ == 2);
    }

    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_setservername(0, "id.pool.ntp.org");
            esp_sntp_setservername(1, "time.google.com");
            esp_sntp_setservername(2, "time.cloudflare.com");
            esp_sntp_setservername(3, "pool.ntp.org");
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP client initialized after network start (IMMED)");
        }
    }

    virtual void OnNetworkEvent(NetworkEvent event, const std::string& data = "") override {
        WifiBoard::OnNetworkEvent(event, data);
        if (event == NetworkEvent::Connected) {
            // Ensure maximum Wi-Fi TX Power (20 dBm = 80 in 0.25 dBm units)
            esp_wifi_set_max_tx_power(80);
            // Inisialisasi Zona Waktu Sistem (Default: WIB UTC+7 / Indonesia Bagian Barat)
            Settings sys_settings("system", true);
            int tz_idx = sys_settings.GetInt("tz_idx", 0);
            static const char* kTimezonesEnv[] = {
                "WIB-7", "WITA-8", "WIT-9", "GMT0", "JST-9", "EST5EDT,M3.2.0,M11.1.0"
            };
            const char* tz_str = kTimezonesEnv[(tz_idx >= 0 && tz_idx < 6) ? tz_idx : 0];
            setenv("TZ", tz_str, 1);
            tzset();

            // Refresh / restart SNTP immediately on Wi-Fi connection
            if (esp_sntp_enabled()) {
                esp_sntp_stop();
            }
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_setservername(0, "id.pool.ntp.org");
            esp_sntp_setservername(1, "time.google.com");
            esp_sntp_setservername(2, "time.cloudflare.com");
            esp_sntp_setservername(3, "pool.ntp.org");
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP re-initialized on Wi-Fi Connected (%s, TZ: %s, IMMED)", data.c_str(), tz_str);
        }
    }

    virtual bool GetBleConnected() override {
        return ble_connected_flag_;
    }

    void InitializeBle() {
#if CONFIG_BT_ENABLED
        if (chronchi_ble_ != nullptr) return;
        ESP_LOGI(TAG, "Initializing Chronchi Bluetooth BLE (Chronos Companion)...");
        chronchi_ble_ = new ChronchiBle();
        chronchi_ble_->begin("Chronchi");

        if (!ble_task_handle_) {
            xTaskCreatePinnedToCore([](void* arg) {
                auto* self = static_cast<LCDWikiES3C28P_Board*>(arg);
                while (true) {
#if CONFIG_BT_ENABLED
                    if (self->chronchi_ble_) {
                        self->chronchi_ble_->loop();
                    }
#endif
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }, "ble_loop", 4096, this, 3, &ble_task_handle_, 0);
        }

        chronchi_ble_->setConnectionCallback([this](bool connected) {
            ESP_LOGI(TAG, "Chronchi BLE: %s", connected ? "Connected" : "Disconnected");
            ble_connected_flag_ = connected;
            if (chronchi_display_) {
                chronchi_display_->updateBtStatus(connected);
            }
        });

        chronchi_ble_->setNotificationCallback([this](const ChronchiNotification& notif) {
            ESP_LOGI(TAG, "Notification received: app='%s', title='%s', msg='%s'",
                     notif.app.c_str(), notif.title.c_str(), notif.message.c_str());
            last_notif_ = notif;
            ble_notif_flag_ = true;

            int64_t now_us = esp_timer_get_time();
            bool nav_recently_updated = (last_nav_timestamp_us_ > 0) && ((now_us - last_nav_timestamp_us_) < 20000000ULL);
            bool is_on_maps_screen = false;
            if (nova_display_ && nova_display_->GetCurrentPage() == NovaDisplay::PAGE_NAV) {
                is_on_maps_screen = true;
            }
            if (chronchi_display_ && chronchi_display_->getScreenState() == ChronchiDisplay::NAVIGATION) {
                is_on_maps_screen = true;
            }

            if (nova_display_) {
                nova_display_->UpdateNotif();
                bool suppress_banner = last_nav_.active && nav_recently_updated && is_on_maps_screen;
                if (!suppress_banner) {
                    nova_display_->ShowNotificationBanner(notif.app, notif.title, notif.message);
                } else {
                    ESP_LOGI(TAG, "Suppressing notification banner during active Google Maps screen");
                }
            }
            if (chronchi_display_) {
                chronchi_display_->showNotification(notif);
            }
            ChronchiAlert::GetInstance().VibrateNotification();
            Settings audio_s("audio", true);
            bool snd_en = audio_s.GetInt("notif_snd_en", 1) != 0;
            int snd_idx = audio_s.GetInt("notif_snd_idx", 0);
            if (snd_en) {
                TriggerGameSound(201 + (snd_idx % 6));
            }
        });

        chronchi_ble_->setNavigationCallback([this](const ChronchiNavigation& nav) {
            ESP_LOGI(TAG, "Navigation: %s - %s (%s, ETA:%s, Dur:%s, Spd:%s, active=%d)",
                     nav.title.c_str(), nav.directions.c_str(), nav.distance.c_str(),
                     nav.eta.c_str(), nav.duration.c_str(), nav.speed.c_str(), (int)nav.active);
            last_nav_ = nav;
            if (nav.active) {
                last_nav_timestamp_us_ = esp_timer_get_time();
            } else {
                last_nav_timestamp_us_ = 0;
            }
            ble_nav_flag_ = true;
            if (nova_display_) {
                nova_display_->UpdateNavData(nav.distance, nav.directions, nav.title, nav.duration, nav.eta, nav.speed, nav.active,
                                             nav.maneuver, nav.hasIcon, nav.icon);
            }
            if (chronchi_display_) {
                chronchi_display_->showNavigation(nav);
            }
        });

        chronchi_ble_->setWeatherCallback([this](const ChronchiWeather& weather) {
            ESP_LOGI(TAG, "Weather: %.0f°C %s (loc=%s, H=%d, L=%d)", weather.temperatureC, weather.condition.c_str(),
                     weather.location.c_str(), weather.highC, weather.lowC);
            last_weather_ = weather;
            ble_weather_flag_ = true;
            if (nova_display_) {
                nova_display_->UpdateWeatherFull(weather.temperatureC, weather.condition, weather.humidity, weather.location, weather.highC, weather.lowC);
            }
            if (chronchi_display_) {
                chronchi_display_->showWeather(weather);
                chronchi_display_->updateHomeWeather(weather);
            }
        });

        chronchi_ble_->setPhoneStatusCallback([this](const ChronchiPhoneStatus& status) {
            ESP_LOGI(TAG, "Phone: %d%% %s", status.batteryLevel, status.network.transport.c_str());
            last_phone_status_ = status;
            ble_status_flag_ = true;
            if (nova_display_) {
                nova_display_->UpdatePhoneStatus(status.batteryLevel, status.charging, status.network.transport);
            }
            if (chronchi_display_) {
                chronchi_display_->showPhoneStatus(status);
            }
        });

        chronchi_ble_->setMusicCallback([this](const ChronchiMusic& music) {
            ESP_LOGI(TAG, "Music: %s - %s", music.artist.c_str(), music.title.c_str());
            last_music_ = music;
            ble_music_flag_ = true;
            if (nova_display_) {
                nova_display_->UpdateBtMusicData(music.title, music.artist, music.state == 1);
            }
            if (chronchi_display_) {
                chronchi_display_->showMusic(music);
            }
        });

        chronchi_ble_->setWatchfaceCallback([this](uint32_t color, uint32_t layout) {
            ESP_LOGI(TAG, "Watchface customized via Chronos: color=0x%06lX layout=%lu",
                     (unsigned long)color, (unsigned long)layout);
            if (chronchi_display_) {
                chronchi_display_->setWatchfaceTheme(color, layout);
            }
        });

        chronchi_ble_->setTimeFormatCallback([this](bool is_24h) {
            if (chronchi_display_) {
                chronchi_display_->set24HourFormat(is_24h);
            }
        });

        chronchi_ble_->setFindWatchCallback([this](bool active) {
            ESP_LOGI(TAG, "Chronos Find Watch alert: %d", active);
            if (nova_display_) {
                nova_display_->ShowFindWatchAlert(active);
            }
            if (chronchi_display_) {
                chronchi_display_->showFindWatchAlert(active);
            }
            if (active) {
                ChronchiAlert::GetInstance().StartFindDeviceAlarm();
            } else {
                ChronchiAlert::GetInstance().StopAlert();
            }
        });

        chronchi_ble_->setCameraStateCallback([this](bool ready) {
            ESP_LOGI(TAG, "Chronos Camera state: %d", ready);
            if (ready && nova_display_) {
                nova_display_->ShowNotificationBanner("Kamera HP", "Kamera Terbuka", "Tekan tombol Shutter di Cari HP untuk memotret");
            }
            if (chronchi_display_) {
                chronchi_display_->showCameraPrompt(ready);
            }
        });

        chronchi_ble_->setRingerCallback([this](const std::string& caller, bool active) {
            ESP_LOGI(TAG, "Chronos Phone Call: '%s' active=%d", caller.c_str(), active);
            if (nova_display_) {
                nova_display_->ShowIncomingCallAlert(caller, active);
            }
            if (active) {
                if (chronchi_display_) {
                    chronchi_display_->showReminderAlert("📞 PANGGILAN MASUK", caller.empty() ? "Panggilan Suara" : caller.c_str());
                }
                ChronchiAlert::GetInstance().StartCallAlert();
            } else {
                if (chronchi_display_ && chronchi_display_->getScreenState() == ChronchiDisplay::ALERT) {
                    chronchi_display_->showHome();
                }
                ChronchiAlert::GetInstance().StopAlert();
            }
        });

        chronchi_ble_->setAlarmCallback([this](uint8_t index, const ChronchiAlarm& alarm) {
            ESP_LOGI(TAG, "Chronos Alarm configured: #%d %02d:%02d en=%d rep=0x%02X",
                     index, alarm.hour, alarm.minute, alarm.enabled, alarm.repeat);
            if (alarm.enabled) {
                char buf[48];
                snprintf(buf, sizeof(buf), "Alarm #%d aktif: %02d:%02d", index + 1, alarm.hour, alarm.minute);
                ChronchiNotification notif;
                notif.app = "Alarm";
                notif.title = "Alarm Disetel";
                notif.message = buf;
                notif.time = "";
                if (nova_display_) {
                    nova_display_->ShowNotificationBanner(notif.app, notif.title, notif.message);
                }
                if (chronchi_display_) {
                    chronchi_display_->showNotification(notif);
                }
                ChronchiAlert::GetInstance().VibrateNotification();
            }
        });

        chronchi_ble_->setReminderCallback([this](const std::string& type, const ChronchiReminder& reminder) {
            ESP_LOGI(TAG, "Chronos Reminder '%s': enabled=%d (%02d:%02d - %02d:%02d, int=%d)",
                     type.c_str(), reminder.enabled, reminder.startHour, reminder.startMinute,
                     reminder.endHour, reminder.endMinute, reminder.intervalMinutes);
            if (reminder.enabled) {
                const char* title = (type == "water") ? "💧 Pengingat Minum" : "🚶 Pengingat Gerak";
                const char* msg = (type == "water") ? "Waktunya minum air putih!" : "Waktunya berdiri & bergerak sejenak!";
                if (nova_display_) {
                    nova_display_->ShowNotificationBanner(title, "Kesehatan", msg);
                }
                if (chronchi_display_) {
                    chronchi_display_->showReminderAlert(title, msg);
                }
                ChronchiAlert::GetInstance().TriggerNotificationAlert();
            }
        });
#endif
    }

    void StartNovaOS() {
        ESP_LOGI(TAG, "Starting NOVA MINI OS on LCDWiki ES3C28P (240x320)...");
        nova_mode_ = true;
        chronchi_mode_ = false;
        vTaskDelay(pdMS_TO_TICKS(100));

        StartNetwork();
        InitializeBle();
        YouTubeAudioPoller::GetInstance().Start();

        NovaCallbacks cb;
        cb.switch_mode = [this](int target_mode) {
            SwitchToMode(target_mode);
        };
        cb.launch_chronchi_screen = [this](int screen) {
            StartChronchiWatch(screen);
        };
        cb.ble_music_control = [this](int cmd) {
            if (chronchi_ble_) {
                ChronosControl cc;
                if (cmd == 1) cc = MUSIC_TOGGLE;
                else if (cmd == 2) cc = MUSIC_PREVIOUS;
                else if (cmd == 3) cc = MUSIC_NEXT;
                else if (cmd == 4) cc = VOLUME_UP;
                else if (cmd == 5) cc = VOLUME_DOWN;
                else cc = static_cast<ChronosControl>(cmd);
                chronchi_ble_->musicControl(cc);
            }
        };
        cb.ble_find_phone = [this](bool ring) {
            if (chronchi_ble_) {
                chronchi_ble_->findPhone(ring);
            }
        };
        cb.ble_capture_photo = [this]() -> bool {
            if (chronchi_ble_) {
                return chronchi_ble_->capturePhoto();
            }
            return false;
        };
        cb.ble_sync_request = [this]() {
            if (chronchi_ble_) {
                chronchi_ble_->syncRequest();
            }
        };
        cb.get_ble_notif_count = [this]() -> int {
            if (chronchi_ble_) {
                return chronchi_ble_->getNotificationCount();
            }
            return (!last_notif_.app.empty() || !last_notif_.title.empty()) ? 1 : 0;
        };
        cb.get_ble_notif_at = [this](int idx, std::string& app, std::string& title, std::string& msg, std::string& time) -> bool {
            if (chronchi_ble_ && idx < chronchi_ble_->getNotificationCount()) {
                auto n = chronchi_ble_->getNotificationAt(idx);
                app = n.app;
                title = n.title;
                msg = n.message;
                time = n.time;
                return true;
            }
            if (idx == 0 && (!last_notif_.app.empty() || !last_notif_.title.empty())) {
                app = last_notif_.app;
                title = last_notif_.title;
                msg = last_notif_.message;
                time = last_notif_.time;
                return true;
            }
            return false;
        };
        cb.clear_ble_notifs = [this]() {
            if (chronchi_ble_) {
                chronchi_ble_->clearNotifications();
            }
            last_notif_ = {};
        };
        cb.get_ble_weather_full = [this](float& temp, std::string& cond, int& hum, std::string& loc, int& high, int& low) -> bool {
            temp = last_weather_.temperatureC;
            cond = last_weather_.condition;
            hum = last_weather_.humidity;
            loc = last_weather_.location;
            high = last_weather_.highC;
            low = last_weather_.lowC;
            return last_weather_.valid || !cond.empty();
        };
        cb.get_ble_music = [this](std::string& title, std::string& artist, bool& playing) {
            title = last_music_.title;
            artist = last_music_.artist;
            playing = (last_music_.state == 1);
            return !title.empty();
        };
        cb.get_ble_nav = [this](std::string& dist, std::string& dir, std::string& title, std::string& dur, std::string& eta, std::string& spd, bool& active) {
            dist = last_nav_.distance;
            dir = last_nav_.directions;
            title = last_nav_.title;
            dur = last_nav_.duration;
            eta = last_nav_.eta;
            spd = last_nav_.speed;
            active = last_nav_.active;
            return active || !dir.empty();
        };
        cb.get_ble_nav_extra = [this](std::string& maneuver, bool& has_icon, const uint8_t*& icon_data) {
            maneuver = last_nav_.maneuver;
            has_icon = last_nav_.hasIcon;
            icon_data = last_nav_.icon;
        };
        cb.get_ble_weather = [this](float& temp, std::string& cond, int& hum) {
            temp = last_weather_.temperatureC;
            cond = last_weather_.condition;
            hum = last_weather_.humidity;
            return last_weather_.valid || !cond.empty();
        };
        cb.get_ble_phone = [this](int& bat, bool& chg, std::string& net) {
            bat = last_phone_status_.batteryLevel;
            chg = last_phone_status_.charging;
            net = last_phone_status_.network.transport;
            return bat > 0;
        };
        cb.get_ble_notif = [this](std::string& app, std::string& title, std::string& msg, std::string& time) {
            app = last_notif_.app;
            title = last_notif_.title;
            msg = last_notif_.message;
            time = last_notif_.time;
            return !app.empty() || !title.empty();
        };
        cb.set_brightness = [this](uint8_t b) {
            uint8_t pct = (b * 100) / 255;
            GetBacklight()->SetBrightness(pct, true);
        };
        cb.set_volume = [this](int vol) {
            if (GetAudioCodec()) GetAudioCodec()->SetOutputVolume(vol);
        };
        cb.get_volume = [this]() {
            return GetAudioCodec() ? GetAudioCodec()->output_volume() : 75;
        };
        cb.set_haptic = [this](bool en) {
            SetTouchHaptic(en);
        };
        cb.get_haptic = [this]() {
            return IsTouchHapticEnabled();
        };
        cb.get_battery = [this](int& lvl, bool& chg, int& mv, int& raw) {
            bool dchg = false;
            bool ok = GetBatteryLevel(lvl, chg, dchg);
            mv = battery_monitor_ ? battery_monitor_->GetVoltageMv() : 0;
            raw = battery_monitor_ ? battery_monitor_->GetRawAdc() : 0;
            return ok;
        };
        cb.get_ble_status = [this](bool& connected, std::string& name, std::string& mac) {
#if CONFIG_BT_ENABLED
            connected = (chronchi_ble_ != nullptr) && chronchi_ble_->isConnected();
            name = "Chronchi";
            uint8_t mac_bytes[6] = {0};
            esp_read_mac(mac_bytes, ESP_MAC_BT);
            char mac_str[24];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);
            mac = mac_str;
            return true;
#else
            connected = false;
            name = "Tidak Aktif";
            mac = "00:00:00:00:00:00";
            return false;
#endif
        };
        cb.get_sd_status = [this]() {
            return sd_card_manager_.GetStatus();
        };
        cb.remount_sd = [this]() {
            sd_card_manager_.Unmount();
            return sd_card_manager_.Mount();
        };
        cb.format_sd = [this]() {
            return sd_card_manager_.Format();
        };
        cb.read_text_file = [this](const std::string& path) {
            return ReadTextFile(path);
        };
        cb.play_audio = [this](const std::string& path) {
            return PlayWav(path);
        };
        cb.stop_audio = [this]() {
            StopWav();
        };
        cb.is_audio_playing = [this]() {
            return wav_playing_.load();
        };
        cb.set_repeat = [this](bool repeat) {
            SetWavRepeat(repeat);
        };
        cb.get_repeat = [this]() {
            return GetWavRepeat();
        };
        cb.get_online_playlist = []() -> std::vector<OnlineSongItem> {
            return {};
        };
        cb.refresh_online_playlist = [](std::function<void()> on_done) {
            if (on_done) on_done();
        };
        cb.is_fetching_online_playlist = []() -> bool {
            return false;
        };
        cb.play_online = [](const std::string& vid, const std::string& title, const std::string& duration) {
            YouTubeAudioPoller::GetInstance().Start();
            if (!vid.empty()) {
                YouTubeAudioPoller::GetInstance().PlaySong(vid, title, duration);
            } else {
                YouTubeAudioPoller::GetInstance().PlayQuery(title);
            }
        };
        cb.stop_online = []() {
            YouTubeAudioPoller::GetInstance().Abort();
        };
        cb.is_online_playing = []() {
            return YouTubeAudioPoller::GetInstance().IsPlaying();
        };
        cb.download_online = [](const std::string& q, const std::string& t) {
            return YouTubeAudioPoller::GetInstance().DownloadSong(q, t);
        };
        cb.trigger_click = [this]() {
            TriggerHapticClick();
        };
        cb.play_sound = [this](int sound_id) {
            TriggerGameSound(sound_id);
        };
        cb.get_temperature = [this]() -> float {
            return GetChipTemperature();
        };

        // Otomatis deteksi & mount MicroSD saat Nova OS aktif
        sd_card_manager_.Mount();

        lvgl_port_lock(0);
        if (chronchi_display_) {
            delete chronchi_display_;
            chronchi_display_ = nullptr;
        }
        if (nova_display_) {
            delete nova_display_;
            nova_display_ = nullptr;
        }
        lv_obj_t* scr_nova = lv_obj_create(NULL);
        lv_screen_load(scr_nova);

        nova_display_ = new NovaDisplay(cb);
        int cur_w = (rotation_ == 90 || rotation_ == 270) ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
        int cur_h = (rotation_ == 90 || rotation_ == 270) ? DISPLAY_WIDTH : DISPLAY_HEIGHT;
        nova_display_->Init(cur_w, cur_h);
        lvgl_port_unlock();

        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    void NovaOSLoop() {
#if CONFIG_BT_ENABLED
        if (chronchi_ble_) {
            chronchi_ble_->loop();
        }
#endif
        static uint32_t last_ui_update = 0;
        static uint32_t last_bat_sync = 0;
        uint32_t now = esp_timer_get_time() / 1000;

        if (now - last_ui_update >= 500) {
            last_ui_update = now;
            lvgl_port_lock(0);
            if (nova_display_) {
                nova_display_->Update();
            }
            lvgl_port_unlock();
        }

        if (now - last_bat_sync >= 5000) {
            last_bat_sync = now;
            int level = 100;
            bool chg = false, dchg = false;
            if (GetBatteryLevel(level, chg, dchg)) {
#if CONFIG_BT_ENABLED
                if (chronchi_ble_) {
                    chronchi_ble_->setBattery(level, chg);
                }
#endif
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    void StartChronchiWatch(int initial_screen = 0) {
        ESP_LOGI(TAG, "Starting Chronchi mode on LCDWiki ES3C28P (initial_screen=%d)...", initial_screen);
        chronchi_mode_ = true;
        nova_mode_ = false;
        vTaskDelay(pdMS_TO_TICKS(100));

        StartNetwork();
        InitializeBle();

        ChronchiAlert::GetInstance().Initialize(GPIO_NUM_NC, GPIO_NUM_NC);

        lvgl_port_lock(0);
        if (nova_display_) {
            delete nova_display_;
            nova_display_ = nullptr;
        }
        if (chronchi_display_) {
            delete chronchi_display_;
            chronchi_display_ = nullptr;
        }
        chronchi_display_ = new ChronchiDisplay();
        int cur_w = (rotation_ == 90 || rotation_ == 270) ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
        int cur_h = (rotation_ == 90 || rotation_ == 270) ? DISPLAY_WIDTH : DISPLAY_HEIGHT;
        chronchi_display_->init(cur_w, cur_h);
        if (initial_screen > 0) {
            chronchi_display_->setScreenState(static_cast<ChronchiDisplay::ScreenState>(initial_screen));
        } else {
            chronchi_display_->showHome();
        }
        lvgl_port_unlock();

        // Update initial battery status immediately
        int level = 100;
        bool chg = false, dchg = false;
        if (GetBatteryLevel(level, chg, dchg)) {
            chronchi_display_->updateBattery(level, chg);
        }

        // Read Bluetooth MAC address
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        char mac_str[24];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        chronchi_display_->setBleInfo("Chronchi", mac_str, "https://chronos.ke/");

#if CONFIG_BT_ENABLED
        chronchi_display_->setMusicControlCallback([this](int cmd) {
            if (chronchi_ble_) {
                chronchi_ble_->musicControl(static_cast<ChronosControl>(cmd));
            }
        });

        chronchi_display_->setCameraCaptureCallback([this]() {
            if (chronchi_ble_) {
                chronchi_ble_->capturePhoto();
            }
        });

        chronchi_display_->setFindPhoneCallback([this](bool active) {
            if (chronchi_ble_) {
                chronchi_ble_->findPhone(active);
            }
        });

        chronchi_display_->updateBtStatus(chronchi_ble_ ? chronchi_ble_->isConnected() : false);
#endif

        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    void ChronchiWatchLoop() {
#if CONFIG_BT_ENABLED
        if (chronchi_ble_) {
            chronchi_ble_->loop();
        }
#endif

        static uint32_t last_time_update = 0;
        static uint32_t last_battery_update = 0;
        static int last_alarm_min = -1;
        uint32_t now = esp_timer_get_time() / 1000;

        if (now - last_time_update >= 1000) {
            last_time_update = now;
            time_t t = time(NULL);
            struct tm* tm_info = localtime(&t);
            if (chronchi_display_) {
                chronchi_display_->updateTime(tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
            }

#if CONFIG_BT_ENABLED
            // Check alarms set from Chronos companion app
            if (chronchi_ble_ && tm_info->tm_sec == 0 && tm_info->tm_min != last_alarm_min) {
                last_alarm_min = tm_info->tm_min;
                for (uint8_t i = 0; i < 8; i++) {
                    ChronchiAlarm alarm = chronchi_ble_->getAlarm(i);
                    if (alarm.enabled && alarm.hour == tm_info->tm_hour && alarm.minute == tm_info->tm_min) {
                        ESP_LOGI(TAG, "ALARM #%d BERBUNYI: %02d:%02d", i, alarm.hour, alarm.minute);
                        if (chronchi_display_) {
                            chronchi_display_->showAlarmAlert(alarm.hour, alarm.minute);
                        }
                        ChronchiAlert::GetInstance().StartFindDeviceAlarm();
                        break;
                    }
                }
            }
#endif
        }

        if (now - last_battery_update >= 5000) {
            last_battery_update = now;
            int level = 100;
            bool chg = false, dchg = false;
            if (GetBatteryLevel(level, chg, dchg) && chronchi_display_) {
                chronchi_display_->updateBattery(level, chg);
#if CONFIG_BT_ENABLED
                if (chronchi_ble_) {
                    chronchi_ble_->setBattery(level, chg);
                }
#endif
            }

            esp_netif_ip_info_t ip_info;
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            bool wifi_connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);
            if (chronchi_display_) {
                chronchi_display_->updateWifi(wifi_connected);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    virtual void StartChronchi() override {
        if (boot_mode_ == 2) {
            StartNovaOS();
            return;
        }
        StartChronchiWatch();
    }

    virtual void ChronchiLoop() override {
        if (chronchi_mode_) {
            ChronchiWatchLoop();
        } else if (nova_mode_) {
            NovaOSLoop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    virtual void SwitchToMode(int target_mode) override {
        boot_mode_ = target_mode;
        const char* name = (target_mode == 1) ? "Chronchi" : (target_mode == 2) ? "Nova OS" : "XiaoZhi";
        ESP_LOGI(TAG, "Switching mode to %s (mode=%d)...", name, target_mode);
        Settings settings("chronchi", true);
        settings.SetInt("mode", target_mode);

        if (battery_monitor_) {
            battery_monitor_->SaveStateNow();
        }

        bool was_in_chronchi_or_nova = (chronchi_mode_ || nova_mode_);

        // Mode 1 (Chronchi Watch) <-> Mode 2 (Nova OS):
        // 100% In-Memory Seamless Switching without reboot!
        // Bluetooth BLE and Wi-Fi stay active and connected continuously.
        // HANYA jika sebelumnya sudah aktif di mode Chronchi atau Nova OS (bukan dari mode XiaoZhi)!
        if (was_in_chronchi_or_nova && (target_mode == 1 || target_mode == 2)) {
            if (target_mode == 1) {
                StartChronchiWatch(0);
            } else {
                StartNovaOS();
            }
            return;
        }

        // Switching between XiaoZhi (Mode 0) and Nova/Chronchi (Mode 1/2):
        // Clean hardware reboot guarantees 100% dedicated audio codec & network resources.
        ESP_LOGI(TAG, "Rebooting into %s...", name);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }

    void ToggleMode() {
        Settings settings("chronchi", true);
        int cur = settings.GetInt("mode", 0);
        int target = (cur == 1) ? 0 : 1;
        SwitchToMode(target);
    }

    static void TouchpadRead(lv_indev_t* indev, lv_indev_data_t* data) {
        auto* self = static_cast<LCDWikiES3C28P_Board*>(lv_indev_get_user_data(indev));
        if (!self) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        static int16_t last_raw_x = 0, last_raw_y = 0;
        static int16_t last_scr_x = 0, last_scr_y = 0;
        static uint32_t press_start = 0;
        static uint16_t start_scr_x = 0, start_scr_y = 0;
        static bool was_pressed = false;

        bool touched = false;
        uint16_t rx = 0, ry = 0;
        bool ok = self->touch_.Read(touched, rx, ry);
        if (!ok) {
            // Keep last valid state on transient I2C fail to avoid premature release
            data->point.x = last_raw_x;
            data->point.y = last_raw_y;
            data->state = was_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            return;
        }

        uint32_t now = esp_timer_get_time() / 1000;

        if (touched) {
            int16_t px = std::clamp((int)rx, 0, DISPLAY_WIDTH - 1);
            int16_t py = std::clamp((int)ry, 0, DISPLAY_HEIGHT - 1);

            last_raw_x = px;
            last_raw_y = py;

            // Berikan koordinat mentah (raw unrotated) ke LVGL indev.
            // LVGL secara otomatis memutar koordinat melalui lv_display_rotate_point()
            // sesuai rotasi aktif display (disp->rotation).
            data->point.x = px;
            data->point.y = py;
            data->state = LV_INDEV_STATE_PRESSED;

            // Hitung koordinat rotasi layar aktif untuk pengenalan gestur internal & logging
            lv_display_t* disp = lv_display_get_default();
            lv_point_t scr_pt = { px, py };
            if (disp) {
                lv_display_rotate_point(disp, &scr_pt);
            }
            int16_t scr_x = scr_pt.x;
            int16_t scr_y = scr_pt.y;

            last_scr_x = scr_x;
            last_scr_y = scr_y;

            if (!was_pressed) {
                was_pressed = true;
                press_start = now;
                start_scr_x = scr_x;
                start_scr_y = scr_y;
                ESP_LOGI(TAG, "Touch PRESSED at scr(%d, %d), raw(%d, %d) [rot=%d]",
                         scr_x, scr_y, px, py, self->rotation_);

                static uint32_t last_haptic_trigger = 0;
                if (now - last_haptic_trigger >= 70) {
                    last_haptic_trigger = now;
                    self->TriggerHapticClick();
                }
            }
        } else {
            data->point.x = last_raw_x;
            data->point.y = last_raw_y;
            data->state = LV_INDEV_STATE_RELEASED;

            if (was_pressed) {
                was_pressed = false;
                uint32_t duration = now - press_start;
                int dx = std::abs((int)last_scr_x - (int)start_scr_x);
                int dy = std::abs((int)last_scr_y - (int)start_scr_y);
                ESP_LOGI(TAG, "Touch RELEASED at scr(%d, %d), dur=%lums, dx=%d, dy=%d",
                         last_scr_x, last_scr_y, duration, dx, dy);

                lv_display_t* disp = lv_display_get_default();
                int cur_w = disp ? lv_display_get_horizontal_resolution(disp) : DISPLAY_WIDTH;
                int cur_h = disp ? lv_display_get_vertical_resolution(disp) : DISPLAY_HEIGHT;
                int top_bar_h = 36;
                int bot_bar_y = cur_h - 36;

                // Short tap (finger lifted, movement < 65px, duration < 750ms)
                if (duration < 750 && dx < 65 && dy < 65) {
                    if (self->nova_mode_) {
                        // Nova OS handles all buttons and touch UI natively through LVGL widgets!
                    } else if (self->chronchi_mode_) {
                        // --- MODE CHRONCHI ---
                        // Abaikan gestur HANYA jika tap berada di area bottom navbar (y >= cur_h - 28)
                        if (start_scr_y < (cur_h - 28)) {
                            // 1. 4x klik di tengah layar -> Beralih ke Mode Nova Mini OS!
                            if (start_scr_x >= (cur_w * 15 / 100) && start_scr_x <= (cur_w * 85 / 100) &&
                                start_scr_y >= (cur_h * 15 / 100) && start_scr_y <= (cur_h * 85 / 100)) {
                                static uint32_t last_chronchi_center_tap = 0;
                                static int chronchi_center_taps = 0;
                                if (now - last_chronchi_center_tap < 600) {
                                    chronchi_center_taps++;
                                } else {
                                    chronchi_center_taps = 1;
                                }
                                last_chronchi_center_tap = now;

                                if (chronchi_center_taps == 4) {
                                    chronchi_center_taps = 0;
                                    ESP_LOGI(TAG, "4x Tap di Tengah -> Beralih ke Mode Nova Mini OS!");
                                    self->SwitchToMode(2);
                                    return;
                                }
                            }

                            // 2. Klik 2 kali (Double-Tap) di layar -> Pindah-pindah tab layar (Semua screen Chronchi)
                            if (self->chronchi_display_) {
                                static uint32_t last_chronchi_any_tap = 0;
                                static int chronchi_any_taps = 0;
                                if (now - last_chronchi_any_tap < 850) {
                                    chronchi_any_taps++;
                                } else {
                                    chronchi_any_taps = 1;
                                }
                                last_chronchi_any_tap = now;

                                if (chronchi_any_taps == 2) {
                                    chronchi_any_taps = 0;
                                    ESP_LOGI(TAG, "Double-Tap di Chronchi -> Pindah Tab Layar!");
                                    self->chronchi_display_->cycleScreen();
                                }
                            }
                        }
                    } else {
                        // --- MODE XIAOZHI ---
                        // Abaikan gestur jika tap berada di area top bar atau bottom navbar
                        if (start_scr_y > top_bar_h && start_scr_y < bot_bar_y) {
                            static uint32_t last_xz_tap = 0;
                            static int xz_taps = 0;

                            if (now - last_xz_tap < 400) {
                                xz_taps++;
                            } else {
                                xz_taps = 1;
                            }
                            last_xz_tap = now;

                            if (xz_taps == 2) {
                                ESP_LOGI(TAG, "XiaoZhi 2x tap detected, scheduling toggle listening / standby...");
                                self->StartDoubleTapTimer(260);
                            } else if (xz_taps == 3) {
                                self->CancelDoubleTapTimer();
                                xz_taps = 0;

                                if (start_scr_x >= (cur_w * 15 / 100) && start_scr_x <= (cur_w * 85 / 100) &&
                                    start_scr_y >= (cur_h * 20 / 100) && start_scr_y <= (cur_h * 80 / 100)) {
                                    ESP_LOGI(TAG, "Triple Tap di Tengah -> Beralih ke Mode Nova Mini OS!");
                                    self->SwitchToMode(2);
                                }
                            }
                        }
                    }
                } else if (self->chronchi_mode_ && duration < 600 && dx > 45 && dy < 45) {
                    if (start_scr_y > top_bar_h && start_scr_y < bot_bar_y) {
                        if (start_scr_x > last_scr_x) {
                            // Swipe Kiri -> Pindah Tab Layar
                            ESP_LOGI(TAG, "Chronchi Swipe Kiri -> Layar Berikutnya");
                            if (self->chronchi_display_) {
                                self->chronchi_display_->cycleScreen();
                            }
                        } else {
                            // Swipe Kanan -> Layar Utama (Home)
                            ESP_LOGI(TAG, "Chronchi Swipe Kanan -> Layar Utama (Home)");
                            if (self->chronchi_display_) {
                                self->chronchi_display_->showHome();
                            }
                        }
                    }
                }
            }
        }
    }

    void InitializeTouch() {
        if (TOUCH_RST_PIN != GPIO_NUM_NC) {
            gpio_config_t rst_cfg = {
                .pin_bit_mask = (1ULL << TOUCH_RST_PIN),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&rst_cfg);
            gpio_set_level(TOUCH_RST_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(TOUCH_RST_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (!touch_.Init(codec_i2c_bus_, TOUCH_I2C_ADDR)) {
            ESP_LOGW(TAG, "Touch controller not detected (ES3N28P non-touch variant)");
            return;
        }

        bool touched = false;
        uint16_t x = 0, y = 0;
        if (!touch_.Read(touched, x, y)) {
            ESP_LOGW(TAG, "Touch controller I2C read failed (ES3N28P non-touch variant)");
            return;
        }

        lv_indev_t* indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, TouchpadRead);
        lv_indev_set_user_data(indev, this);
        lv_indev_set_disp(indev, lv_display_get_default());
        lv_indev_set_scroll_limit(indev, 16);
        lv_timer_t* indev_timer = lv_indev_get_read_timer(indev);
        if (indev_timer) {
            lv_timer_set_period(indev_timer, 15);
        }
        ESP_LOGI(TAG, "Capacitive touch indev initialized successfully for LVGL (15ms polling)");
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        if (nova_mode_) {
            boot_button_.OnClick([this]() {
                if (nova_display_) {
                    lvgl_port_lock(0);
                    if (nova_display_->GetCurrentPage() == NovaDisplay::PAGE_HOME) {
                        nova_display_->NavigateRoot(NovaDisplay::PAGE_APPS);
                    } else {
                        nova_display_->NavigateRoot(NovaDisplay::PAGE_HOME);
                    }
                    lvgl_port_unlock();
                }
            });

            boot_button_.OnDoubleClick([this]() {
                SwitchToMode(0); // Buka Asisten AI XiaoZhi
            });

            boot_button_.OnMultipleClick([this]() {
                SwitchToMode(1); // Buka Chronchi Smartwatch
            }, 3);

            boot_button_.OnLongPress([this]() {
                EnterWifiConfigMode();
            });
            return;
        }

        if (chronchi_mode_) {
            boot_button_.OnClick([this]() {
                if (chronchi_display_) {
                    chronchi_display_->cycleScreen();
                }
            });

            boot_button_.OnDoubleClick([this]() {
                SwitchToMode(2); // Kembali ke Nova Mini OS
            });

            boot_button_.OnMultipleClick([this]() {
                SwitchToMode(0); // Buka XiaoZhi AI
            }, 3);

            boot_button_.OnLongPress([this]() {
                SwitchToMode(2); // Kembali ke Nova Mini OS
            });
            return;
        }

        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                YouTubeAudioPoller::GetInstance().Abort();
                return;
            }
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.AbortSpeaking(kAbortReasonNone);
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnDoubleClick([this]() {
            YouTubeAudioPoller::GetInstance().PlayQuery("remix");
        });

        boot_button_.OnMultipleClick([this]() {
            SwitchToMode(2); // Beralih ke Nova Mini OS
        }, 3);

        boot_button_.OnLongPress([this]() {
            EnterWifiConfigMode();
        });
    }

    void InitializeLcdDisplay() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver ILI9341");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "Install LCD driver ILI9341 done");

        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io_, panel_,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
                           "End this conversation and enter WiFi configuration mode.\n"
                           "**CAUTION** You must ask the user to confirm this action.",
                           PropertyList(), [this](const PropertyList& properties) {
                               EnterWifiConfigMode();
                               return true;
                           });

        mcp_server.AddTool("self.system.get_battery_level",
                           "Get the current battery level, charging state, and voltage of the device.",
                           PropertyList(), [this](const PropertyList& properties) {
                               int level = 0;
                               bool charging = false, discharging = false;
                               if (GetBatteryLevel(level, charging, discharging)) {
                                   char buf[80];
                                   snprintf(buf, sizeof(buf), "Battery: %d%%, Charging: %s, Voltage: %dmV",
                                            level, charging ? "yes" : "no",
                                            battery_monitor_ ? battery_monitor_->GetVoltageMv() : 0);
                                   return std::string(buf);
                               }
                               return std::string("Battery monitor not available");
                           });

        mcp_server.AddTool("self.system.calibrate_battery",
                           "Kalibrasi tegangan baterai dengan membandingkan tegangan terukur multimeter (mV). "
                           "Parameter measured_mv: tegangan fisik baterai dalam milivolt (contoh: 3920 untuk 3.92V).",
                           PropertyList({Property("measured_mv", kPropertyTypeInteger)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               if (!battery_monitor_) {
                                   return "Battery monitor tidak tersedia";
                               }
                               int measured_mv = properties["measured_mv"].value<int>();
                               if (measured_mv < 2500 || measured_mv > 5000) {
                                   return "Nilai tegangan tidak valid (harus antara 2500mV s/d 5000mV)";
                               }
                               int current_mv = battery_monitor_->GetVoltageMv();
                               if (current_mv <= 0) {
                                   return "Gagal membaca tegangan saat ini";
                               }
                               float old_factor = battery_monitor_->GetCalibrationFactor();
                               float new_factor = old_factor * ((float)measured_mv / (float)current_mv);
                               battery_monitor_->SetCalibrationFactor(new_factor);
                               char buf[128];
                               snprintf(buf, sizeof(buf), "Kalibrasi berhasil! Faktor lama: %.4f -> Baru: %.4f, Vbat sekarang: %dmV",
                                        old_factor, new_factor, battery_monitor_->GetVoltageMv());
                               return std::string(buf);
                           });

        mcp_server.AddTool("self.system.set_charging_full_led_color",
            "Atur pilihan warna LED saat baterai terisi penuh (charging selesai). "
            "Pilihan parameter color: 'green' (hijau - default) atau 'blue' (biru).",
            PropertyList({Property("color", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string col = properties["color"].value<std::string>();
                bool use_blue = (col == "blue" || col == "biru");
                if (led_) {
                    led_->SetFullChargeColor(use_blue);
                }
                return use_blue ? "Warna LED saat baterai penuh diatur ke BIRU" : "Warna LED saat baterai penuh diatur ke HIJAU";
            });

        mcp_server.AddTool("self.system.toggle_touch_haptic",
            "Aktifkan atau nonaktifkan umpan balik suara klik haptik halus saat layar sentuh ditekan. "
            "Parameter: enabled (boolean, true untuk aktifkan, false untuk matikan).",
            PropertyList({Property("enabled", kPropertyTypeBoolean)}),
            [this](const PropertyList& properties) -> ReturnValue {
                bool enabled = properties["enabled"].value<bool>();
                SetTouchHaptic(enabled);
                return enabled ? "Suara haptik layar sentuh diaktifkan" : "Suara haptik layar sentuh dinonaktifkan";
            });

        // Tool Panduan Gestur Layar Sentuh & Tombol untuk XiaoZhi AI
        auto guide_handler = [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "title", "Panduan Kontrol Layar Sentuh & Tombol Fisik (LCDWiki 2.8\")");
            cJSON_AddStringToObject(root, "device", "LCDWiki ES3C28P 2.8-inch Capacitive Touch");

            cJSON* gestures = cJSON_CreateArray();

            cJSON* g1 = cJSON_CreateObject();
            cJSON_AddStringToObject(g1, "gesture", "Ketuk 1x di Layar (Single Tap)");
            cJSON_AddStringToObject(g1, "function", "Bicara / Interupsi / Stop Lagu");
            cJSON_AddStringToObject(g1, "description", "Mulai atau hentikan percakapan AI. Jika AI sedang berbicara, ketukan 1x akan langsung menyela/menghentikan bicara AI. Jika sedang memutar lagu YouTube, ketukan 1x akan menghentikan lagu.");
            cJSON_AddItemToArray(gestures, g1);

            cJSON* g2 = cJSON_CreateObject();
            cJSON_AddStringToObject(g2, "gesture", "Ketuk 2x di Layar (Double Tap)");
            cJSON_AddStringToObject(g2, "function", "Mulai Mendengarkan / Standby (Toggle Chat)");
            cJSON_AddStringToObject(g2, "description", "Ketuk layar 2 kali untuk beralih antara mendengarkan (listening) dan standby. Jika AI sedang berbicara atau musik sedang diputar, ketukan 2x akan langsung menghentikannya.");
            cJSON_AddItemToArray(gestures, g2);
            cJSON* g_tri = cJSON_CreateObject();
            cJSON_AddStringToObject(g_tri, "gesture", "Ketuk 3x di Tengah Layar (Triple Tap)");
            cJSON_AddStringToObject(g_tri, "function", "Beralih ke Mode Jam Chronchi");
            cJSON_AddStringToObject(g_tri, "description", "Ketuk layar 3 kali secara cepat di area tengah untuk beralih ke mode smartwatch / jam meja Chronchi (dan sebaliknya ketuk 4x di Chronchi untuk kembali ke XiaoZhi).");
            cJSON_AddItemToArray(gestures, g_tri);

            cJSON* g3 = cJSON_CreateObject();
            cJSON_AddStringToObject(g3, "gesture", "Geser Jari ke Atas di Sisi Kanan (Swipe UP)");
            cJSON_AddStringToObject(g3, "function", "Volume Naik (+10%)");
            cJSON_AddStringToObject(g3, "description", "Menaikkan volume speaker sebesar 10% setiap usapan.");
            cJSON_AddItemToArray(gestures, g3);

            cJSON* g4 = cJSON_CreateObject();
            cJSON_AddStringToObject(g4, "gesture", "Geser Jari ke Bawah di Sisi Kanan (Swipe DOWN)");
            cJSON_AddStringToObject(g4, "function", "Volume Turun (-10%)");
            cJSON_AddStringToObject(g4, "description", "Menurunkan volume speaker sebesar 10% setiap usapan.");
            cJSON_AddItemToArray(gestures, g4);

            cJSON* g5 = cJSON_CreateObject();
            cJSON_AddStringToObject(g5, "gesture", "Geser Layar ke Kiri atau Kanan (Swipe Horizontal)");
            cJSON_AddStringToObject(g5, "function", "Beralih ke Mode Jam Chronchi");
            cJSON_AddStringToObject(g5, "description", "Beralih antara mode Asisten Suara XiaoZhi dan mode Smartwatch/Jam Meja Chronchi.");
            cJSON_AddItemToArray(gestures, g5);

            cJSON* g6 = cJSON_CreateObject();
            cJSON_AddStringToObject(g6, "gesture", "Ketuk 2x di Pojok Kiri Atas Layar");
            cJSON_AddStringToObject(g6, "function", "Rotasi Layar 180°");
            cJSON_AddStringToObject(g6, "description", "Memutar orientasi tampilan layar 180 derajat seketika tanpa perlu restart.");
            cJSON_AddItemToArray(gestures, g6);

            cJSON* g7 = cJSON_CreateObject();
            cJSON_AddStringToObject(g7, "gesture", "Tekan dan Tahan Layar >2.5 Detik (Long Press)");
            cJSON_AddStringToObject(g7, "function", "Portal Konfigurasi Wi-Fi / Mute / Max Volume");
            cJSON_AddStringToObject(g7, "description", "Tahan di tengah layar untuk membuka hotspot Wi-Fi (192.168.4.1). Tahan di pojok kanan atas untuk Volume 100%, atau pojok kanan bawah untuk Mute 0%.");
            cJSON_AddItemToArray(gestures, g7);

            cJSON_AddItemToObject(root, "touch_gestures", gestures);

            cJSON* btn = cJSON_CreateObject();
            cJSON_AddStringToObject(btn, "button", "Tombol Fisik BOOT (GPIO 0)");
            cJSON_AddStringToObject(btn, "single_click", "Mulai bicara / Interupsi suara AI / Stop YouTube.");
            cJSON_AddStringToObject(btn, "double_click", "Putar lagu remix YouTube.");
            cJSON_AddStringToObject(btn, "triple_click", "Beralih ke mode Chronchi Smartwatch.");
            cJSON_AddStringToObject(btn, "long_press", "Masuk mode pengaturan Wi-Fi.");
            cJSON_AddItemToObject(root, "physical_button", btn);

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);
            return result;
        };

        mcp_server.AddTool("self.get_touch_and_buttons_guide",
            "Dapatkan panduan lengkap kontrol gestur layar sentuh (touchscreen) dan tombol fisik pada perangkat LCDWiki 2.8 inci ini. "
            "Panggil tool ini jika pengguna bertanya tentang: fungsi layar sentuh, cara pakai touchscreen, gestur apa saja yang ada, "
            "cara menaikkan volume lewat layar, cara memutar lagu remix YouTube, cara rotasi layar 180 derajat, cara beralih ke mode jam Chronchi, "
            "atau fungsi tombol BOOT fisik.",
            PropertyList(), guide_handler);
    }

public:
    LCDWikiES3C28P_Board() : boot_button_(BOOT_BUTTON_GPIO) {
        // Ensure default boot mode is XiaoZhi AI Assistant (Mode 0) & Theme 1
        Settings chronchi_settings("chronchi", true);
        if (chronchi_settings.GetInt("boot_to_xz", 0) == 0) {
            chronchi_settings.SetInt("mode", 0);
            chronchi_settings.SetInt("boot_to_xz", 1);
        }
        boot_mode_ = chronchi_settings.GetInt("mode", 0);
        Settings disp_settings("display", true);
        if (disp_settings.GetInt("def_th2_v5", 0) == 0) {
            disp_settings.SetInt("ui_theme", 2);
            disp_settings.SetInt("def_th2_v5", 1);
        }
        rotation_ = disp_settings.GetInt("rotation", 0);

        // Ensure default Wi-Fi TX Power is 20 dBm (Maximum range)
        Settings wifi_settings("wifi", true);
        if (wifi_settings.GetInt("def_tx20_v1", 0) == 0) {
            wifi_settings.SetInt("max_tx_power", 20);
            wifi_settings.SetInt("def_tx20_v1", 1);
        }

        InitializeI2c();
        InitializeBatteryMonitor();
        InitializeLed();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();

        if (rotation_ != 0) {
            ApplyDisplayRotation();
        }

        // Inisialisasi Zona Waktu Sistem (Default: WIB UTC+7 / Indonesia Bagian Barat)
        Settings sys_settings("system", true);
        int tz_idx = sys_settings.GetInt("tz_idx", 0);
        static const char* kTimezonesEnv[] = {
            "WIB-7", "WITA-8", "WIT-9", "GMT0", "JST-9", "EST5EDT,M3.2.0,M11.1.0"
        };
        const char* tz_str = kTimezonesEnv[(tz_idx >= 0 && tz_idx < 6) ? tz_idx : 0];
        setenv("TZ", tz_str, 1);
        tzset();
        ESP_LOGI(TAG, "System timezone initialized to %s", tz_str);

        esp_timer_create_args_t dt_args = {
            .callback = [](void* arg) {
                auto* board = static_cast<LCDWikiES3C28P_Board*>(arg);
                board->OnDoubleTapTriggered();
            },
            .arg = this,
            .name = "dt_timer"
        };
        esp_timer_create(&dt_args, &double_tap_timer_);

        Settings touch_settings("touch", true);
        haptic_enabled_ = touch_settings.GetInt("haptic", 1) != 0;

        sound_fx_queue_ = xQueueCreate(8, sizeof(int));

        xTaskCreatePinnedToCore([](void* arg) {
            auto* self = static_cast<LCDWikiES3C28P_Board*>(arg);
            self->HapticClickTask();
            vTaskDelete(NULL);
        }, "haptic_click", 4096, this, 4, &haptic_task_handle_, 1);

        InitializeTemperatureSensor();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual ~LCDWikiES3C28P_Board() {
#if CONFIG_BT_ENABLED
        if (ble_task_handle_) {
            vTaskDelete(ble_task_handle_);
            ble_task_handle_ = nullptr;
        }
#endif
        if (haptic_task_handle_) {
            vTaskDelete(haptic_task_handle_);
            haptic_task_handle_ = nullptr;
        }
        if (sound_fx_queue_) {
            vQueueDelete(sound_fx_queue_);
            sound_fx_queue_ = nullptr;
        }
        if (temp_sensor_) {
            temperature_sensor_disable(temp_sensor_);
            temperature_sensor_uninstall(temp_sensor_);
            temp_sensor_ = nullptr;
        }
        if (double_tap_timer_) {
            esp_timer_stop(double_tap_timer_);
            esp_timer_delete(double_tap_timer_);
        }
        if (charging_led_timer_) {
            esp_timer_stop(charging_led_timer_);
            esp_timer_delete(charging_led_timer_);
            charging_led_timer_ = nullptr;
        }
        StopWav();
        delete battery_monitor_;
        delete led_;
        delete chronchi_display_;
        delete nova_display_;
    }

    virtual Led* GetLed() override {
        return led_;
    }


    virtual AudioCodec* GetAudioCodec() override {
        static struct LCDWikiES3C28PAudioCodec : public Es8311AudioCodec {
            LCDWikiES3C28PAudioCodec(void* i2c, i2c_port_t port, int in_rate, int out_rate,
                gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                gpio_num_t pa, uint8_t addr, bool use_mclk, bool pa_inverted)
                : Es8311AudioCodec(i2c, port, in_rate, out_rate,
                    mclk, bclk, ws, dout, din, pa, addr, use_mclk, pa_inverted) {
                input_gain_ = 30.0; // High-sensitivity microphone gain (+30 dB)
            }
        } audio_codec(codec_i2c_bus_, AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        if (!battery_monitor_) {
            return false;
        }
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        level = battery_monitor_->GetBatteryLevel();
        return true;
    }

    virtual void ShowDownloadProgress(const char* title, int progress_pct, const char* status_text = nullptr) override {
        // 1. Selalu perbarui animasi progress bar di kolom chat XiaoZhi (LcdDisplay)
        Board::ShowDownloadProgress(title, progress_pct, status_text);

        // 2. Jika di Nova OS (Mode 2), perbarui juga kartu modal download pop-up
        if (nova_display_) {
            lvgl_port_lock(0);
            nova_display_->ShowDownloadModal(title ? title : "Mengunduh Lagu", progress_pct, status_text ? status_text : "");
            lvgl_port_unlock();
        }
    }

    virtual void CloseDownloadProgress(bool success = true) override {
        Board::CloseDownloadProgress(success);
        if (nova_display_) {
            lvgl_port_lock(0);
            nova_display_->CloseDownloadModal(success);
            lvgl_port_unlock();
        }
    }
};

DECLARE_BOARD(LCDWikiES3C28P_Board);
