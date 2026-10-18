#pragma once

#include <atomic>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ChronchiAlert {
public:
    static ChronchiAlert& GetInstance();

    // Inisialisasi pin motor getar dan LED indikator
    void Initialize(gpio_num_t motor_pin = GPIO_NUM_NC, gpio_num_t led_pin = GPIO_NUM_NC);

    // Bunyikan nada notifikasi (chime merdu) via MAX98357A speaker + getar 2x
    void TriggerNotificationAlert();

    // Getar 2x pendek & bunyikan chime notifikasi (WhatsApp, dsb.)
    void VibrateNotification();

    // Bunyikan suara bell nyaring ("Ding-Dong!" 2048Hz/1706Hz) via MAX98357A speaker 3W (100% volume) + getar
    void StartFindDeviceAlarm();

    // Peringatan panggilan telepon masuk (dering berkala + getar)
    void StartCallAlert();

    // Hentikan suara dan getaran seketika
    void StopAlert();

    // Periksa apakah alarm sedang berbunyi/bergetar
    bool IsAlarmActive() const { return is_alarm_active_.load(); }

    // Kontrol langsung motor getar
    void SetVibration(bool on);

private:
    ChronchiAlert();
    ~ChronchiAlert();

    static void NotificationTask(void* arg);
    static void AlarmTask(void* arg);

    gpio_num_t motor_pin_ = GPIO_NUM_NC;
    gpio_num_t led_pin_ = GPIO_NUM_NC;
    bool initialized_ = false;

    std::atomic<bool> is_alarm_active_{false};
    std::atomic<bool> is_notif_playing_{false};
    TaskHandle_t alarm_task_handle_ = nullptr;
    TaskHandle_t notif_task_handle_ = nullptr;
};
