#include "chronchi_ble.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <cJSON.h>
#include <sys/time.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#define TAG "chronchi_ble"

// 1. Chronos NUS 128-bit UUIDs (Little Endian)
// Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
static const uint8_t CHRONOS_NUS_SVC_UUID_BYTES[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
};
// RX: 6e400002-...
static const uint8_t CHRONOS_NUS_RX_UUID_BYTES[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
};
// TX: 6e400003-...
static const uint8_t CHRONOS_NUS_TX_UUID_BYTES[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
};

// 2. Chronchi (ESPBridge) 128-bit UUIDs (Little Endian)
// Service: 7c9e0001-6f2f-4d4d-9f25-0d7fd4f0a001
static const uint8_t CHRONCHI_SVC_UUID_BYTES[16] = {
    0x01, 0xa0, 0xf0, 0xd4, 0x7f, 0x0d, 0x25, 0x9f,
    0x4d, 0x4d, 0x2f, 0x6f, 0x01, 0x00, 0x9e, 0x7c
};
// RX: 7c9e0002-...
static const uint8_t CHRONCHI_RX_UUID_BYTES[16] = {
    0x01, 0xa0, 0xf0, 0xd4, 0x7f, 0x0d, 0x25, 0x9f,
    0x4d, 0x4d, 0x2f, 0x6f, 0x02, 0x00, 0x9e, 0x7c
};
// TX: 7c9e0003-...
static const uint8_t CHRONCHI_TX_UUID_BYTES[16] = {
    0x01, 0xa0, 0xf0, 0xd4, 0x7f, 0x0d, 0x25, 0x9f,
    0x4d, 0x4d, 0x2f, 0x6f, 0x03, 0x00, 0x9e, 0x7c
};

// ESPBridge V1 packet types
enum EspBridgePacketType : uint8_t {
    PKT_TIME_SYNC = 0x01,
    PKT_PHONE_STATUS = 0x02,
    PKT_NETWORK_STATUS = 0x03,
    PKT_WEATHER = 0x04,
    PKT_NOTIFICATION = 0x05,
    PKT_NAVIGATION = 0x06,
    PKT_LOCATION = 0x07,
    PKT_COMMAND = 0x08,
    PKT_DEVICE_STATUS = 0x09,
    PKT_ACK = 0x0A,
    PKT_SYNC_BEGIN = 0x0B,
    PKT_SYNC_END = 0x0C,
};

static constexpr uint8_t ESPBRIDGE_MAGIC = 0x45; // 'E'
static constexpr uint8_t ESPBRIDGE_VERSION = 0x01;
static constexpr int HEADER_SIZE = 8;
static constexpr size_t MAX_JSON_PAYLOAD = 768;

namespace {

const char* GetFirstString(cJSON* root, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            return item->valuestring;
        }
    }
    return "";
}

bool GetBool(cJSON* root, const char* key, bool fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

} // namespace

// Singleton
ChronchiBle* ChronchiBle::_instance = nullptr;

ChronchiBle::ChronchiBle() {
    _instance = this;
    _rx_queue = xQueueCreate(48, sizeof(QueueItem));
    _connected = false;
    _chronos_subscribed = false;
    _chronchi_subscribed = false;
    _cameraReady = false;
    _sendEspPending = false;
    _gatts_if = ESP_GATT_IF_NONE;
    _conn_id = 0;
    _batteryLevel = 100;
    _isCharging = false;
    _mtu = 23;
    _handshake_tick = 0;
    _chronchi_ready_tick = 0;
    _chronos_expected_len = 0;
    _chronos_received_len = 0;
    _notifications.reserve(MAX_NOTIFICATIONS);

    // Initialize advertising parameters early so callbacks never see zeroed memory
    memset(&_adv_params, 0, sizeof(_adv_params));
    _adv_params.adv_int_min = 0x20; // 20ms (32 * 0.625ms) - ultra fast discovery
    _adv_params.adv_int_max = 0x40; // 40ms (64 * 0.625ms)
    _adv_params.adv_type = ADV_TYPE_IND;
    _adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    _adv_params.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    memset(_adv_params.peer_addr, 0, sizeof(_adv_params.peer_addr));
    _adv_params.channel_map = ADV_CHNL_ALL;
    _adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
}

ChronchiBle::~ChronchiBle() {
    stop();
    if (_rx_queue) {
        vQueueDelete(_rx_queue);
        _rx_queue = nullptr;
    }
    _instance = nullptr;
}

bool ChronchiBle::begin(const char* device_name) {
    _device_name = (device_name && strlen(device_name) > 0) ? device_name : "Chronchi";
    ESP_LOGI(TAG, "Starting Dual BLE (Chronos + Chronchi): %s", _device_name.c_str());

    // Setup advertising parameters early (fast discovery)
    memset(&_adv_params, 0, sizeof(_adv_params));
    _adv_params.adv_int_min = 0x20; // 20ms
    _adv_params.adv_int_max = 0x40; // 40ms
    _adv_params.adv_type = ADV_TYPE_IND;
    _adv_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    _adv_params.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
    memset(_adv_params.peer_addr, 0, sizeof(_adv_params.peer_addr));
    _adv_params.channel_map = ADV_CHNL_ALL;
    _adv_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    // Init BT controller
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_bt_controller_init(&bt_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(err));
            return false;
        }
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    esp_bluedroid_status_t bt_state = esp_bluedroid_get_status();
    if (bt_state != ESP_BLUEDROID_STATUS_ENABLED) {
        esp_err_t err = esp_bluedroid_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_bluedroid_enable();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    // Register callbacks
    esp_err_t err = esp_ble_gatts_register_callback(_gattsEventHandler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ble_gap_register_callback(_gapEventHandler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(err));
        return false;
    }

    // Set local MTU up to 517 to match Chronos
    esp_ble_gatt_set_local_mtu(517);

    // Set device name before app register
    esp_ble_gap_set_device_name(_device_name.c_str());

    err = esp_ble_gatts_app_register(0x55);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Dual BLE initialized. Awaiting GATTS service creation...");
    return true;
}

void ChronchiBle::stop() {
    _connected = false;
    _chronos_subscribed = false;
    _chronchi_subscribed = false;
    _cameraReady = false;
    _sendEspPending = false;
    _handshake_tick = 0;
    _chronchi_ready_tick = 0;
    _chronos_expected_len = 0;
    _chronos_received_len = 0;

    esp_ble_gap_stop_advertising();
    if (_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_app_unregister(_gatts_if);
        _gatts_if = ESP_GATT_IF_NONE;
    }
    ESP_LOGI(TAG, "Chronchi BLE stopped");
}

void ChronchiBle::loop() {
    // 1. Process pending RX data from phone
    QueueItem item;
    while (_rx_queue && xQueueReceive(_rx_queue, &item, 0) == pdTRUE) {
        _handleRxChunk(item.data, item.len);
    }

    // Auto-advertising watchdog: keep advertising alive whenever disconnected so phone reconnects instantly
    static uint32_t s_last_adv_check = 0;
    uint32_t now_tick = xTaskGetTickCount();
    if (!_connected && (now_tick - s_last_adv_check >= pdMS_TO_TICKS(1500))) {
        s_last_adv_check = now_tick;
        if (!_advertising && _gatts_if != ESP_GATT_IF_NONE) {
            ESP_LOGI(TAG, "Watchdog: ensuring BLE advertising is active for auto-reconnect...");
            esp_ble_gap_start_advertising(&_adv_params);
        }
    }

    // 2. Scheduled Chronos handshake upon subscription
    if (_chronos_subscribed && _handshake_tick != 0 && xTaskGetTickCount() >= _handshake_tick) {
        _handshake_tick = 0;
        ESP_LOGI(TAG, "Executing Chronos handshake (info, battery, sync)...");
        _sendInfo();
        _sendBattery();
        setNotifyBattery(true);
        syncRequest();
    }

    // 3. Scheduled Chronchi initial device status upon subscription
    if (_chronchi_subscribed && _chronchi_ready_tick != 0 && xTaskGetTickCount() >= _chronchi_ready_tick) {
        _chronchi_ready_tick = 0;
        ESP_LOGI(TAG, "Sending Chronchi device status...");
        _sendDeviceStatus();
    }

    // 4. Pending ESP device info response for Chronos app
    if (_sendEspPending) {
        _sendEspPending = false;
        _sendESP();
    }
}

bool ChronchiBle::isConnected() const {
    return _connected && (_chronos_subscribed || _chronchi_subscribed);
}

bool ChronchiBle::isSubscribed() const {
    return _chronos_subscribed || _chronchi_subscribed;
}

void ChronchiBle::setBattery(uint8_t level, bool charging) {
    bool changed = (_batteryLevel != level || _isCharging != charging);
    _batteryLevel = level;
    _isCharging = charging;
    if (changed && isConnected()) {
        if (_chronos_subscribed) {
            _sendBattery();
        }
        if (_chronchi_subscribed) {
            _sendDeviceStatus();
        }
    }
}

ChronchiNavigation ChronchiBle::getNavigation() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _navigation;
}

ChronchiWeather ChronchiBle::getWeather() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _weather;
}

ChronchiLocation ChronchiBle::getLocation() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _location;
}

ChronchiPhoneStatus ChronchiBle::getPhoneStatus() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _phoneStatus;
}

ChronchiMusic ChronchiBle::getMusic() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _music;
}

int ChronchiBle::getNotificationCount() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return static_cast<int>(_notifications.size());
}

ChronchiNotification ChronchiBle::getNotificationAt(int index) const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    if (index >= 0 && index < static_cast<int>(_notifications.size())) {
        return _notifications[_notifications.size() - 1 - index];
    }
    return ChronchiNotification{};
}

void ChronchiBle::clearNotifications() {
    std::lock_guard<std::mutex> lock(_dataMutex);
    _notifications.clear();
}

bool ChronchiBle::isCameraReady() const {
    return _cameraReady;
}

ChronchiAlarm ChronchiBle::getAlarm(uint8_t index) const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    if (index < 8) return _alarms[index];
    return {};
}

ChronchiReminder ChronchiBle::getSedentaryReminder() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _sedentary;
}

ChronchiReminder ChronchiBle::getWaterReminder() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _water;
}

ChronchiQuietHours ChronchiBle::getQuietHours() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return _quietHours;
}

bool ChronchiBle::isQuietActive() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    if (!_quietHours.enabled) return false;
    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    int current_min = tm_info.tm_hour * 60 + tm_info.tm_min;
    int start_min = _quietHours.startHour * 60 + _quietHours.startMinute;
    int end_min = _quietHours.endHour * 60 + _quietHours.endMinute;
    if (start_min <= end_min) {
        return current_min >= start_min && current_min < end_min;
    } else {
        return current_min >= start_min || current_min < end_min;
    }
}

bool ChronchiBle::isRaiseToWakeEnabled() const {
    return _raiseToWake;
}

int ChronchiBle::getContactCount() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    return static_cast<int>(_contacts.size());
}

ChronchiContact ChronchiBle::getContactAt(int index) const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    if (index >= 0 && index < static_cast<int>(_contacts.size())) {
        return _contacts[index];
    }
    return {};
}

ChronchiContact ChronchiBle::getSOSContact() const {
    std::lock_guard<std::mutex> lock(_dataMutex);
    if (_sosContactIndex < _contacts.size()) {
        return _contacts[_sosContactIndex];
    }
    return {};
}

// ---- Common / Watch to Phone Controls ----

void ChronchiBle::sendCommand(const uint8_t* command, size_t length, bool force_chunked) {
    uint16_t tx_handle = _chronos_tx_handle ? _chronos_tx_handle : _chronchi_tx_handle;
    if (!_connected || tx_handle == 0 || length == 0) {
        return;
    }

    if ((length <= 20 || _mtu > 50) && !force_chunked) {
        esp_ble_gatts_send_indicate(_gatts_if, _conn_id, tx_handle, length,
                                    const_cast<uint8_t*>(command), false);
    } else {
        size_t firstChunkLen = length < 20 ? length : 20;
        esp_ble_gatts_send_indicate(_gatts_if, _conn_id, tx_handle, firstChunkLen,
                                    const_cast<uint8_t*>(command), false);
        vTaskDelay(pdMS_TO_TICKS(25));

        size_t offset = 20;
        uint8_t seq = 0;
        uint8_t chunk[20];
        while (offset < length) {
            size_t bytesToSend = length - offset;
            if (bytesToSend > 19) bytesToSend = 19;
            chunk[0] = seq++;
            memcpy(chunk + 1, command + offset, bytesToSend);
            esp_ble_gatts_send_indicate(_gatts_if, _conn_id, tx_handle, bytesToSend + 1,
                                        chunk, false);
            offset += bytesToSend;
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }
}

void ChronchiBle::musicControl(ChronosControl command) {
    if (_connected) {
        uint8_t cmdBytes[] = {
            0xAB, 0x00, 0x04, 0xFF,
            static_cast<uint8_t>(command >> 8),
            0x80,
            static_cast<uint8_t>(command & 0xFF)
        };
        sendCommand(cmdBytes, sizeof(cmdBytes));
        ESP_LOGI(TAG, "Chronos: Sent music control: 0x%04X", (int)command);

        // Also send alternative 0x99 opcode for prev/next to cover any Android Chronos APK variant
        if (command == MUSIC_PREVIOUS) {
            uint8_t altCmd[] = {0xAB, 0x00, 0x04, 0xFF, 0x99, 0x80, 0x02};
            sendCommand(altCmd, sizeof(altCmd));
        } else if (command == MUSIC_NEXT) {
            uint8_t altCmd[] = {0xAB, 0x00, 0x04, 0xFF, 0x99, 0x80, 0x03};
            sendCommand(altCmd, sizeof(altCmd));
        }
    }
    if (_connected || _chronchi_subscribed) {
        const char* action = (command == MUSIC_NEXT) ? "next" :
                             (command == MUSIC_PREVIOUS) ? "prev" :
                             (command == MUSIC_PLAY) ? "play" :
                             (command == MUSIC_PAUSE) ? "pause" : "toggle";
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"action\":\"music\",\"command\":\"%s\"}", action);
        _sendEspBridgePacket(PKT_COMMAND, buf);
        ESP_LOGI(TAG, "Chronchi: Sent music action: %s", action);
    }
}

void ChronchiBle::setVolume(uint8_t level) {
    if (_chronos_subscribed) {
        uint8_t volCmd[] = {0xAB, 0x00, 0x05, 0xFF, 0x99, 0x80, 0xA0, level};
        sendCommand(volCmd, sizeof(volCmd));
    }
    if (_chronchi_subscribed) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"action\":\"volume\",\"level\":%d}", level);
        _sendEspBridgePacket(PKT_COMMAND, buf);
    }
}

bool ChronchiBle::capturePhoto() {
    if (_chronos_subscribed) {
        uint8_t captureCmd[] = {0xAB, 0x00, 0x04, 0xFF, 0x79, 0x80, 0x01};
        sendCommand(captureCmd, sizeof(captureCmd));
    }
    if (_chronchi_subscribed) {
        _sendEspBridgePacket(PKT_COMMAND, "{\"action\":\"camera\",\"trigger\":\"capture\"}");
    }
    ESP_LOGI(TAG, "Sent capture photo trigger");
    return _cameraReady;
}

void ChronchiBle::findPhone(bool state) {
    if (_connected) {
        // Chronos / DaFit remote alert command: 0x71
        uint8_t findCmd71[] = {0xAB, 0x00, 0x04, 0xFF, 0x71, 0x80, static_cast<uint8_t>(state ? 0x01 : 0x00)};
        sendCommand(findCmd71, sizeof(findCmd71));
        // Alternate variant: 0x7D
        uint8_t findCmd7D[] = {0xAB, 0x00, 0x04, 0xFF, 0x7D, 0x80, static_cast<uint8_t>(state ? 0x01 : 0x00)};
        sendCommand(findCmd7D, sizeof(findCmd7D));
    }
    if (_chronchi_subscribed || _connected) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"action\":\"find_phone\",\"active\":%s}", state ? "true" : "false");
        _sendEspBridgePacket(PKT_COMMAND, buf);
    }
    ESP_LOGI(TAG, "Sent find phone: %d (connected=%d)", state, (int)_connected);
}

void ChronchiBle::syncRequest() {
    if (_chronos_subscribed) {
        uint8_t syncCmd[] = {0xAB, 0x00, 0x03, 0xFE, 0x23, 0x80};
        sendCommand(syncCmd, sizeof(syncCmd));
    }
    if (_chronchi_subscribed) {
        _sendEspBridgePacket(PKT_COMMAND, "{\"action\":\"sync\"}");
    }
    ESP_LOGI(TAG, "Sent sync request");
}

void ChronchiBle::setNotifyBattery(bool state) {
    if (_chronos_subscribed) {
        uint8_t batRq[] = {0xAB, 0x00, 0x04, 0xFE, 0x91, 0x80, static_cast<uint8_t>(state ? 0x01 : 0x00)};
        sendCommand(batRq, sizeof(batRq));
    }
}

// ---- Chronos Specific Outgoing Handshake ----

void ChronchiBle::_sendInfo() {
    uint8_t infoCmd[] = {
        0xAB, 0x00, 0x11, 0xFF, 0x92, 0xC0,
        0x01, 0x5B, // v1.9.1 (1, 91 = 0x5B)
        0x00, 0xFB, 0x1E, 0x40, 0xC0, 0x0E, 0x32, 0x28, 0x00, 0xE2,
        0x81, // 0x81 = Generic ESP32 240x240
        0x80
    };
    sendCommand(infoCmd, sizeof(infoCmd));
}

void ChronchiBle::_sendBattery() {
    uint8_t c = _isCharging ? 0x01 : 0x00;
    uint8_t batCmd[] = {0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80, c, _batteryLevel};
    sendCommand(batCmd, sizeof(batCmd));
}

void ChronchiBle::_sendESP() {
    std::string espInfo = "ChronosESP32 v1.9.1\nESP32-S3";
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    espInfo += " Cores:" + std::to_string(chip_info.cores);
    espInfo += " Rev:" + std::to_string(chip_info.revision);
    espInfo += "\nRAM: " + std::to_string(esp_get_free_heap_size() / 1024) + "kB";
    espInfo += "\nIDF: " + std::string(esp_get_idf_version());
    if (espInfo.length() > 500) {
        espInfo = espInfo.substr(0, 500);
    }
    uint16_t len = static_cast<uint16_t>(espInfo.length());
    std::vector<uint8_t> out(6 + len);
    out[0] = 0xAB;
    out[1] = static_cast<uint8_t>((len + 3) >> 8);
    out[2] = static_cast<uint8_t>((len + 3) & 0xFF);
    out[3] = 0xFE;
    out[4] = 0x92;
    out[5] = 0x80;
    memcpy(out.data() + 6, espInfo.data(), len);
    sendCommand(out.data(), out.size(), true);
    ESP_LOGI(TAG, "Sent ESP system info to Chronos app");
}

// ---- Chronchi (ESPBridge) Specific Outgoing Packets ----

void ChronchiBle::_sendDeviceStatus() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "battery", _batteryLevel);
    cJSON_AddBoolToObject(root, "charging", _isCharging);
    char* jsonStr = cJSON_PrintUnformatted(root);
    if (jsonStr) {
        _sendEspBridgePacket(PKT_DEVICE_STATUS, jsonStr);
        free(jsonStr);
    }
    cJSON_Delete(root);
}

void ChronchiBle::_sendEspBridgePacket(uint8_t type, const char* jsonPayload, int sequence) {
    uint16_t tx_handle = _chronchi_tx_handle ? _chronchi_tx_handle : _chronos_tx_handle;
    if (!_connected || tx_handle == 0) return;

    size_t payloadLen = strlen(jsonPayload);
    if (payloadLen > MAX_JSON_PAYLOAD) payloadLen = MAX_JSON_PAYLOAD;

    size_t maxGattPayload = (_mtu - 3) > 20 ? (_mtu - 3) : 20;
    size_t maxChunk = (maxGattPayload - HEADER_SIZE) > 8 ? (maxGattPayload - HEADER_SIZE) : 8;
    int fragmentCount = static_cast<int>((payloadLen + maxChunk - 1) / maxChunk);
    if (fragmentCount < 1) fragmentCount = 1;
    if (fragmentCount > 255) return;

    static uint8_t next_seq = 0;
    uint8_t frame_seq = (sequence >= 0) ? static_cast<uint8_t>(sequence) : static_cast<uint8_t>(++next_seq);

    for (int i = 0; i < fragmentCount; i++) {
        size_t start = i * maxChunk;
        size_t end = start + maxChunk;
        if (end > payloadLen) end = payloadLen;
        size_t partLen = end - start;

        std::vector<uint8_t> frame(HEADER_SIZE + partLen);
        frame[0] = ESPBRIDGE_MAGIC; // 0x45 ('E')
        frame[1] = ESPBRIDGE_VERSION; // 0x01
        frame[2] = type;
        frame[3] = frame_seq;
        frame[4] = static_cast<uint8_t>(i);
        frame[5] = static_cast<uint8_t>(fragmentCount);
        frame[6] = static_cast<uint8_t>((partLen >> 8) & 0xFF);
        frame[7] = static_cast<uint8_t>(partLen & 0xFF);
        memcpy(frame.data() + HEADER_SIZE, jsonPayload + start, partLen);

        esp_ble_gatts_send_indicate(_gatts_if, _conn_id, tx_handle,
                                    HEADER_SIZE + partLen, frame.data(), false);
    }
}

// ---- Multiplexed RX Chunk Handler ----

void ChronchiBle::_handleRxChunk(const uint8_t* data, uint16_t len) {
    if (len == 0) return;

    // Protocol 1: Chronchi (ESPBridge V1 JSON Protocol)
    if (data[0] == ESPBRIDGE_MAGIC && data[1] == ESPBRIDGE_VERSION && len >= HEADER_SIZE) {
        uint8_t type = data[2];
        uint8_t seq = data[3];
        uint8_t frag_idx = data[4];
        uint8_t frag_cnt = data[5];
        uint16_t partLen = (static_cast<uint16_t>(data[6]) << 8) | data[7];

        if (frag_idx == 0) {
            _espbridge_reassembly.Reset();
            _espbridge_reassembly.type = type;
            _espbridge_reassembly.sequence = seq;
            _espbridge_reassembly.fragment_count = frag_cnt;
        }

        if (len >= HEADER_SIZE + partLen) {
            _espbridge_reassembly.payload.insert(_espbridge_reassembly.payload.end(),
                                                data + HEADER_SIZE, data + HEADER_SIZE + partLen);
        }

        if (frag_idx + 1 >= frag_cnt) {
            _espbridge_reassembly.payload.push_back('\0');
            _processEspBridgePacket(_espbridge_reassembly.type,
                                    reinterpret_cast<const char*>(_espbridge_reassembly.payload.data()),
                                    _espbridge_reassembly.sequence);
            _espbridge_reassembly.Reset();
        }
        return;
    }

    // Protocol 2: Chronos NUS Binary Protocol Header
    if ((data[0] == 0xAB || data[0] == 0xEA) && len >= 4) {
        _chronos_expected_len = ((static_cast<uint16_t>(data[1]) << 8) | data[2]) + 3;
        if (_chronos_expected_len > sizeof(_chronos_incoming_buf)) {
            _chronos_expected_len = sizeof(_chronos_incoming_buf);
        }
        uint16_t copy_len = len > _chronos_expected_len ? _chronos_expected_len : len;
        memcpy(_chronos_incoming_buf, data, copy_len);
        _chronos_received_len = copy_len;

        if (_chronos_received_len >= _chronos_expected_len) {
            _processChronosPacket(_chronos_incoming_buf, _chronos_expected_len);
            _chronos_expected_len = 0;
            _chronos_received_len = 0;
        }
        return;
    }

    // Protocol 2: Chronos NUS Subsequent Fragment
    if (_chronos_expected_len > 0) {
        int offset = 20 + (static_cast<int>(data[0]) * 19);
        int payload_len = len - 1;
        if (offset + payload_len <= static_cast<int>(sizeof(_chronos_incoming_buf))) {
            memcpy(_chronos_incoming_buf + offset, data + 1, payload_len);
            if (offset + payload_len > _chronos_received_len) {
                _chronos_received_len = offset + payload_len;
            }
            if (_chronos_received_len >= _chronos_expected_len) {
                _processChronosPacket(_chronos_incoming_buf, _chronos_expected_len);
                _chronos_expected_len = 0;
                _chronos_received_len = 0;
            }
        } else {
            _chronos_expected_len = 0;
            _chronos_received_len = 0;
        }
        return;
    }
}

// ---- Chronchi (ESPBridge JSON) Processing ----

void ChronchiBle::_processEspBridgePacket(uint8_t type, const char* json, uint8_t sequence) {
    ESP_LOGI(TAG, "Chronchi ESPBridge packet: type=0x%02X seq=%d", type, sequence);
    switch (type) {
        case PKT_TIME_SYNC: _handleTimeSync(json); break;
        case PKT_PHONE_STATUS: _handlePhoneStatus(json); break;
        case PKT_NETWORK_STATUS: _handleNetworkStatus(json); break;
        case PKT_WEATHER: _handleWeather(json); break;
        case PKT_NOTIFICATION: _handleNotification(json); break;
        case PKT_NAVIGATION: _handleNavigation(json); break;
        case PKT_LOCATION: _handleLocation(json); break;
        case PKT_COMMAND: {
            cJSON* root = cJSON_Parse(json);
            if (root) {
                std::string action = GetFirstString(root, {"action", "cmd"});
                if (action == "status") {
                    _sendDeviceStatus();
                } else if (action == "theme" || action == "watchface") {
                    cJSON* lay_item = cJSON_GetObjectItem(root, "layout");
                    cJSON* col_item = cJSON_GetObjectItem(root, "color");
                    uint32_t col = (col_item && cJSON_IsNumber(col_item)) ? col_item->valueint : 0;
                    uint32_t lay = (lay_item && cJSON_IsNumber(lay_item)) ? lay_item->valueint : 0;
                    if (_watchfaceCb) _watchfaceCb(col, lay);
                }
                cJSON_Delete(root);
            }
            break;
        }
        case PKT_SYNC_BEGIN:
        case PKT_SYNC_END:
            break;
        default:
            break;
    }

    char ackBuf[64];
    snprintf(ackBuf, sizeof(ackBuf), "{\"status\":\"ok\",\"ack\":%d}", sequence);
    _sendEspBridgePacket(PKT_ACK, ackBuf, sequence);
}

void ChronchiBle::_handleNotification(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    ChronchiNotification notif;
    notif.app = GetFirstString(root, {"app", "package", "source"});
    notif.title = GetFirstString(root, {"title", "header"});
    notif.message = GetFirstString(root, {"message", "text", "body"});
    notif.time = GetFirstString(root, {"time", "timestamp"});
    cJSON* icon_item = cJSON_GetObjectItem(root, "icon");
    if (cJSON_IsNumber(icon_item)) {
        notif.icon = icon_item->valueint;
    }
    if (notif.time.empty()) {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
        notif.time = buf;
    }

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        if (_notifications.size() >= MAX_NOTIFICATIONS) {
            _notifications.erase(_notifications.begin());
        }
        _notifications.push_back(notif);
    }

    if (_notifCb) _notifCb(notif);
    cJSON_Delete(root);
}

void ChronchiBle::_handleNavigation(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        _navigation.active = GetBool(root, "active", false);
        _navigation.title = GetFirstString(root, {"title", "destination"});
        _navigation.directions = GetFirstString(root, {"directions", "instruction", "maneuver_text"});
        _navigation.distance = GetFirstString(root, {"distance", "dist"});
        _navigation.duration = GetFirstString(root, {"duration", "time"});
        _navigation.eta = GetFirstString(root, {"eta", "arrival_time"});
        _navigation.speed = GetFirstString(root, {"speed"});
        _navigation.maneuver = GetFirstString(root, {"maneuver", "icon"});

        cJSON* icon_arr = cJSON_GetObjectItem(root, "icon_data");
        if (cJSON_IsArray(icon_arr)) {
            int sz = cJSON_GetArraySize(icon_arr);
            if (sz > 288) sz = 288;
            for (int i = 0; i < sz; i++) {
                cJSON* val = cJSON_GetArrayItem(icon_arr, i);
                if (cJSON_IsNumber(val)) _navigation.icon[i] = val->valueint;
            }
            _navigation.hasIcon = (sz >= 288);
        }
    }

    if (_navCb) _navCb(_navigation);
    cJSON_Delete(root);
}

void ChronchiBle::_handleTimeSync(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* time_item = cJSON_GetObjectItem(root, "epoch");
    if (!time_item) time_item = cJSON_GetObjectItem(root, "time");
    if (cJSON_IsNumber(time_item)) {
        time_t epoch = static_cast<time_t>(time_item->valuedouble);
        if (epoch > 100000000000ULL) epoch /= 1000;
        struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
        settimeofday(&tv, nullptr);
    } else {
        cJSON* y = cJSON_GetObjectItem(root, "year");
        if (y && y->valueint >= 2020) {
            cJSON* m = cJSON_GetObjectItem(root, "month");
            cJSON* d = cJSON_GetObjectItem(root, "day");
            cJSON* h = cJSON_GetObjectItem(root, "hour");
            cJSON* min = cJSON_GetObjectItem(root, "minute");
            cJSON* s = cJSON_GetObjectItem(root, "second");

            struct tm tm_info = {};
            tm_info.tm_year = y->valueint - 1900;
            tm_info.tm_mon = (m ? m->valueint : 1) - 1;
            tm_info.tm_mday = d ? d->valueint : 1;
            tm_info.tm_hour = h ? h->valueint : 0;
            tm_info.tm_min = min ? min->valueint : 0;
            tm_info.tm_sec = s ? s->valueint : 0;
            time_t t = mktime(&tm_info);
            struct timeval tv = {.tv_sec = t, .tv_usec = 0};
            settimeofday(&tv, nullptr);
        }
    }
    cJSON_Delete(root);
}

void ChronchiBle::_handlePhoneStatus(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        cJSON* bat = cJSON_GetObjectItem(root, "battery");
        if (!bat) bat = cJSON_GetObjectItem(root, "level");
        if (cJSON_IsNumber(bat)) _phoneStatus.batteryLevel = bat->valueint;
        _phoneStatus.charging = GetBool(root, "charging", false);
        _phoneStatus.bluetoothEnabled = GetBool(root, "bluetooth", true);
    }

    if (_phoneCb) _phoneCb(_phoneStatus);
    cJSON_Delete(root);
}

void ChronchiBle::_handleNetworkStatus(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        _phoneStatus.network.transport = GetFirstString(root, {"transport", "type"});
        _phoneStatus.network.generation = GetFirstString(root, {"generation", "gen"});
        cJSON* sig = cJSON_GetObjectItem(root, "signal");
        if (!sig) sig = cJSON_GetObjectItem(root, "level");
        if (cJSON_IsNumber(sig)) _phoneStatus.network.signalLevel = sig->valueint;
    }

    if (_phoneCb) _phoneCb(_phoneStatus);
    cJSON_Delete(root);
}

void ChronchiBle::_handleWeather(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        _weather.valid = true;
        cJSON* temp = cJSON_GetObjectItem(root, "temp");
        if (!temp) temp = cJSON_GetObjectItem(root, "temperature");
        if (cJSON_IsNumber(temp)) _weather.temperatureC = static_cast<float>(temp->valuedouble);
        _weather.condition = GetFirstString(root, {"condition", "weather", "desc"});
        cJSON* hum = cJSON_GetObjectItem(root, "humidity");
        if (cJSON_IsNumber(hum)) _weather.humidity = hum->valueint;
        _weather.location = GetFirstString(root, {"location", "city"});
    }

    if (_weatherCb) _weatherCb(_weather);
    cJSON_Delete(root);
}

void ChronchiBle::_handleLocation(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    {
        std::lock_guard<std::mutex> lock(_dataMutex);
        _location.valid = true;
        cJSON* lat = cJSON_GetObjectItem(root, "lat");
        if (!lat) lat = cJSON_GetObjectItem(root, "latitude");
        if (cJSON_IsNumber(lat)) _location.latitude = lat->valuedouble;
        cJSON* lon = cJSON_GetObjectItem(root, "lon");
        if (!lon) lon = cJSON_GetObjectItem(root, "longitude");
        if (cJSON_IsNumber(lon)) _location.longitude = lon->valuedouble;
        _location.city = GetFirstString(root, {"city", "location"});
        _location.country = GetFirstString(root, {"country"});
        if (!_location.city.empty()) _weather.location = _location.city;
    }

    if (_locCb) _locCb(_location);
    if (_weatherCb) _weatherCb(_weather);
    cJSON_Delete(root);
}

// ---- Chronos NUS Binary Processing ----

void ChronchiBle::_processChronosPacket(const uint8_t* data, uint16_t len) {
    if (len < 5) return;

    if (data[0] == 0xAB) {
        uint8_t cmd = data[4];
        switch (cmd) {
            case 0x51: { // Step records request
                ESP_LOGI(TAG, "Chronos Steps sync requested, responding...");
                time_t now = time(nullptr);
                struct tm tm_info;
                localtime_r(&now, &tm_info);
                uint32_t steps = (esp_timer_get_time() / 1000000ULL / 30) % 15000 + 120;
                uint16_t cal = steps / 25;
                uint16_t dist = (steps * 7) / 100;
                uint8_t stepPkt[] = {
                    0xAB, 0x00, 0x0E, 0xFF, 0x51, 0x80,
                    static_cast<uint8_t>(tm_info.tm_year + 1900 - 2000),
                    static_cast<uint8_t>(tm_info.tm_mon + 1),
                    static_cast<uint8_t>(tm_info.tm_mday),
                    static_cast<uint8_t>((steps >> 16) & 0xFF),
                    static_cast<uint8_t>((steps >> 8) & 0xFF),
                    static_cast<uint8_t>(steps & 0xFF),
                    static_cast<uint8_t>((cal >> 8) & 0xFF),
                    static_cast<uint8_t>(cal & 0xFF),
                    static_cast<uint8_t>((dist >> 8) & 0xFF),
                    static_cast<uint8_t>(dist & 0xFF)
                };
                sendCommand(stepPkt, sizeof(stepPkt));
                break;
            }

            case 0x53: { // Water reminder
                if (len >= 12) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _water.enabled = (data[6] == 1);
                    _water.startHour = data[7];
                    _water.startMinute = data[8];
                    _water.endHour = data[9];
                    _water.endMinute = data[10];
                    _water.intervalMinutes = data[11];
                    ESP_LOGI(TAG, "Chronos Water reminder: en=%d %02d:%02d-%02d:%02d int=%d min",
                             _water.enabled, _water.startHour, _water.startMinute,
                             _water.endHour, _water.endMinute, _water.intervalMinutes);
                }
                if (_reminderCb) _reminderCb("water", _water);
                break;
            }

            case 0x71: { // Find Watch alert
                bool active = (len > 6) ? (data[6] == 1) : true;
                ESP_LOGI(TAG, "Chronos command: Find Watch alert (active=%d)", active);
                if (_findWatchCb) _findWatchCb(active);
                break;
            }

            case 0x72: { // Notification or Phone Call Alert
                int icon = data[6];
                int state = data[7];
                std::string msg(reinterpret_cast<const char*>(&data[8]), len > 8 ? len - 8 : 0);

                ESP_LOGI(TAG, "Chronos 0x72 packet: icon=0x%02X, state=0x%02X, len=%d, msg='%s'",
                         icon, state, len, msg.c_str());

                if (icon == 0x01) {
                    ESP_LOGI(TAG, "Chronos incoming call ringer: %s", msg.c_str());
                    if (_ringerCb) _ringerCb(msg, true);
                } else if (icon == 0x02) {
                    ESP_LOGI(TAG, "Chronos call ringer stopped");
                    if (_ringerCb) _ringerCb(msg, false);
                } else if (state == 0x03) {
                    ESP_LOGI(TAG, "Chronos notification dismissed on phone");
                } else {
                    // All other states (0x01 = ACTION_NEW, 0x02 = ACTION_UPDATE, 0x00, etc.) are NOTIFICATIONS!
                    ChronchiNotification notif;
                    notif.icon = icon;
                    notif.app = _appName(icon);

                    time_t now = time(nullptr);
                    struct tm tm_info;
                    localtime_r(&now, &tm_info);
                    char tbuf[16];
                    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
                    notif.time = tbuf;

                    _splitTitle(msg, notif.title, notif.message, icon);
                    ESP_LOGI(TAG, "Dispatching Chronos Notification: [%s] %s: %s",
                             notif.app.c_str(), notif.title.c_str(), notif.message.c_str());

                    {
                        std::lock_guard<std::mutex> lock(_dataMutex);
                        if (_notifications.size() >= MAX_NOTIFICATIONS) {
                            _notifications.erase(_notifications.begin());
                        }
                        _notifications.push_back(notif);
                    }

                    if (_notifCb) _notifCb(notif);
                }
                break;
            }

            case 0x73: { // Alarms (Chronos: data[6]=idx, data[7]=en, data[8]=hr, data[9]=min, data[10]=rep)
                if (len >= 11) {
                    uint8_t idx = data[6] % 8;
                    ChronchiAlarm alarm;
                    alarm.enabled = (data[7] == 1);
                    alarm.hour = data[8];
                    alarm.minute = data[9];
                    alarm.repeat = data[10];
                    {
                        std::lock_guard<std::mutex> lock(_dataMutex);
                        _alarms[idx] = alarm;
                    }
                    ESP_LOGI(TAG, "Chronos Alarm #%d: %02d:%02d rep=0x%02X en=%d",
                             idx, alarm.hour, alarm.minute, alarm.repeat, alarm.enabled);
                    if (_alarmCb) _alarmCb(idx, alarm);
                }
                break;
            }

            case 0x74: { // User profile
                if (len >= 12) {
                    uint8_t age = data[7];
                    uint8_t height = data[8];
                    uint8_t weight = data[9];
                    uint32_t target = static_cast<uint32_t>(data[11]) * 1000;
                    ESP_LOGI(TAG, "Chronos User Profile: age=%d height=%dcm weight=%dkg target=%lu steps",
                             age, height, weight, (unsigned long)target);
                }
                break;
            }

            case 0x75: { // Sedentary reminder
                if (len >= 12) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _sedentary.enabled = (data[6] == 1);
                    _sedentary.startHour = data[7];
                    _sedentary.startMinute = data[8];
                    _sedentary.endHour = data[9];
                    _sedentary.endMinute = data[10];
                    _sedentary.intervalMinutes = data[11];
                    ESP_LOGI(TAG, "Chronos Sedentary reminder: en=%d %02d:%02d-%02d:%02d int=%d min",
                             _sedentary.enabled, _sedentary.startHour, _sedentary.startMinute,
                             _sedentary.endHour, _sedentary.endMinute, _sedentary.intervalMinutes);
                }
                if (_reminderCb) _reminderCb("sedentary", _sedentary);
                break;
            }

            case 0x76: { // Sleep / Quiet hours / DND
                if (len >= 11) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _quietHours.enabled = (data[6] == 1);
                    _quietHours.startHour = data[7];
                    _quietHours.startMinute = data[8];
                    _quietHours.endHour = data[9];
                    _quietHours.endMinute = data[10];
                    ESP_LOGI(TAG, "Chronos Quiet hours: en=%d %02d:%02d-%02d:%02d",
                             _quietHours.enabled, _quietHours.startHour, _quietHours.startMinute,
                             _quietHours.endHour, _quietHours.endMinute);
                }
                break;
            }

            case 0x77: { // Raise to wake
                _raiseToWake = (len > 6) ? (data[6] == 1) : false;
                ESP_LOGI(TAG, "Chronos Raise to wake: %d", _raiseToWake);
                break;
            }

            case 0x78: { // Hourly measurement / reminder
                ESP_LOGI(TAG, "Chronos Hourly measurement: %d", (len > 6) ? (int)data[6] : 0);
                break;
            }

            case 0x79: { // Camera ready
                _cameraReady = (data[6] == 1);
                ESP_LOGI(TAG, "Chronos camera ready: %d", _cameraReady);
                if (_cameraStateCb) _cameraStateCb(_cameraReady);
                break;
            }

            case 0x7B: { // Language
                if (len > 6) {
                    _languageIndex = data[6];
                    ESP_LOGI(TAG, "Chronos Language index: %d", _languageIndex);
                }
                break;
            }

            case 0x7C: { // Time format (0 = 24-hour, 1 = 12-hour)
                bool is_24h = (len > 6) ? (data[6] == 0) : true;
                ESP_LOGI(TAG, "Chronos Time format: %s", is_24h ? "24-hour" : "12-hour");
                if (_timeFormatCb) _timeFormatCb(is_24h);
                break;
            }

            case 0x7E: { // Weather
                int entries = (len - 6) / 2;
                if (entries > 0) {
                    int icon = data[6] >> 4;
                    int sign = (data[6] & 1) ? -1 : 1;
                    int temp = static_cast<int>(data[7]) * sign;

                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _weather.valid = true;
                    _weather.temperatureC = static_cast<float>(temp);
                    _weather.condition = _weatherCondition(icon);
                    ESP_LOGI(TAG, "Weather: %.0f°C, condition: %s", _weather.temperatureC, _weather.condition.c_str());
                    if (_weatherCb) _weatherCb(_weather);
                }
                break;
            }

            case 0x88: { // Weather High / Low
                int signH = (data[6] >> 7 & 1) ? -1 : 1;
                int signL = (data[7] >> 7 & 1) ? -1 : 1;
                std::lock_guard<std::mutex> lock(_dataMutex);
                _weather.highC = static_cast<int>(data[6] & 0x7F) * signH;
                _weather.lowC = static_cast<int>(data[7] & 0x7F) * signL;
                if (_weatherCb) _weatherCb(_weather);
                break;
            }

            case 0x8A: { // UV and Pressure
                std::lock_guard<std::mutex> lock(_dataMutex);
                _weather.uv = data[6];
                _weather.pressure = (static_cast<int>(data[7]) << 8) | data[8];
                if (_weatherCb) _weatherCb(_weather);
                break;
            }

            case 0x91: { // Phone battery
                if (data[3] == 0xFE && len >= 8) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _phoneStatus.charging = (data[6] == 1);
                    _phoneStatus.batteryLevel = data[7];
                    ESP_LOGI(TAG, "Phone battery: %d%%, charging: %d", _phoneStatus.batteryLevel, _phoneStatus.charging);
                    if (_phoneCb) _phoneCb(_phoneStatus);
                }
                break;
            }

            case 0x93: { // Time sync
                if (len >= 14) {
                    int year = (static_cast<int>(data[7]) << 8) | data[8];
                    int month = data[9];
                    int day = data[10];
                    int hour = data[11];
                    int min = data[12];
                    int sec = data[13];

                    struct tm tm_info = {};
                    tm_info.tm_year = year - 1900;
                    tm_info.tm_mon = month - 1;
                    tm_info.tm_mday = day;
                    tm_info.tm_hour = hour;
                    tm_info.tm_min = min;
                    tm_info.tm_sec = sec;
                    time_t t = mktime(&tm_info);
                    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
                    settimeofday(&tv, nullptr);

                    ESP_LOGI(TAG, "Chronos Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                             year, month, day, hour, min, sec);
                }
                break;
            }

            case 0x9C: { // Watchface custom settings (Font color & layout)
                ESP_LOG_BUFFER_HEX(TAG, data, len);
                if (len >= 10) {
                    uint32_t color = (static_cast<uint32_t>(data[5]) << 16) | (static_cast<uint32_t>(data[6]) << 8) | static_cast<uint32_t>(data[7]);
                    uint32_t select = (static_cast<uint32_t>(data[8]) << 16) | static_cast<uint32_t>(data[9]);
                    ESP_LOGI(TAG, "Chronos Watchface config: color=0x%06lX, layout=%lu", (unsigned long)color, (unsigned long)select);
                    if (_watchfaceCb) _watchfaceCb(color, select);
                }
                break;
            }

            case 0x9D: { // Music playback info
                if (data[3] == 0xFE && len >= 7) {
                    uint8_t sub = data[5];
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    if (sub == 0x80) {
                        _music.state = data[6]; // 0 = pause, 1 = play
                        int i = 13;
                        _music.app.clear();
                        while (i < len && data[i] != 0) {
                            _music.app += static_cast<char>(data[i++]);
                        }
                    } else if (sub == 0x81) {
                        _music.title.clear();
                        int i = 7;
                        while (i < len && data[i] != 0) {
                            _music.title += static_cast<char>(data[i++]);
                        }
                    } else if (sub == 0x82) {
                        _music.artist.clear();
                        int i = 7;
                        while (i < len && data[i] != 0) {
                            _music.artist += static_cast<char>(data[i++]);
                        }
                    }
                    ESP_LOGI(TAG, "Music: state=%d '%s' by '%s' (%s)",
                             _music.state, _music.title.c_str(), _music.artist.c_str(), _music.app.c_str());
                    if (_musicCb) _musicCb(_music);
                }
                break;
            }

            case 0xA2: { // Contact Name
                if (len >= 6) {
                    int pos = data[5] % 32;
                    std::string name(reinterpret_cast<const char*>(&data[6]), len > 6 ? len - 6 : 0);
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    if (pos >= static_cast<int>(_contacts.size())) _contacts.resize(pos + 1);
                    _contacts[pos].name = name;
                    ESP_LOGI(TAG, "Chronos Contact #%d Name: %s", pos, name.c_str());
                }
                break;
            }

            case 0xA3: { // Contact Number
                if (len >= 7) {
                    int pos = data[5] % 32;
                    int nSize = data[6];
                    std::string num;
                    for (int i = 7; i < len; i++) {
                        char d[3];
                        snprintf(d, sizeof(d), "%02X", data[i]);
                        char tmp = d[0]; d[0] = d[1]; d[1] = tmp;
                        num += d;
                    }
                    for (char& c : num) { if (c == 'A') c = '+'; }
                    if (num.length() > static_cast<size_t>(nSize)) num = num.substr(0, nSize);
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    if (pos >= static_cast<int>(_contacts.size())) _contacts.resize(pos + 1);
                    _contacts[pos].number = num;
                    ESP_LOGI(TAG, "Chronos Contact #%d Number: %s", pos, num.c_str());
                }
                break;
            }

            case 0xA5: { // SOS contact & total count
                if (len >= 8) {
                    _sosContactIndex = data[6];
                    _contactSize = data[7];
                    ESP_LOGI(TAG, "Chronos SOS Contact index: %d, total: %d", _sosContactIndex, _contactSize);
                }
                break;
            }

            case 0xCA: { // App info & trigger ESP info response
                if (data[3] == 0xFE && len >= 8) {
                    _phoneAppCode = (static_cast<int>(data[6]) << 8) | data[7];
                    _phoneAppVersion.assign(reinterpret_cast<const char*>(&data[8]), len > 8 ? len - 8 : 0);
                    _sendEspPending = true;
                    ESP_LOGI(TAG, "Chronos App: code=%d ver=%s", _phoneAppCode, _phoneAppVersion.c_str());
                } else if (data[3] == 0xFE) {
                    _sendEspPending = true;
                }
                break;
            }

            case 0xCB: { // Phone device & SDK info
                if (data[3] == 0xFE && len >= 8) {
                    _phoneSdkVersion = (static_cast<int>(data[6]) << 8) | data[7];
                    int i = 8;
                    _phoneManufacturer.clear();
                    while (i < len && data[i] != 0) _phoneManufacturer += static_cast<char>(data[i++]);
                    if (i < len && data[i] == 0) i++;
                    _phoneModel.clear();
                    while (i < len && data[i] != 0) _phoneModel += static_cast<char>(data[i++]);
                    ESP_LOGI(TAG, "Phone Device: SDK=%d %s %s", _phoneSdkVersion, _phoneManufacturer.c_str(), _phoneModel.c_str());
                }
                break;
            }

            case 0xEE: { // Navigation icon chunk (48x48 1bpp icon in 3 chunks of 96 bytes)
                if (data[3] == 0xFE && len >= 11) {
                    uint8_t pos = data[6];
                    uint32_t crc = (static_cast<uint32_t>(data[7]) << 24) |
                                   (static_cast<uint32_t>(data[8]) << 16) |
                                   (static_cast<uint32_t>(data[9]) << 8) |
                                   static_cast<uint32_t>(data[10]);

                    if (pos < 3) {
                        std::lock_guard<std::mutex> lock(_dataMutex);
                        for (int i = 0; i < 96 && (11 + i) < len; i++) {
                            _navigation.icon[i + (96 * pos)] = data[11 + i];
                        }
                        if (pos == 2) {
                            _navigation.hasIcon = true;
                            _navigation.iconCRC = crc;
                            ESP_LOGI(TAG, "Navigation full icon received (288 bytes), CRC=0x%08X", (unsigned int)crc);
                            if (_navCb) _navCb(_navigation);
                        }
                    }
                }
                break;
            }

            case 0xEF: { // Navigation state and text instructions
                if (data[3] == 0xFE && len >= 6) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    if (data[5] == 0x00) {
                        _navigation.active = false;
                        _navigation.hasIcon = false;
                        _navigation.title = "Chronos";
                        _navigation.directions = "Navigation inactive";
                        _navigation.distance.clear();
                        _navigation.duration.clear();
                        _navigation.eta.clear();
                        _navigation.speed.clear();
                        ESP_LOGI(TAG, "Navigation stopped");
                    } else if (data[5] == 0x80) {
                        _navigation.active = true;
                        _navigation.hasIcon = (data[6] == 1);
                        _navigation.isNavigation = (data[7] == 1);
                        _navigation.iconCRC = (static_cast<uint32_t>(data[8]) << 24) |
                                              (static_cast<uint32_t>(data[9]) << 16) |
                                              (static_cast<uint32_t>(data[10]) << 8) |
                                              static_cast<uint32_t>(data[11]);

                        int idx = 12;
                        auto readStr = [&](std::string& out) {
                            out.clear();
                            while (idx < len && data[idx] != 0) {
                                out += static_cast<char>(data[idx++]);
                            }
                            if (idx < len && data[idx] == 0) idx++;
                        };
                        readStr(_navigation.title);
                        readStr(_navigation.duration);
                        readStr(_navigation.distance);
                        readStr(_navigation.eta);
                        readStr(_navigation.directions);
                        readStr(_navigation.speed);

                        ESP_LOGI(TAG, "Navigation: %s | %s | %s | %s",
                                 _navigation.title.c_str(), _navigation.directions.c_str(),
                                 _navigation.distance.c_str(), _navigation.eta.c_str());
                    }
                    if (_navCb) _navCb(_navigation);
                }
                break;
            }

            default:
                break;
        }
    } else if (data[0] == 0xEA) {
        uint8_t cmd = data[4];
        switch (cmd) {
            case 0x7E: { // Weather city name or hourly forecast
                if (data[5] == 0x01) {
                    std::string city(reinterpret_cast<const char*>(&data[7]), len > 7 ? len - 7 : 0);
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _weather.location = city;
                    ESP_LOGI(TAG, "Weather city: %s", city.c_str());
                    if (_weatherCb) _weatherCb(_weather);
                } else if (data[5] == 0x02 && len >= 14) {
                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _weather.humidity = data[12];
                    if (_weatherCb) _weatherCb(_weather);
                }
                break;
            }

            case 0x7F: { // Weather location & GPS coordinates
                if (data[3] == 0xFE && len >= 15) {
                    uint8_t payloadLen = data[6];
                    const uint8_t* payload = &data[7];

                    float latitude = 0.0f, longitude = 0.0f;
                    memcpy(&latitude, payload, 4);
                    memcpy(&longitude, payload + 4, 4);

                    int idx = 8;
                    std::string city, region, country;
                    while (idx < payloadLen && payload[idx] != 0) city += static_cast<char>(payload[idx++]);
                    idx++;
                    while (idx < payloadLen && payload[idx] != 0) region += static_cast<char>(payload[idx++]);
                    idx++;
                    while (idx < payloadLen) country += static_cast<char>(payload[idx++]);

                    std::lock_guard<std::mutex> lock(_dataMutex);
                    _location.valid = true;
                    _location.latitude = latitude;
                    _location.longitude = longitude;
                    _location.city = city;
                    _location.country = country;
                    if (!city.empty()) {
                        _weather.location = city;
                    }

                    ESP_LOGI(TAG, "Location: %s, %s (%.4f, %.4f)",
                             city.c_str(), country.c_str(), latitude, longitude);
                    if (_locCb) _locCb(_location);
                    if (_weatherCb) _weatherCb(_weather);
                }
                break;
            }

            default:
                break;
        }
    }
}

void ChronchiBle::_splitTitle(const std::string& input, std::string& title, std::string& message, int icon) {
    if (input.find("Google Maps") != std::string::npos || input.find("Maps") == 0) {
        title = "Google Maps";
        message = input;
        return;
    }

    size_t colonIndex = input.find(':');
    size_t tabIndex = input.find('\t');
    size_t sepIndex = std::string::npos;
    if (tabIndex != std::string::npos && tabIndex < 40) {
        sepIndex = tabIndex;
    } else if (colonIndex != std::string::npos && colonIndex < 40) {
        sepIndex = colonIndex;
    }
    size_t newlineIndex = input.find('\n');

    if (sepIndex != std::string::npos &&
        (newlineIndex == std::string::npos || newlineIndex > sepIndex)) {
        title = input.substr(0, sepIndex);
        message = input.substr(sepIndex + 1);
        if (!message.empty() && message[0] == ' ') {
            message.erase(0, 1);
        }
    } else {
        title = _appName(icon);
        message = input;
    }
    if (title.empty()) title = _appName(icon);
    if (message.empty()) message = "(Notifikasi)";
}

const char* ChronchiBle::_appName(int id) {
    switch (id) {
        case 0x03: return "SMS";
        case 0x04: return "Mail";
        case 0x07: return "Tencent";
        case 0x08: return "Skype";
        case 0x09: return "WeChat";
        case 0x0A: return "WhatsApp";
        case 0x0B: return "Gmail";
        case 0x0E: return "Line";
        case 0x0F: return "Twitter/X";
        case 0x10: return "Facebook";
        case 0x11: return "Messenger";
        case 0x12: return "Instagram";
        case 0x13: return "Weibo";
        case 0x14: return "KakaoTalk";
        case 0x16: return "Viber";
        case 0x17: return "VK";
        case 0x18: return "Telegram";
        case 0x1B: return "DingTalk";
        case 0x20: return "WhatsApp Business";
        case 0x22: return "WearFit Pro";
        case 0x24:
        case 0x25: return "Google Maps";
        case 0xC0: return "Chronos";
        default: return "Message";
    }
}

const char* ChronchiBle::_weatherCondition(int icon) {
    switch (icon) {
        case 0: return "Partly Cloudy";
        case 1: return "Sunny";
        case 2: return "Snow";
        case 3: return "Rainy";
        case 4: return "Cloudy";
        case 5: return "Extreme";
        case 6: return "Windy";
        case 7: return "Haze/Fog";
        default: return "Clear";
    }
}

// ---- BLE GATTS & GAP Event Handlers ----

void ChronchiBle::_gattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                     esp_ble_gatts_cb_param_t* param) {
    if (_instance) {
        _instance->_onGattsEvent(event, gatts_if, param);
    }
}

void ChronchiBle::_gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (_instance) {
        _instance->_onGapEvent(event, param);
    }
}

void ChronchiBle::_onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            _adv_config_done &= ~(1 << 0);
            if (_adv_config_done == 0) {
                esp_ble_gap_start_advertising(&_adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            _adv_config_done &= ~(1 << 1);
            if (_adv_config_done == 0) {
                esp_ble_gap_start_advertising(&_adv_params);
            }
            break;
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: {
            esp_ble_gap_start_advertising(&_adv_params);
            break;
        }
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                _advertising = false;
                ESP_LOGE(TAG, "Advertising start failed: 0x%X", param->adv_start_cmpl.status);
            } else {
                _advertising = true;
                ESP_LOGI(TAG, "Dual Advertising started successfully (ready for auto-reconnect)");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            _advertising = false;
            break;
        default:
            break;
    }
}

void ChronchiBle::_onGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            _gatts_if = gatts_if;
            ESP_LOGI(TAG, "GATTS registered, creating Service 1: Chronos NUS...");

            esp_gatt_srvc_id_t svc1_id = {};
            svc1_id.is_primary = true;
            svc1_id.id.inst_id = 0x00;
            svc1_id.id.uuid.len = ESP_UUID_LEN_128;
            memcpy(svc1_id.id.uuid.uuid.uuid128, CHRONOS_NUS_SVC_UUID_BYTES, 16);
            esp_ble_gatts_create_service(gatts_if, &svc1_id, 10);
            break;
        }

        case ESP_GATTS_CREATE_EVT: {
            uint16_t handle = param->create.service_handle;

            if (_chronos_svc_handle == 0) {
                _chronos_svc_handle = handle;
                ESP_LOGI(TAG, "Service 1 (Chronos NUS) created, handle=%d. Adding TX char...", _chronos_svc_handle);

                // Add Chronos TX char (Watch -> Phone, Notify)
                esp_bt_uuid_t tx_uuid = {};
                tx_uuid.len = ESP_UUID_LEN_128;
                memcpy(tx_uuid.uuid.uuid128, CHRONOS_NUS_TX_UUID_BYTES, 16);

                esp_attr_value_t tx_val = {};
                tx_val.attr_max_len = 512;
                tx_val.attr_len = 0;
                tx_val.attr_value = nullptr;

                esp_attr_control_t tx_ctrl = {};
                tx_ctrl.auto_rsp = ESP_GATT_RSP_BY_APP;

                esp_ble_gatts_add_char(_chronos_svc_handle, &tx_uuid, ESP_GATT_PERM_READ,
                                       static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_NOTIFY),
                                       &tx_val, &tx_ctrl);
            } else if (_chronchi_svc_handle == 0) {
                _chronchi_svc_handle = handle;
                ESP_LOGI(TAG, "Service 2 (Chronchi ESPBridge) created, handle=%d. Adding TX char...", _chronchi_svc_handle);

                // Add Chronchi TX char (Watch -> Phone, Notify)
                esp_bt_uuid_t tx_uuid = {};
                tx_uuid.len = ESP_UUID_LEN_128;
                memcpy(tx_uuid.uuid.uuid128, CHRONCHI_TX_UUID_BYTES, 16);

                esp_attr_value_t tx_val = {};
                tx_val.attr_max_len = 512;
                tx_val.attr_len = 0;
                tx_val.attr_value = nullptr;

                esp_attr_control_t tx_ctrl = {};
                tx_ctrl.auto_rsp = ESP_GATT_RSP_BY_APP;

                esp_ble_gatts_add_char(_chronchi_svc_handle, &tx_uuid, ESP_GATT_PERM_READ,
                                       static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_NOTIFY),
                                       &tx_val, &tx_ctrl);
            }
            break;
        }

        case ESP_GATTS_ADD_CHAR_EVT: {
            uint16_t svc_handle = param->add_char.service_handle;
            uint16_t attr_handle = param->add_char.attr_handle;

            if (svc_handle == _chronos_svc_handle) {
                if (_chronos_tx_handle == 0) {
                    _chronos_tx_handle = attr_handle;
                    ESP_LOGI(TAG, "Chronos TX char handle: %d. Adding CCCD...", _chronos_tx_handle);

                    // Add CCCD descriptor for Chronos TX
                    esp_bt_uuid_t cccd_uuid = {};
                    cccd_uuid.len = ESP_UUID_LEN_16;
                    cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
                    esp_attr_value_t cccd_val = {};
                    cccd_val.attr_max_len = 2;
                    cccd_val.attr_len = 2;
                    uint8_t cccd_data[2] = {0x00, 0x00};
                    cccd_val.attr_value = cccd_data;
                    esp_ble_gatts_add_char_descr(_chronos_svc_handle, &cccd_uuid,
                                                 ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                 &cccd_val, nullptr);
                } else if (_chronos_rx_handle == 0) {
                    _chronos_rx_handle = attr_handle;
                    ESP_LOGI(TAG, "Chronos RX char handle: %d. Service 1 complete! Starting Service 1 and creating Service 2...", _chronos_rx_handle);
                    esp_ble_gatts_start_service(_chronos_svc_handle);

                    // Sequentially create Service 2: Chronchi (ESPBridge)
                    esp_gatt_srvc_id_t svc2_id = {};
                    svc2_id.is_primary = true;
                    svc2_id.id.inst_id = 0x01;
                    svc2_id.id.uuid.len = ESP_UUID_LEN_128;
                    memcpy(svc2_id.id.uuid.uuid.uuid128, CHRONCHI_SVC_UUID_BYTES, 16);
                    esp_ble_gatts_create_service(gatts_if, &svc2_id, 10);
                }
            } else if (svc_handle == _chronchi_svc_handle) {
                if (_chronchi_tx_handle == 0) {
                    _chronchi_tx_handle = attr_handle;
                    ESP_LOGI(TAG, "Chronchi TX char handle: %d. Adding CCCD...", _chronchi_tx_handle);

                    // Add CCCD descriptor for Chronchi TX
                    esp_bt_uuid_t cccd_uuid = {};
                    cccd_uuid.len = ESP_UUID_LEN_16;
                    cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
                    esp_attr_value_t cccd_val = {};
                    cccd_val.attr_max_len = 2;
                    cccd_val.attr_len = 2;
                    uint8_t cccd_data[2] = {0x00, 0x00};
                    cccd_val.attr_value = cccd_data;
                    esp_ble_gatts_add_char_descr(_chronchi_svc_handle, &cccd_uuid,
                                                 ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                 &cccd_val, nullptr);
                } else if (_chronchi_rx_handle == 0) {
                    _chronchi_rx_handle = attr_handle;
                    ESP_LOGI(TAG, "Chronchi RX char handle: %d. Service 2 complete! Starting Service 2...", _chronchi_rx_handle);
                    esp_ble_gatts_start_service(_chronchi_svc_handle);
                }
            }
            break;
        }

        case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
            uint16_t svc_handle = param->add_char_descr.service_handle;
            uint16_t attr_handle = param->add_char_descr.attr_handle;

            if (svc_handle == _chronos_svc_handle) {
                _chronos_tx_cccd_handle = attr_handle;
                ESP_LOGI(TAG, "Chronos TX CCCD handle: %d. Adding Chronos RX char...", _chronos_tx_cccd_handle);

                esp_bt_uuid_t rx_uuid = {};
                rx_uuid.len = ESP_UUID_LEN_128;
                memcpy(rx_uuid.uuid.uuid128, CHRONOS_NUS_RX_UUID_BYTES, 16);

                esp_attr_value_t rx_val = {};
                rx_val.attr_max_len = 512;
                rx_val.attr_len = 0;
                rx_val.attr_value = nullptr;

                esp_attr_control_t rx_ctrl = {};
                rx_ctrl.auto_rsp = ESP_GATT_RSP_BY_APP;

                esp_ble_gatts_add_char(_chronos_svc_handle, &rx_uuid,
                                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                       static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_WRITE |
                                                                         ESP_GATT_CHAR_PROP_BIT_WRITE_NR),
                                       &rx_val, &rx_ctrl);
            } else if (svc_handle == _chronchi_svc_handle) {
                _chronchi_tx_cccd_handle = attr_handle;
                ESP_LOGI(TAG, "Chronchi TX CCCD handle: %d. Adding Chronchi RX char...", _chronchi_tx_cccd_handle);

                esp_bt_uuid_t rx_uuid = {};
                rx_uuid.len = ESP_UUID_LEN_128;
                memcpy(rx_uuid.uuid.uuid128, CHRONCHI_RX_UUID_BYTES, 16);

                esp_attr_value_t rx_val = {};
                rx_val.attr_max_len = 512;
                rx_val.attr_len = 0;
                rx_val.attr_value = nullptr;

                esp_attr_control_t rx_ctrl = {};
                rx_ctrl.auto_rsp = ESP_GATT_RSP_BY_APP;

                esp_ble_gatts_add_char(_chronchi_svc_handle, &rx_uuid,
                                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                       static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_WRITE |
                                                                         ESP_GATT_CHAR_PROP_BIT_WRITE_NR),
                                       &rx_val, &rx_ctrl);
            }
            break;
        }

        case ESP_GATTS_START_EVT: {
            if (param->start.service_handle == _chronchi_svc_handle) {
                ESP_LOGI(TAG, "All services and characteristics registered. Configuring Advertising and Scan Response...");

                _adv_config_done = (1 << 0) | (1 << 1);

                // 1. Primary Advertising Data (31 bytes):
                // Flags (3 bytes) + Chronos NUS UUID 128 (18 bytes) + Name "Chronchi" (10 bytes)
                uint8_t adv_raw[31];
                int adv_len = 0;

                adv_raw[adv_len++] = 0x02;
                adv_raw[adv_len++] = 0x01;
                adv_raw[adv_len++] = 0x06;

                adv_raw[adv_len++] = 0x11;
                adv_raw[adv_len++] = 0x07;
                memcpy(&adv_raw[adv_len], CHRONOS_NUS_SVC_UUID_BYTES, 16);
                adv_len += 16;

                size_t name_len = _device_name.length();
                if (name_len > 8) name_len = 8;
                adv_raw[adv_len++] = static_cast<uint8_t>(name_len + 1);
                adv_raw[adv_len++] = (name_len < _device_name.length()) ? 0x08 : 0x09;
                memcpy(&adv_raw[adv_len], _device_name.data(), name_len);
                adv_len += name_len;

                esp_ble_gap_config_adv_data_raw(adv_raw, adv_len);

                // 2. Scan Response Data (31 bytes max):
                // Chronchi UUID 128 (18 bytes) + Name (up to 11 bytes)
                uint8_t scan_rsp_raw[31];
                int scan_rsp_len = 0;

                scan_rsp_raw[scan_rsp_len++] = 0x11;
                scan_rsp_raw[scan_rsp_len++] = 0x07;
                memcpy(&scan_rsp_raw[scan_rsp_len], CHRONCHI_SVC_UUID_BYTES, 16);
                scan_rsp_len += 16;

                size_t full_name_len = _device_name.length();
                if (full_name_len > 11) full_name_len = 11;
                scan_rsp_raw[scan_rsp_len++] = static_cast<uint8_t>(full_name_len + 1);
                scan_rsp_raw[scan_rsp_len++] = 0x09;
                memcpy(&scan_rsp_raw[scan_rsp_len], _device_name.data(), full_name_len);
                scan_rsp_len += full_name_len;

                esp_ble_gap_config_scan_rsp_data_raw(scan_rsp_raw, scan_rsp_len);
            }
            break;
        }

        case ESP_GATTS_CONNECT_EVT: {
            _connected = true;
            _advertising = false;
            _conn_id = param->connect.conn_id;
            memcpy(_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "Phone connected, conn_id=%d", _conn_id);

            if (_connCb) _connCb(true);
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT: {
            _connected = false;
            _advertising = false;
            _chronos_subscribed = false;
            _chronchi_subscribed = false;
            _cameraReady = false;
            _sendEspPending = false;
            _handshake_tick = 0;
            _chronchi_ready_tick = 0;
            _chronos_expected_len = 0;
            _chronos_received_len = 0;
            _espbridge_reassembly.Reset();
            ESP_LOGI(TAG, "Phone disconnected, reason=0x%X", param->disconnect.reason);

            if (_navigation.active) {
                _navigation.active = false;
                if (_navCb) _navCb(_navigation);
            }

            if (_connCb) _connCb(false);

            _adv_config_done = 0;
            esp_ble_gap_start_advertising(&_adv_params);
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            if (!param->write.is_prep) {
                // Subscription on Chronos TX CCCD
                if (param->write.handle == _chronos_tx_cccd_handle && param->write.len == 2) {
                    if (param->write.need_rsp) {
                        esp_gatt_rsp_t rsp = {};
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = 2;
                        rsp.attr_value.value[0] = param->write.value[0];
                        rsp.attr_value.value[1] = param->write.value[1];
                        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                    param->write.trans_id, ESP_GATT_OK, &rsp);
                    }
                    uint16_t cccd = (param->write.value[1] << 8) | param->write.value[0];
                    if (cccd != 0) {
                        _chronos_subscribed = true;
                        ESP_LOGI(TAG, "Chronos app subscribed to NUS TX (cccd=0x%04X). Scheduling handshake in 1.5s...", cccd);
                        _handshake_tick = xTaskGetTickCount() + pdMS_TO_TICKS(1500);

                        // Negotiate fast connection interval for instant notifications and responsive smartwatch feel
                        esp_ble_conn_update_params_t conn_params = {};
                        memcpy(conn_params.bda, _remote_bda, sizeof(esp_bd_addr_t));
                        conn_params.min_int = 0x10; // 20ms
                        conn_params.max_int = 0x20; // 40ms
                        conn_params.latency = 0;
                        conn_params.timeout = 400; // 4000ms
                        esp_ble_gap_update_conn_params(&conn_params);
                    } else {
                        _chronos_subscribed = false;
                        _handshake_tick = 0;
                        ESP_LOGI(TAG, "Chronos app unsubscribed from NUS TX");
                    }
                }
                // Subscription on Chronchi TX CCCD
                else if (param->write.handle == _chronchi_tx_cccd_handle && param->write.len == 2) {
                    if (param->write.need_rsp) {
                        esp_gatt_rsp_t rsp = {};
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = 2;
                        rsp.attr_value.value[0] = param->write.value[0];
                        rsp.attr_value.value[1] = param->write.value[1];
                        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                    param->write.trans_id, ESP_GATT_OK, &rsp);
                    }
                    uint16_t cccd = (param->write.value[1] << 8) | param->write.value[0];
                    if (cccd != 0) {
                        _chronchi_subscribed = true;
                        ESP_LOGI(TAG, "Chronchi app subscribed to TX (cccd=0x%04X). Scheduling device status...", cccd);
                        _chronchi_ready_tick = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
                    } else {
                        _chronchi_subscribed = false;
                        _chronchi_ready_tick = 0;
                        ESP_LOGI(TAG, "Chronchi app unsubscribed from TX");
                    }
                }
                // Data write on either RX characteristic
                else if (param->write.handle == _chronos_rx_handle || param->write.handle == _chronchi_rx_handle) {
                    if (param->write.need_rsp) {
                        esp_gatt_rsp_t rsp = {};
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = 0;
                        esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                    param->write.trans_id, ESP_GATT_OK, &rsp);
                    }
                    if (_rx_queue) {
                        QueueItem item;
                        item.len = param->write.len > sizeof(item.data) ? sizeof(item.data) : param->write.len;
                        memcpy(item.data, param->write.value, item.len);
                        xQueueSend(_rx_queue, &item, 0);
                    }
                }
            }
            break;
        }

        case ESP_GATTS_MTU_EVT: {
            _mtu = param->mtu.mtu;
            ESP_LOGI(TAG, "BLE MTU negotiated: %d", _mtu);
            break;
        }

        case ESP_GATTS_READ_EVT: {
            esp_gatt_rsp_t rsp = {};
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.offset = param->read.offset;
            if (param->read.handle == _chronos_tx_cccd_handle) {
                rsp.attr_value.len = 2;
                rsp.attr_value.value[0] = _chronos_subscribed ? 0x01 : 0x00;
                rsp.attr_value.value[1] = 0x00;
            } else if (param->read.handle == _chronchi_tx_cccd_handle) {
                rsp.attr_value.len = 2;
                rsp.attr_value.value[0] = _chronchi_subscribed ? 0x01 : 0x00;
                rsp.attr_value.value[1] = 0x00;
            } else {
                rsp.attr_value.len = 0;
            }
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id, ESP_GATT_OK, &rsp);
            break;
        }

        case ESP_GATTS_CONF_EVT:
            break;

        default:
            break;
    }
}
