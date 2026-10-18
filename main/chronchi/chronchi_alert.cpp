#include "chronchi/chronchi_alert.h"
#include "board.h"
#include "audio_codec.h"

#include <esp_log.h>
#include <cmath>
#include <vector>

static const char* TAG = "ChronchiAlert";

ChronchiAlert& ChronchiAlert::GetInstance() {
    static ChronchiAlert instance;
    return instance;
}

ChronchiAlert::ChronchiAlert() {}

ChronchiAlert::~ChronchiAlert() {
    StopAlert();
}

void ChronchiAlert::Initialize(gpio_num_t motor_pin, gpio_num_t led_pin) {
    if (initialized_) return;
    motor_pin_ = motor_pin;
    led_pin_ = led_pin;

    if (motor_pin_ != GPIO_NUM_NC && (int)motor_pin_ >= 0) {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << (int)motor_pin_);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(motor_pin_, 0);
        ESP_LOGI(TAG, "Motor getar diaktifkan pada GPIO %d", (int)motor_pin_);
    }

    if (led_pin_ != GPIO_NUM_NC && (int)led_pin_ >= 0) {
        gpio_config_t led_conf = {};
        led_conf.pin_bit_mask = (1ULL << (int)led_pin_);
        led_conf.mode = GPIO_MODE_OUTPUT;
        led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        led_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        led_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&led_conf);
        gpio_set_level(led_pin_, 0);
        ESP_LOGI(TAG, "LED indikator diaktifkan pada GPIO %d", (int)led_pin_);
    }

    initialized_ = true;
}

void ChronchiAlert::SetVibration(bool on) {
    if (motor_pin_ != GPIO_NUM_NC && (int)motor_pin_ >= 0) {
        gpio_set_level(motor_pin_, on ? 1 : 0);
    }
    if (led_pin_ != GPIO_NUM_NC && (int)led_pin_ >= 0) {
        gpio_set_level(led_pin_, on ? 1 : 0);
    }
}

void ChronchiAlert::TriggerNotificationAlert() {
    if (is_alarm_active_.load()) {
        return; // Jangan potong alarm temukan perangkat yang sedang nyaring
    }
    if (is_notif_playing_.load()) {
        return; // Hindari penumpukan task jika banyak notifikasi masuk sekaligus
    }
    is_notif_playing_.store(true);
    xTaskCreate(NotificationTask, "notif_alert", 4096, this, 4, &notif_task_handle_);
}

void ChronchiAlert::VibrateNotification() {
    TriggerNotificationAlert();
}

void ChronchiAlert::NotificationTask(void* arg) {
    auto* self = static_cast<ChronchiAlert*>(arg);
    ESP_LOGI(TAG, "Notifikasi masuk: membunyikan chime speaker + 2x haptic getar");

    self->SetVibration(true);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        codec->SetOutputVolume(95); // 95% volume jelas dan jernih
        codec->EnableOutput(true);

        constexpr int kSampleRate = 24000;
        constexpr int kChunkSize = 256;
        constexpr int kTotalSamples = (int)(0.30f * kSampleRate); // 300ms total
        constexpr int kNote1Samples = (int)(0.12f * kSampleRate); // 120ms nada pertama (E6)
        constexpr int kNote2Start = (int)(0.09f * kSampleRate);   // 90ms nada kedua mulai (A6)
        constexpr int kNote2Samples = kTotalSamples - kNote2Start;

        constexpr float kFreq1 = 1318.51f; // E6 (1318.5 Hz)
        constexpr float kFreq2 = 1760.00f; // A6 (1760.0 Hz)
        constexpr float kAmp = 26000.0f;

        std::vector<int16_t> chunk(kChunkSize);
        int sample_pos = 0;

        while (sample_pos < kTotalSamples) {
            for (int i = 0; i < kChunkSize; ++i) {
                int pos = sample_pos + i;
                float val = 0.0f;

                if (pos < kNote1Samples) {
                    float t = (float)pos / (float)kSampleRate;
                    float env = expf(-6.0f * (float)pos / (float)kNote1Samples);
                    val += (sinf(2.0f * M_PI * kFreq1 * t) + 0.25f * sinf(4.0f * M_PI * kFreq1 * t)) * kAmp * env;
                }
                if (pos >= kNote2Start && pos < kTotalSamples) {
                    int p2 = pos - kNote2Start;
                    float t = (float)p2 / (float)kSampleRate;
                    float env = expf(-5.0f * (float)p2 / (float)kNote2Samples);
                    val += (sinf(2.0f * M_PI * kFreq2 * t) + 0.25f * sinf(4.0f * M_PI * kFreq2 * t)) * kAmp * env;
                }

                int32_t clamped = (int32_t)val;
                if (clamped > 32767) clamped = 32767;
                if (clamped < -32768) clamped = -32768;
                chunk[i] = (int16_t)clamped;
            }

            codec->OutputData(chunk);
            sample_pos += kChunkSize;
        }

        // Flush DMA dengan sedikit silence
        std::vector<int16_t> silence(kChunkSize, 0);
        codec->OutputData(silence);
    } else {
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Pola getar kedua
    self->SetVibration(false);
    vTaskDelay(pdMS_TO_TICKS(60));
    self->SetVibration(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    self->SetVibration(false);

    self->is_notif_playing_.store(false);
    self->notif_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void ChronchiAlert::StartFindDeviceAlarm() {
    if (is_alarm_active_.load()) {
        ESP_LOGW(TAG, "Alarm Find Device sudah aktif");
        return;
    }
    is_alarm_active_.store(true);
    xTaskCreate(AlarmTask, "find_device_alarm", 4096, this, 5, &alarm_task_handle_);
}

void ChronchiAlert::StartCallAlert() {
    StartFindDeviceAlarm();
}

void ChronchiAlert::StopAlert() {
    if (!is_alarm_active_.load()) return;
    ESP_LOGI(TAG, "Menghentikan alarm & getaran Chronchi");
    is_alarm_active_.store(false);
    SetVibration(false);

    // Audio codec silence
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        std::vector<int16_t> silence(256, 0);
        codec->OutputData(silence);
    }
}

void ChronchiAlert::AlarmTask(void* arg) {
    auto* self = static_cast<ChronchiAlert*>(arg);
    ESP_LOGI(TAG, "Memulai alarm bel Find My Device nyaring maksimal via speaker...");

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        codec->SetOutputVolume(100); // 100% volume maksimal
        codec->EnableOutput(true);
    }

    constexpr int kSampleRate = 24000;
    constexpr int kChunkSize = 256;
    std::vector<int16_t> audio_chunk(kChunkSize);

    // Frekuensi dentang bel kristal "Ding-Dong!"
    // Ding: 2048 Hz fundamental + 4096 Hz overtone
    // Dong: 1706 Hz fundamental + 3412 Hz overtone
    constexpr float kFreqDing = 2048.0f;
    constexpr float kFreqDong = 1706.0f;
    constexpr float kMaxAmp = 30000.0f; // Nyaring maksimal tanpa distorsi

    constexpr int kCycleSamples = (int)(1.2f * kSampleRate); // 1.2 detik per siklus dentang
    constexpr int kDingSamples = (int)(0.35f * kSampleRate); // 350ms Ding
    constexpr int kDongPause = (int)(0.08f * kSampleRate);   // 80ms jeda
    constexpr int kDongSamples = (int)(0.45f * kSampleRate); // 450ms Dong
    constexpr int kDongEnd = kDingSamples + kDongPause + kDongSamples; // 880ms

    int total_elapsed_samples = 0;
    constexpr int kMaxDurationSamples = 30 * kSampleRate; // 30 detik auto-timeout

    while (self->is_alarm_active_.load() && total_elapsed_samples < kMaxDurationSamples) {
        int cycle_pos = total_elapsed_samples % kCycleSamples;

        // Ritmik haptic getaran berdenyut bersama dentang bell
        if (cycle_pos < (kDingSamples + kDongSamples)) {
            self->SetVibration(true);
        } else {
            self->SetVibration(false);
        }

        for (int i = 0; i < kChunkSize; ++i) {
            int pos = cycle_pos + i;
            float sample_val = 0.0f;

            if (pos < kDingSamples) {
                // "Ding!" - Nada tinggi 2048Hz meluruh eksponensial
                float t = (float)pos / (float)kSampleRate;
                float decay = expf(-5.5f * (float)pos / (float)kDingSamples);
                float f1 = sinf(2.0f * (float)M_PI * kFreqDing * t);
                float f2 = sinf(2.0f * (float)M_PI * (kFreqDing * 2.0f) * t) * 0.35f;
                sample_val = (f1 + f2) * kMaxAmp * decay;
            } else if (pos >= (kDingSamples + kDongPause) && pos < kDongEnd) {
                // "Dong!" - Nada resonansi 1706Hz meluruh eksponensial
                int dong_pos = pos - (kDingSamples + kDongPause);
                float t = (float)dong_pos / (float)kSampleRate;
                float decay = expf(-4.5f * (float)dong_pos / (float)kDongSamples);
                float f1 = sinf(2.0f * (float)M_PI * kFreqDong * t);
                float f2 = sinf(2.0f * (float)M_PI * (kFreqDong * 2.0f) * t) * 0.3f;
                sample_val = (f1 + f2) * kMaxAmp * decay;
            } else {
                sample_val = 0.0f;
            }

            int32_t clamped = (int32_t)sample_val;
            if (clamped > 32767) clamped = 32767;
            if (clamped < -32768) clamped = -32768;
            audio_chunk[i] = (int16_t)clamped;
        }

        if (codec) {
            codec->OutputData(audio_chunk);
        }

        total_elapsed_samples += kChunkSize;

        // Beri jeda kecil CPU hanya saat jeda hening antar dentang
        if (cycle_pos >= kDongEnd) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    self->SetVibration(false);
    self->is_alarm_active_.store(false);
    self->alarm_task_handle_ = nullptr;
    ESP_LOGI(TAG, "Alarm Find Device selesai.");
    vTaskDelete(nullptr);
}
