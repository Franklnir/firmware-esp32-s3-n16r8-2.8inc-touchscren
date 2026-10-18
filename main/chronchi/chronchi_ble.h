#pragma once

#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <sdkconfig.h>

#if CONFIG_BT_ENABLED
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_common_api.h>
#include <esp_gatts_api.h>
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// 1. Chronos Official Nordic UART Service (NUS) BLE UUIDs
#define CHRONOS_NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHRONOS_NUS_CHAR_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHRONOS_NUS_CHAR_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// 2. Chronchi (ESPBridge V1) BLE UUIDs
#define CHRONCHI_SERVICE_UUID "7c9e0001-6f2f-4d4d-9f25-0d7fd4f0a001"
#define CHRONCHI_CHAR_RX_UUID "7c9e0002-6f2f-4d4d-9f25-0d7fd4f0a001"
#define CHRONCHI_CHAR_TX_UUID "7c9e0003-6f2f-4d4d-9f25-0d7fd4f0a001"

// Chronos watch control commands
enum ChronosControl {
    MUSIC_PLAY = 0x9D00,
    MUSIC_PAUSE = 0x9D01,
    MUSIC_PREVIOUS = 0x9D02,
    MUSIC_NEXT = 0x9D03,
    MUSIC_TOGGLE = 0x9900,
    VOLUME_UP = 0x99A1,
    VOLUME_DOWN = 0x99A2,
    VOLUME_MUTE = 0x99A3,
};

// Data structures
struct ChronchiNotification {
    int icon = 0;
    std::string app;
    std::string time;
    std::string title;
    std::string message;
};

struct ChronchiNavigation {
    bool active = false;
    bool isNavigation = false;
    bool hasIcon = false;
    std::string distance;
    std::string duration;
    std::string eta;
    std::string title;
    std::string directions;
    std::string speed;
    std::string maneuver;
    uint8_t icon[288] = {};
    uint32_t iconCRC = 0;
};

struct ChronchiWeather {
    bool valid = false;
    float temperatureC = 0;
    std::string condition;
    int humidity = 0;
    std::string location;
    int highC = 0;
    int lowC = 0;
    int pressure = 0;
    int uv = 0;
};

struct ChronchiLocation {
    bool valid = false;
    double latitude = 0;
    double longitude = 0;
    std::string city;
    std::string country;
    float speedMps = 0;
    float bearingDegrees = 0;
    float accuracyMeters = 0;
};

struct ChronchiNetwork {
    std::string transport;
    std::string generation;
    int signalLevel = 0;
};

struct ChronchiPhoneStatus {
    int batteryLevel = 0;
    bool charging = false;
    bool bluetoothEnabled = false;
    ChronchiNetwork network;
};

struct ChronchiMusic {
    uint8_t state = 0; // 0 = paused, 1 = playing
    std::string title;
    std::string artist;
    std::string app;
};

struct ChronchiAlarm {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t repeat = 0;
    bool enabled = false;
};

struct ChronchiReminder {
    bool enabled = false;
    uint8_t startHour = 8;
    uint8_t startMinute = 0;
    uint8_t endHour = 20;
    uint8_t endMinute = 0;
    uint8_t intervalMinutes = 60;
};

struct ChronchiQuietHours {
    bool enabled = false;
    uint8_t startHour = 22;
    uint8_t startMinute = 0;
    uint8_t endHour = 7;
    uint8_t endMinute = 0;
};

struct ChronchiContact {
    std::string name;
    std::string number;
};

// Callback types
using ChronchiConnectionCallback = std::function<void(bool connected)>;
using ChronchiNotificationCallback = std::function<void(const ChronchiNotification& notif)>;
using ChronchiNavigationCallback = std::function<void(const ChronchiNavigation& nav)>;
using ChronchiWeatherCallback = std::function<void(const ChronchiWeather& weather)>;
using ChronchiLocationCallback = std::function<void(const ChronchiLocation& location)>;
using ChronchiPhoneStatusCallback = std::function<void(const ChronchiPhoneStatus& status)>;
using ChronchiMusicCallback = std::function<void(const ChronchiMusic& music)>;
using ChronchiRingerCallback = std::function<void(const std::string& caller, bool active)>;
using ChronchiWatchfaceCallback = std::function<void(uint32_t color, uint32_t layout)>;
using ChronchiFindWatchCallback = std::function<void(bool active)>;
using ChronchiCameraStateCallback = std::function<void(bool ready)>;
using ChronchiTimeFormatCallback = std::function<void(bool is_24h)>;
using ChronchiAlarmCallback = std::function<void(uint8_t index, const ChronchiAlarm& alarm)>;
using ChronchiReminderCallback = std::function<void(const std::string& type, const ChronchiReminder& reminder)>;

#if CONFIG_BT_ENABLED
class ChronchiBle {
public:
    ChronchiBle();
    ~ChronchiBle();

    bool begin(const char* device_name = "Chronchi");
    void stop();
    void loop();
    bool isConnected() const;
    bool isSubscribed() const;

    void setBattery(uint8_t level, bool charging);

    ChronchiNavigation getNavigation() const;
    ChronchiWeather getWeather() const;
    ChronchiLocation getLocation() const;
    ChronchiPhoneStatus getPhoneStatus() const;
    ChronchiMusic getMusic() const;
    int getNotificationCount() const;
    ChronchiNotification getNotificationAt(int index) const;
    void clearNotifications();
    bool isCameraReady() const;
    ChronchiAlarm getAlarm(uint8_t index) const;
    ChronchiReminder getSedentaryReminder() const;
    ChronchiReminder getWaterReminder() const;
    ChronchiQuietHours getQuietHours() const;
    bool isQuietActive() const;
    bool isRaiseToWakeEnabled() const;
    int getContactCount() const;
    ChronchiContact getContactAt(int index) const;
    ChronchiContact getSOSContact() const;

    // Outgoing commands (supported by Chronos & Chronchi)
    void sendCommand(const uint8_t* command, size_t length, bool force_chunked = false);
    void musicControl(ChronosControl command);
    void setVolume(uint8_t level);
    bool capturePhoto();
    void findPhone(bool state);
    void syncRequest();
    void setNotifyBattery(bool state);

    // Callbacks
    void setConnectionCallback(ChronchiConnectionCallback cb) { _connCb = std::move(cb); }
    void setNotificationCallback(ChronchiNotificationCallback cb) { _notifCb = std::move(cb); }
    void setNavigationCallback(ChronchiNavigationCallback cb) { _navCb = std::move(cb); }
    void setWeatherCallback(ChronchiWeatherCallback cb) { _weatherCb = std::move(cb); }
    void setLocationCallback(ChronchiLocationCallback cb) { _locCb = std::move(cb); }
    void setPhoneStatusCallback(ChronchiPhoneStatusCallback cb) { _phoneCb = std::move(cb); }
    void setMusicCallback(ChronchiMusicCallback cb) { _musicCb = std::move(cb); }
    void setRingerCallback(ChronchiRingerCallback cb) { _ringerCb = std::move(cb); }
    void setWatchfaceCallback(ChronchiWatchfaceCallback cb) { _watchfaceCb = std::move(cb); }
    void setFindWatchCallback(ChronchiFindWatchCallback cb) { _findWatchCb = std::move(cb); }
    void setCameraStateCallback(ChronchiCameraStateCallback cb) { _cameraStateCb = std::move(cb); }
    void setTimeFormatCallback(ChronchiTimeFormatCallback cb) { _timeFormatCb = std::move(cb); }
    void setAlarmCallback(ChronchiAlarmCallback cb) { _alarmCb = std::move(cb); }
    void setReminderCallback(ChronchiReminderCallback cb) { _reminderCb = std::move(cb); }

private:
    struct QueueItem {
        uint8_t data[512];
        uint16_t len;
    };

    // Protocol processing
    void _handleRxChunk(const uint8_t* data, uint16_t len);

    // Chronos Protocol
    void _processChronosPacket(const uint8_t* data, uint16_t len);
    void _sendInfo();
    void _sendBattery();
    void _sendESP();
    void _splitTitle(const std::string& input, std::string& title, std::string& message, int icon);
    const char* _appName(int id);
    const char* _weatherCondition(int icon);

    // Chronchi (ESPBridge V1) Protocol
    void _processEspBridgePacket(uint8_t type, const char* json, uint8_t sequence);
    void _handleNotification(const char* json);
    void _handleNavigation(const char* json);
    void _handleTimeSync(const char* json);
    void _handlePhoneStatus(const char* json);
    void _handleNetworkStatus(const char* json);
    void _handleWeather(const char* json);
    void _handleLocation(const char* json);
    void _sendDeviceStatus();
    void _sendEspBridgePacket(uint8_t type, const char* jsonPayload, int sequence = -1);

    // BLE callbacks
    static void _gattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                   esp_ble_gatts_cb_param_t* param);
    static void _gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
    void _onGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                       esp_ble_gatts_cb_param_t* param);
    void _onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);

    // State
    std::string _device_name;
    bool _connected = false;
    bool _chronos_subscribed = false;
    bool _chronchi_subscribed = false;
    bool _cameraReady = false;
    bool _sendEspPending = false;
    TickType_t _handshake_tick = 0;
    TickType_t _chronchi_ready_tick = 0;
    esp_gatt_if_t _gatts_if = ESP_GATT_IF_NONE;
    uint16_t _conn_id = 0;
    esp_bd_addr_t _remote_bda = {};
    uint16_t _mtu = 23;

    // GATT handles for Chronos NUS Service
    uint32_t _chronos_svc_handle = 0;
    uint16_t _chronos_tx_handle = 0;
    uint16_t _chronos_tx_cccd_handle = 0;
    uint16_t _chronos_rx_handle = 0;

    // GATT handles for Chronchi (ESPBridge) Service
    uint32_t _chronchi_svc_handle = 0;
    uint16_t _chronchi_tx_handle = 0;
    uint16_t _chronchi_tx_cccd_handle = 0;
    uint16_t _chronchi_rx_handle = 0;

    // Advertising params
    esp_ble_adv_params_t _adv_params = {};
    uint8_t _adv_config_done = 0;
    bool _advertising = false;

    // Battery
    uint8_t _batteryLevel = 100;
    bool _isCharging = false;

    // Data models
    static constexpr size_t MAX_NOTIFICATIONS = 10;
    std::vector<ChronchiNotification> _notifications;
    ChronchiNavigation _navigation;
    ChronchiWeather _weather;
    ChronchiLocation _location;
    ChronchiPhoneStatus _phoneStatus;
    ChronchiMusic _music;
    mutable std::mutex _dataMutex;

    // Reassembly buffers
    QueueHandle_t _rx_queue = nullptr;

    // Chronos reassembly
    uint8_t _chronos_incoming_buf[512] = {};
    uint16_t _chronos_expected_len = 0;
    uint16_t _chronos_received_len = 0;

    // ESPBridge reassembly
    struct ReassemblyState {
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t fragment_count = 0;
        std::vector<uint8_t> payload;
        void Reset() {
            type = 0;
            sequence = 0;
            fragment_count = 0;
            payload.clear();
        }
    };
    ReassemblyState _espbridge_reassembly;

    // Callbacks
    ChronchiConnectionCallback _connCb;
    ChronchiNotificationCallback _notifCb;
    ChronchiNavigationCallback _navCb;
    ChronchiWeatherCallback _weatherCb;
    ChronchiLocationCallback _locCb;
    ChronchiPhoneStatusCallback _phoneCb;
    ChronchiMusicCallback _musicCb;
    ChronchiRingerCallback _ringerCb;
    ChronchiWatchfaceCallback _watchfaceCb;
    ChronchiFindWatchCallback _findWatchCb;
    ChronchiCameraStateCallback _cameraStateCb;
    ChronchiTimeFormatCallback _timeFormatCb;
    ChronchiAlarmCallback _alarmCb;
    ChronchiReminderCallback _reminderCb;

    // Alarms
    ChronchiAlarm _alarms[8] = {};

    // Reminders & Quiet hours
    ChronchiReminder _sedentary;
    ChronchiReminder _water;
    ChronchiQuietHours _quietHours;
    bool _raiseToWake = false;
    uint8_t _languageIndex = 0;

    // Contacts
    std::vector<ChronchiContact> _contacts;
    uint8_t _sosContactIndex = 0;
    uint8_t _contactSize = 0;

    // Phone App & Device Info
    int _phoneAppCode = 0;
    std::string _phoneAppVersion;
    int _phoneSdkVersion = 0;
    std::string _phoneManufacturer;
    std::string _phoneModel;

    // Singleton
    static ChronchiBle* _instance;
};
#else
class ChronchiBle {
public:
    ChronchiBle() = default;
    ~ChronchiBle() = default;
    bool begin(const char* device_name = "Chronchi") { return false; }
    void stop() {}
    void loop() {}
    bool isConnected() const { return false; }
    bool isSubscribed() const { return false; }
    void setBattery(uint8_t level, bool charging) {}
    ChronchiNavigation getNavigation() const { return ChronchiNavigation(); }
    ChronchiWeather getWeather() const { return ChronchiWeather(); }
    ChronchiLocation getLocation() const { return ChronchiLocation(); }
    ChronchiPhoneStatus getPhoneStatus() const { return ChronchiPhoneStatus(); }
    ChronchiMusic getMusic() const { return ChronchiMusic(); }
    int getNotificationCount() const { return 0; }
    ChronchiNotification getNotificationAt(int index) const { return ChronchiNotification(); }
    void clearNotifications() {}
    bool isCameraReady() const { return false; }
    ChronchiAlarm getAlarm(uint8_t index) const { return ChronchiAlarm(); }
    ChronchiReminder getSedentaryReminder() const { return ChronchiReminder(); }
    ChronchiReminder getWaterReminder() const { return ChronchiReminder(); }
    ChronchiQuietHours getQuietHours() const { return ChronchiQuietHours(); }
    bool isQuietActive() const { return false; }
    bool isRaiseToWakeEnabled() const { return false; }
    int getContactCount() const { return 0; }
    ChronchiContact getContactAt(int index) const { return ChronchiContact(); }
    ChronchiContact getSOSContact() const { return ChronchiContact(); }
    void sendCommand(const uint8_t* command, size_t length, bool force_chunked = false) {}
    void musicControl(ChronosControl command) {}
    void setVolume(uint8_t level) {}
    bool capturePhoto() { return false; }
    void findPhone(bool state) {}
    void syncRequest() {}
    void setNotifyBattery(bool state) {}
    void setConnectionCallback(ChronchiConnectionCallback cb) {}
    void setNotificationCallback(ChronchiNotificationCallback cb) {}
    void setNavigationCallback(ChronchiNavigationCallback cb) {}
    void setWeatherCallback(ChronchiWeatherCallback cb) {}
    void setLocationCallback(ChronchiLocationCallback cb) {}
    void setPhoneStatusCallback(ChronchiPhoneStatusCallback cb) {}
    void setMusicCallback(ChronchiMusicCallback cb) {}
    void setRingerCallback(ChronchiRingerCallback cb) {}
    void setWatchfaceCallback(ChronchiWatchfaceCallback cb) {}
    void setFindWatchCallback(ChronchiFindWatchCallback cb) {}
    void setCameraStateCallback(ChronchiCameraStateCallback cb) {}
    void setTimeFormatCallback(ChronchiTimeFormatCallback cb) {}
    void setAlarmCallback(ChronchiAlarmCallback cb) {}
    void setReminderCallback(ChronchiReminderCallback cb) {}
};
#endif
