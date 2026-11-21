#include "youtube_audio_poller.h"
#include <nvs.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <mutex>
#include <esp_memory_utils.h>

#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "settings.h"
#include "audio/audio_service.h"
#include "protocols/protocol.h"
#include "assets/lang_config.h"

static const char* TAG = "YTPoller";
static std::string s_cached_server_domain;
static std::mutex s_domain_mutex;

void YouTubeAudioPoller::InvalidateServerDomainCache() {
    std::lock_guard<std::mutex> lock(s_domain_mutex);
    s_cached_server_domain.clear();
}

std::string YouTubeAudioPoller::GetServerDomain() {
    {
        std::lock_guard<std::mutex> lock(s_domain_mutex);
        if (!s_cached_server_domain.empty()) {
            return s_cached_server_domain;
        }
    }

    char stack_probe;
    // CRITICAL: If the current task stack is in PSRAM (external SPIRAM),
    // calling NVS Flash will trigger an ESP-IDF assert in cache_utils.c:127
    // because cache must be disabled during flash operations, rendering external stack inaccessible.
    if (!esp_ptr_internal(&stack_probe)) {
        return "xiaozhiscig.biz.id";
    }

    std::string found_domain;
    Settings wifi_settings("wifi", false);
    found_domain = wifi_settings.GetString("server_domain");
    if (found_domain.empty()) {
        Settings sys_settings("system", false);
        found_domain = sys_settings.GetString("server_domain");
    }
#ifdef CONFIG_CONNECTION_SERVER_URL
    if (found_domain.empty()) {
        found_domain = CONFIG_CONNECTION_SERVER_URL;
    }
#endif
    if (found_domain.empty()) {
        found_domain = "xiaozhiscig.biz.id";
    }

    // Clean up trailing slashes and whitespace
    while (!found_domain.empty() && (found_domain.back() == '/' || found_domain.back() == ' ' ||
                                     found_domain.back() == '\r' || found_domain.back() == '\n')) {
        found_domain.pop_back();
    }
    size_t first = found_domain.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && first > 0) {
        found_domain = found_domain.substr(first);
    }

    // Strip protocol prefixes if present (e.g. "ws://", "wss://", "http://", "https://")
    if (found_domain.find("ws://") == 0) {
        found_domain = found_domain.substr(5);
    } else if (found_domain.find("wss://") == 0) {
        found_domain = found_domain.substr(6);
    } else if (found_domain.find("http://") == 0) {
        found_domain = found_domain.substr(7);
    } else if (found_domain.find("https://") == 0) {
        found_domain = found_domain.substr(8);
    }

    // Strip path if present (e.g. "domain.com/ws" -> "domain.com")
    size_t slash_pos = found_domain.find('/');
    if (slash_pos != std::string::npos) {
        found_domain = found_domain.substr(0, slash_pos);
    }

    if (found_domain.empty()) {
        found_domain = "xiaozhiscig.biz.id";
    }

    {
        std::lock_guard<std::mutex> lock(s_domain_mutex);
        s_cached_server_domain = found_domain;
    }
    ESP_LOGI(TAG, "Server domain resolved: %s", found_domain.c_str());
    return found_domain;
}

std::string YouTubeAudioPoller::GetBaseUrl() {
    std::string domain = GetServerDomain();
    while (!domain.empty() && domain.back() == '/') {
        domain.pop_back();
    }
    if (domain.find("http://") == 0 || domain.find("https://") == 0) {
        return domain;
    }
    if (!domain.empty() && isdigit((unsigned char)domain[0])) {
        return "http://" + domain;
    }
    return "https://" + domain;
}

std::string YouTubeAudioPoller::GetStreamUrl() {
    std::string domain = GetServerDomain();
    while (!domain.empty() && domain.back() == '/') {
        domain.pop_back();
    }
    if (domain.find("http://") == 0 || domain.find("https://") == 0) {
        return domain;
    }
    return "http://" + domain;
}

YouTubeAudioPoller::YouTubeAudioPoller() {
}

YouTubeAudioPoller::~YouTubeAudioPoller() {
    Stop();
    if (task_stack_ != nullptr) {
        heap_caps_free(task_stack_);
        task_stack_ = nullptr;
    }
    if (task_buffer_ != nullptr) {
        heap_caps_free(task_buffer_);
        task_buffer_ = nullptr;
    }
}

YouTubeAudioPoller& YouTubeAudioPoller::GetInstance() {
    static YouTubeAudioPoller instance;
    return instance;
}

void YouTubeAudioPoller::Start() {
    if (running_.load()) {
        return;
    }
    // Pre-cache server domain from NVS safely while running on internal stack (e.g. main_task)
    GetServerDomain();

    running_ = true;
    abort_requested_ = false;

    const size_t stack_size = 4096 * 8; // 32KB stack in PSRAM
    if (task_stack_ == nullptr) {
        task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (task_buffer_ == nullptr) {
        task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    }

    if (task_stack_ != nullptr && task_buffer_ != nullptr) {
        task_handle_ = xTaskCreateStatic([](void* arg) {
            static_cast<YouTubeAudioPoller*>(arg)->TaskLoop();
            vTaskDelete(NULL);
        }, "yt_poller", stack_size, this, 2, task_stack_, task_buffer_);
        ESP_LOGI(TAG, "YouTubeAudioPoller started (PSRAM stack: 32KB)");
    } else {
        xTaskCreate([](void* arg) {
            static_cast<YouTubeAudioPoller*>(arg)->TaskLoop();
            vTaskDelete(NULL);
        }, "yt_poller", 8192, this, 2, &task_handle_);
        ESP_LOGI(TAG, "YouTubeAudioPoller started (internal stack fallback: 8KB)");
    }
}

void YouTubeAudioPoller::Stop() {
    if (!running_.load()) {
        return;
    }
    running_ = false;
    abort_requested_ = true;
    if (task_handle_ != nullptr) {
        task_handle_ = nullptr;
    }
    ESP_LOGI(TAG, "YouTubeAudioPoller stopped");
}

void YouTubeAudioPoller::Abort() {
    has_pending_confirmation_ = false;
    if (is_playing_.load()) {
        ESP_LOGI(TAG, "Abort requested for YouTube audio stream");
        abort_requested_ = true;
        Application::GetInstance().GetAudioService().ResetDecoder();
    }
}

void YouTubeAudioPoller::TriggerFetch() {
    ESP_LOGI(TAG, "Fetch triggered externally");
    trigger_requested_ = true;
}

bool YouTubeAudioPoller::HasPendingSong() {
    std::lock_guard<std::mutex> lock(song_mutex_);
    return has_pending_confirmation_.load() && !pending_video_id_.empty();
}

void YouTubeAudioPoller::PlaySong(const std::string& video_id, const std::string& title, const std::string& duration) {
    std::string vid = video_id;
    std::string t = title;
    std::string dur = duration;
    {
        std::lock_guard<std::mutex> lock(song_mutex_);
        if (vid.empty() && !pending_video_id_.empty()) {
            vid = pending_video_id_;
            if (!pending_title_.empty()) {
                t = pending_title_;
            }
            if (!pending_duration_.empty()) {
                dur = pending_duration_;
            }
        }
    }
    if (vid.empty()) {
        if (!t.empty()) {
            PlayQuery(t);
        } else {
            ESP_LOGW(TAG, "PlaySong called with empty video_id and no pending song");
        }
        return;
    }
    Start();
    if (is_playing_.load()) {
        Abort();
    }
    std::string resolved_title = t.empty() ? "YouTube Song" : t;
    {
        std::lock_guard<std::mutex> lock(song_mutex_);
        pending_video_id_ = vid;
        pending_title_ = resolved_title;
        pending_duration_ = dur;
    }
    ESP_LOGI(TAG, "PlaySong called: title=\"%s\", vid=%s, dur=\"%s\"",
             resolved_title.c_str(), vid.c_str(), dur.c_str());
    has_pending_confirmation_ = false;
    play_requested_ = true;
}

bool YouTubeAudioPoller::SearchSong(const std::string& query, std::string& video_id, std::string& title, std::string& duration) {
    if (query.empty()) {
        return false;
    }
    bool found = FetchDirect(query, video_id, title, duration);
    if (found) {
        std::lock_guard<std::mutex> lock(song_mutex_);
        pending_video_id_ = video_id;
        pending_title_ = title;
        pending_duration_ = duration;
        has_pending_confirmation_ = true;
        ESP_LOGI(TAG, "SearchSong cached: title=\"%s\", vid=%s, dur=\"%s\"",
                 title.c_str(), video_id.c_str(), duration.c_str());
    }
    return found;
}

void YouTubeAudioPoller::PlayQuery(const std::string& query) {
    if (query.empty()) {
        return;
    }
    Start();
    if (is_playing_.load()) {
        Abort();
    }
    ESP_LOGI(TAG, "PlayQuery called: query=\"%s\"", query.c_str());
    {
        std::lock_guard<std::mutex> lock(song_mutex_);
        pending_query_ = query;
    }
    query_requested_ = true;
}

bool YouTubeAudioPoller::FetchDirect(const std::string& query, std::string& video_id, std::string& title, std::string& duration) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) return false;

    auto http = network->CreateHttp(0);
    if (http == nullptr) return false;

    std::string encoded_q;
    for (char c : query) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded_q += c;
        } else if (c == ' ') {
            encoded_q += "%20";
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            encoded_q += hex;
        }
    }

    std::string mac = SystemInfo::GetMacAddress();
    std::string base = GetBaseUrl();
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    std::string url = base + "/api/audio/play_direct?q=" + encoded_q + "&mac=" + mac;
    ESP_LOGI(TAG, "FetchDirect querying URL: %s", url.c_str());
    http->SetTimeout(15000);
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());
    http->SetHeader("Accept", "application/json");

    bool opened = http->Open("GET", url);
    int status = opened ? http->GetStatusCode() : 0;
    if (status == 301 || status == 302) {
        std::string loc = http->GetResponseHeader("Location");
        if (!loc.empty()) {
            http->Close();
            url = loc;
            http->SetTimeout(15000);
            http->SetHeader("User-Agent", "ESP32-XiaoZhi");
            http->SetHeader("Device-Id", mac.c_str());
            http->SetHeader("X-Device-Mac", mac.c_str());
            http->SetHeader("Accept", "application/json");
            opened = http->Open("GET", url);
            status = opened ? http->GetStatusCode() : 0;
        }
    }

    if (!opened || status != 200) {
        if (url.find("https://") == 0) {
            std::string http_url = "http://" + url.substr(8);
            ESP_LOGW(TAG, "HTTPS returned %d (opened=%d), retrying via HTTP: %s", status, (int)opened, http_url.c_str());
            http->Close();
            http = network->CreateHttp(0);
            if (http != nullptr) {
                http->SetTimeout(15000);
                http->SetHeader("User-Agent", "ESP32-XiaoZhi");
                http->SetHeader("Device-Id", mac.c_str());
                http->SetHeader("X-Device-Mac", mac.c_str());
                http->SetHeader("Accept", "application/json");
                opened = http->Open("GET", http_url);
                status = opened ? http->GetStatusCode() : 0;
            }
        }
    }

    if (!opened || status != 200) {
        ESP_LOGW(TAG, "FetchDirect HTTP returned %d (opened=%d)", status, (int)opened);
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    if (body.empty()) {
        ESP_LOGW(TAG, "FetchDirect received empty body");
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "FetchDirect JSON parse failed: %s", body.c_str());
        return false;
    }

    bool found = false;
    cJSON* vid_item = cJSON_GetObjectItem(root, "video_id");
    cJSON* title_item = cJSON_GetObjectItem(root, "title");
    cJSON* dur_item = cJSON_GetObjectItem(root, "duration");
    cJSON* dur_str_item = cJSON_GetObjectItem(root, "duration_string");

    if (cJSON_IsString(vid_item) && vid_item->valuestring != nullptr && vid_item->valuestring[0] != '\0') {
        video_id = vid_item->valuestring;
        title = (cJSON_IsString(title_item) && title_item->valuestring != nullptr && title_item->valuestring[0] != '\0')
                ? title_item->valuestring : query;

        if (cJSON_IsString(dur_str_item) && dur_str_item->valuestring != nullptr && dur_str_item->valuestring[0] != '\0') {
            duration = dur_str_item->valuestring;
        } else if (cJSON_IsString(dur_item) && dur_item->valuestring != nullptr && dur_item->valuestring[0] != '\0') {
            duration = dur_item->valuestring;
        } else if (cJSON_IsNumber(dur_item) && dur_item->valueint > 0) {
            int sec = dur_item->valueint;
            int m = sec / 60;
            int s = sec % 60;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d:%02d", m, s);
            duration = buf;
        } else {
            duration = "";
        }
        found = true;
    }
    cJSON_Delete(root);
    return found;
}

bool YouTubeAudioPoller::FetchDirect(const std::string& query, std::string& video_id, std::string& title) {
    std::string dur;
    return FetchDirect(query, video_id, title, dur);
}

bool YouTubeAudioPoller::FetchCommands(int& cmd_id, std::string& video_id, std::string& title, std::string& duration) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }

    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        return false;
    }

    std::string mac = SystemInfo::GetMacAddress();
    std::string chip = CONFIG_IDF_TARGET;
    std::string base = GetBaseUrl();
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    std::string url = base + "/api/device/audio/commands?mac=" + mac + "&chip=" + chip;

    http->SetTimeout(10000);
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());
    http->SetHeader("X-Chip-Type", chip.c_str());
    http->SetHeader("X-Device-Chip", chip.c_str());
    http->SetHeader("Device-Chip", chip.c_str());

    if (!http->Open("GET", url)) {
        ESP_LOGW(TAG, "FetchCommands: Failed to connect to %s", url.c_str());
        http->Close();
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGW(TAG, "FetchCommands: HTTP returned status %d", http->GetStatusCode());
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    if (body.empty()) {
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return false;
    }

    bool found = false;
    cJSON* commands = cJSON_GetObjectItem(root, "commands");
    if (cJSON_IsArray(commands) && cJSON_GetArraySize(commands) > 0) {
        cJSON* cmd = cJSON_GetArrayItem(commands, 0);
        if (cmd != nullptr) {
            cJSON* id_item = cJSON_GetObjectItem(cmd, "id");
            cJSON* vid_item = cJSON_GetObjectItem(cmd, "video_id");
            cJSON* title_item = cJSON_GetObjectItem(cmd, "title");
            cJSON* dur_item = cJSON_GetObjectItem(cmd, "duration");
            cJSON* dur_str_item = cJSON_GetObjectItem(cmd, "duration_string");

            if (cJSON_IsNumber(id_item) && cJSON_IsString(vid_item)) {
                cmd_id = id_item->valueint;
                video_id = vid_item->valuestring;
                title = cJSON_IsString(title_item) ? title_item->valuestring : "YouTube Song";

                if (cJSON_IsString(dur_str_item) && dur_str_item->valuestring[0] != '\0') {
                    duration = dur_str_item->valuestring;
                } else if (cJSON_IsString(dur_item) && dur_item->valuestring[0] != '\0') {
                    duration = dur_item->valuestring;
                } else if (cJSON_IsNumber(dur_item) && dur_item->valueint > 0) {
                    int sec = dur_item->valueint;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
                    duration = buf;
                } else {
                    duration = "";
                }
                found = true;
            }
        }
    }

    cJSON_Delete(root);
    return found;
}

bool YouTubeAudioPoller::FetchCommands(int& cmd_id, std::string& video_id, std::string& title) {
    std::string dur;
    return FetchCommands(cmd_id, video_id, title, dur);
}

bool YouTubeAudioPoller::AckCommand(int cmd_id) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }

    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        return false;
    }

    std::string mac = SystemInfo::GetMacAddress();
    std::string chip = CONFIG_IDF_TARGET;
    std::string url = GetBaseUrl() + "/api/device/audio/ack";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "command_id", cmd_id);
    cJSON_AddStringToObject(root, "mac", mac.c_str());
    cJSON_AddStringToObject(root, "chip", chip.c_str());
    char* json_str = cJSON_PrintUnformatted(root);
    std::string payload = json_str ? json_str : "";
    if (json_str) {
        free(json_str);
    }
    cJSON_Delete(root);

    http->SetTimeout(10000);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());
    http->SetHeader("X-Chip-Type", chip.c_str());
    http->SetHeader("X-Device-Chip", chip.c_str());
    http->SetHeader("Device-Chip", chip.c_str());
    http->SetContent(std::move(payload));

    bool ok = http->Open("POST", url);
    if (ok) {
        http->ReadAll();
    }
    http->Close();
    return ok;
}

bool YouTubeAudioPoller::ReportStatus(const std::string& status, const std::string& video_id) {
    ESP_LOGI(TAG, "ReportStatus: status=\"%s\", video_id=%s (local)", status.c_str(), video_id.c_str());
    return true;
}


static int ParseDurationToSeconds(const std::string& dur_str) {
    if (dur_str.empty()) return 0;
    int current_val = 0;
    bool has_colon = false;
    std::vector<int> parts;
    for (char c : dur_str) {
        if (c >= '0' && c <= '9') {
            current_val = current_val * 10 + (c - '0');
        } else if (c == ':') {
            has_colon = true;
            parts.push_back(current_val);
            current_val = 0;
        }
    }
    parts.push_back(current_val);
    if (!has_colon) {
        return current_val;
    }
    if (parts.size() == 2) {
        return parts[0] * 60 + parts[1];
    } else if (parts.size() == 3) {
        return parts[0] * 3600 + parts[1] * 60 + parts[2];
    }
    return 0;
}

static int GetOpusPacketDurationMs(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 20;
    }

    const uint8_t toc = data[0];
    const uint8_t config = toc >> 3;
    int frame_duration_us = 0;
    if (config < 12) {
        static constexpr int kSilkDurationsUs[] = {10000, 20000, 40000, 60000};
        frame_duration_us = kSilkDurationsUs[config & 0x03];
    } else if (config < 16) {
        frame_duration_us = (config & 0x01) ? 20000 : 10000;
    } else {
        static constexpr int kCeltDurationsUs[] = {2500, 5000, 10000, 20000};
        frame_duration_us = kCeltDurationsUs[config & 0x03];
    }

    int frame_count = 0;
    switch (toc & 0x03) {
        case 0:
            frame_count = 1;
            break;
        case 1:
        case 2:
            frame_count = 2;
            break;
        case 3:
            if (size < 2) {
                return 20;
            }
            frame_count = data[1] & 0x3f;
            break;
    }

    int total_duration_us = frame_duration_us * frame_count;
    int ms = total_duration_us / 1000;
    return (ms > 0) ? ms : 20;
}

void YouTubeAudioPoller::StreamAudio(const std::string& video_id, const std::string& title, const std::string& duration) {
    auto& app = Application::GetInstance();
    auto display = Board::GetInstance().GetDisplay();

    is_playing_ = true;
    abort_requested_ = false;
    app.ResetAborted();

    ESP_LOGI(TAG, "Starting YouTube stream: %s (%s, dur=%s)", title.c_str(), video_id.c_str(), duration.c_str());
    ReportStatus("playing", video_id);
    app.GetAudioService().EnableWakeWordDetection(false);

    // Wait for previous audio playback to finish (max 3 sec) before opening stream
    {
        int wait_ms = 0;
        while (!app.GetAudioService().IsPlaybackIdle() && wait_ms < 3000 && !abort_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_ms += 100;
        }
        if (wait_ms > 0) {
            ESP_LOGI(TAG, "Waited %d ms for previous playback to finish before streaming", wait_ms);
        }
    }

    if (abort_requested_.load()) {
        is_playing_ = false;
        app.Schedule([&app]() { app.SetDeviceState(kDeviceStateIdle); });
        return;
    }

    // Close any open audio channel to the server so mic does not record speaker output or hit 30s listening timeout
    app.CloseAudioChannel();
    app.Schedule([&app]() {
        if (app.GetDeviceState() == kDeviceStateConnecting || app.GetDeviceState() == kDeviceStateNotifying) {
            app.SetDeviceState(kDeviceStateIdle);
        }
        app.SetDeviceState(kDeviceStateSpeaking);
    });

    // Clear any pending play/query requests that accumulated during TTS wait
    play_requested_ = false;
    query_requested_ = false;

    int total_duration_sec = ParseDurationToSeconds(duration);
    if (duration != "LIVE" && total_duration_sec == 0 && !video_id.empty()) {
        std::string res_vid, res_title, res_dur;
        if (FetchDirect(video_id, res_vid, res_title, res_dur)) {
            int parsed = ParseDurationToSeconds(res_dur);
            if (parsed > 0) {
                total_duration_sec = parsed;
            }
        }
    }

    std::string display_title = "[PLAY] " + title;
    if (display != nullptr) {
        display->StartMusicVisualizer(title.c_str(), total_duration_sec);
        display->SetMusicProgress(0, total_duration_sec);
        display->ShowNotification(display_title.c_str(), 5000);
    }
    auto bl = Board::GetInstance().GetBacklight();
    if (bl != nullptr) {
        ESP_LOGI(TAG, "YouTube playback started -> set screen brightness to 70%%");
        bl->SetBrightness(70, false);
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        if (!codec->output_enabled()) {
            codec->EnableOutput(true);
        }
        if (codec->output_volume() <= 0) {
            codec->SetOutputVolume(70);
        }
        ESP_LOGI(TAG, "Audio playback using current device volume: %d%%", codec->output_volume());
    }

    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Reset decoder to clear any leftover TTS audio in the queue
    app.GetAudioService().ResetDecoder();
    vTaskDelay(pdMS_TO_TICKS(50));

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        is_playing_ = false;
        if (display != nullptr) {
            display->StopMusicVisualizer();
        }
        if (bl != nullptr) {
            app.Schedule([bl]() { bl->RestoreBrightness(); });
        }
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_NONE);
        app.Schedule([&app]() { app.SetDeviceState(kDeviceStateIdle); });
        return;
    }

    // 1. Initial Bitrate Evaluation based on Wi-Fi RSSI & Free Internal SRAM
    wifi_ap_record_t ap_info;
    int8_t rssi = -100;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }
    size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    std::string current_bitrate = "32k";
#if CONFIG_IDF_TARGET_ESP32S3
    if (rssi >= -68) {
        current_bitrate = "40k"; // Sinyal Kuat: 40k Max Fidelity
    } else if (rssi >= -76) {
        current_bitrate = "32k"; // Sinyal Bagus: 32k
    } else if (rssi >= -82) {
        current_bitrate = "24k"; // Sinyal Sedang: 24k
    } else if (rssi >= -88) {
        current_bitrate = "16k"; // Sinyal Lemah: 16k
    } else {
        current_bitrate = "12k"; // Sinyal Sangat Lemah: 12k
    }
    static const size_t kOggBufCap = 16384; // 16KB buffer leveraging 8MB PSRAM
#else
    if (rssi >= -65 && free_sram >= 48000) {
        current_bitrate = "16k";
    } else if (rssi >= -75 && free_sram >= 36000) {
        current_bitrate = "12k";
    } else if (rssi >= -82 && free_sram >= 28000) {
        current_bitrate = "8k";
    } else {
        current_bitrate = "6k";
    }
    static const size_t kOggBufCap = 4096;
#endif
    ESP_LOGI(TAG, "Adaptive Profile Selected: RSSI=%d dBm, FreeSRAM=%u B -> Bitrate=%s",
             rssi, (unsigned int)free_sram, current_bitrate.c_str());

    static const uint8_t OGGS_MAGIC[4] = {'O', 'g', 'g', 'S'};
    uint8_t* raw_ogg = (uint8_t*)heap_caps_malloc(kOggBufCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw_ogg == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate ogg_buf in PSRAM");
        is_playing_ = false;
        if (display != nullptr) {
            display->StopMusicVisualizer();
        }
        if (bl != nullptr) {
            app.Schedule([bl]() { bl->RestoreBrightness(); });
        }
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_NONE);
        app.Schedule([&app]() { app.SetDeviceState(kDeviceStateIdle); });
        return;
    }
    std::unique_ptr<uint8_t[], void(*)(void*)> ogg_buf(raw_ogg, [](void* p) { heap_caps_free(p); });

    size_t total_bytes_streamed = 0;
    int frames_pushed = 0;
    int downshift_retries = 0;
    int eof_retries = 0;
    static const int kMaxRetries = 4;
    int64_t last_downshift_time_us = 0;
    int64_t upshift_healthy_start_us = esp_timer_get_time();
    int64_t last_rssi_check_us = 0;
    int8_t live_rssi = rssi;
    int detected_frame_duration_ms = 20;
    float frame_sec = 0.020f;
    int last_reported_sec = 0;
    int last_reported_total_sec = total_duration_sec;

    while (running_.load() && !abort_requested_.load()) {
        auto http = network->CreateHttp(0);
        if (http == nullptr) {
            break;
        }

        float start_sec = frames_pushed * frame_sec;
        char url_buf[256];
        std::string mac = SystemInfo::GetMacAddress();
        std::string chip = CONFIG_IDF_TARGET;
        if (start_sec > 0.5f) {
            snprintf(url_buf, sizeof(url_buf), "%s/api/audio/stream/%s?br=%s&start=%.2f&mac=%s&chip=%s",
                     GetStreamUrl().c_str(), video_id.c_str(), current_bitrate.c_str(), start_sec, mac.c_str(), chip.c_str());
        } else {
            snprintf(url_buf, sizeof(url_buf), "%s/api/audio/stream/%s?br=%s&mac=%s&chip=%s",
                     GetStreamUrl().c_str(), video_id.c_str(), current_bitrate.c_str(), mac.c_str(), chip.c_str());
        }
        std::string stream_url(url_buf);

        http->SetTimeout(15000); // 15s timeout to tolerate network jitter & backend transcoding latency
        http->SetHeader("User-Agent", "ESP32-XiaoZhi");
        http->SetHeader("Device-Id", mac.c_str());
        http->SetHeader("X-Device-Mac", mac.c_str());
        http->SetHeader("X-Device-Chip", chip.c_str());
        http->SetHeader("Device-Chip", chip.c_str());

        ESP_LOGI(TAG, "Connecting to stream URL: %s", stream_url.c_str());
        bool opened = http->Open("GET", stream_url);
        int status = opened ? http->GetStatusCode() : 0;
        if (status == 301 || status == 302) {
            std::string loc = http->GetResponseHeader("Location");
            if (!loc.empty()) {
                ESP_LOGI(TAG, "Stream redirected (%d) to: %s", status, loc.c_str());
                http->Close();
                stream_url = loc;
                http->SetTimeout(15000);
                http->SetHeader("User-Agent", "ESP32-XiaoZhi");
                http->SetHeader("Device-Id", mac.c_str());
                http->SetHeader("X-Device-Mac", mac.c_str());
                http->SetHeader("X-Device-Chip", chip.c_str());
                http->SetHeader("Device-Chip", chip.c_str());
                opened = http->Open("GET", stream_url);
                status = opened ? http->GetStatusCode() : 0;
            }
        }

        if (!opened || status != 200) {
            std::string alt_url = stream_url;
            if (stream_url.find("http://") == 0) {
                alt_url = "https://" + stream_url.substr(7);
            } else if (stream_url.find("https://") == 0) {
                alt_url = "http://" + stream_url.substr(8);
            }
            if (alt_url != stream_url) {
                ESP_LOGW(TAG, "Stream failed with status %d (opened=%d), retrying with: %s", status, (int)opened, alt_url.c_str());
                http->Close();
                http = network->CreateHttp(0);
                if (http != nullptr) {
                    http->SetTimeout(15000);
                    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
                    http->SetHeader("Device-Id", mac.c_str());
                    http->SetHeader("X-Device-Mac", mac.c_str());
                    http->SetHeader("X-Device-Chip", chip.c_str());
                    http->SetHeader("Device-Chip", chip.c_str());
                    opened = http->Open("GET", alt_url);
                    status = opened ? http->GetStatusCode() : 0;
                    if (opened && status == 200) {
                        stream_url = alt_url;
                    }
                }
            }
        }

        if (!opened || status != 200) {
            ESP_LOGE(TAG, "Stream failed or returned HTTP %d (opened=%d)", status, (int)opened);
            http->Close();
            if (frames_pushed > 0 && downshift_retries < kMaxRetries) {
                downshift_retries++;
                vTaskDelay(pdMS_TO_TICKS(500 * downshift_retries));
                continue;
            }
            break;
        }

        ESP_LOGI(TAG, "Stream connected (HTTP 200, br=%s, start=%.2fs), reading chunks...",
                 current_bitrate.c_str(), start_sec);

        if (total_duration_sec == 0) {
            static const char* const kDurationHeaders[] = {
                "X-Audio-Duration", "x-audio-duration",
                "X-Song-Duration",  "x-song-duration",
                "Content-Duration", "content-duration",
                "X-Duration",       "x-duration",
            };
            std::string hdr_dur;
            for (const char* header_name : kDurationHeaders) {
                hdr_dur = http->GetResponseHeader(header_name);
                if (!hdr_dur.empty()) {
                    break;
                }
            }
            if (!hdr_dur.empty()) {
                int parsed = ParseDurationToSeconds(hdr_dur);
                if (parsed > 0) {
                    total_duration_sec = parsed;
                    ESP_LOGI(TAG, "Received duration from stream header: %s (%d sec)", hdr_dur.c_str(), total_duration_sec);
                    if (display != nullptr) {
                        int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                        int frames_played = std::max(0, frames_pushed - q_size);
                        int cur_sec = std::min(total_duration_sec, (int)(frames_played * frame_sec));
                        display->SetMusicProgress(cur_sec, total_duration_sec);
                        last_reported_sec = cur_sec;
                        last_reported_total_sec = total_duration_sec;
                    }
                }
            }
        }

        size_t ogg_len = 0;
        bool seen_head = false;
        bool seen_tags = false;
        bool eos_detected = false;
        bool stop_stream = false;
        int sample_rate = 48000;
        bool need_downshift = false;
        int64_t last_audio_active_us = esp_timer_get_time();
        size_t session_bytes = 0;
        int session_frames = 0;
        int64_t last_progress_sync_us = 0;

        // Dynamic Pre-Buffer (Jitter Buffer):
        // Target ~900ms - 1200ms buffer agar pemutaran lagu YouTube jauh lebih lancar & anti stutter/buffer underrun
        size_t prebuffer_target = (detected_frame_duration_ms <= 20) ? 45 : 16;
        if (live_rssi < -78) {
            prebuffer_target = (detected_frame_duration_ms <= 20) ? 60 : 22; // Sinyal lemah: buffer lebih besar
        }
        std::vector<std::unique_ptr<AudioStreamPacket>> prebuffer;
        bool is_prebuffering = true;

        try {
            while (running_.load() && !abort_requested_.load() && !stop_stream) {
                int64_t now_us = esp_timer_get_time();
                bool playback_idle = app.GetAudioService().IsPlaybackIdle();

                // Real-time smooth progress sync (runs every 500ms based on actually heard frames)
                if (now_us - last_progress_sync_us >= 500000) {
                    last_progress_sync_us = now_us;
                    int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                    int frames_played = std::max(0, frames_pushed - q_size);
                    int current_sec = (int)(frames_played * frame_sec);
                    if (total_duration_sec > 0 && current_sec > total_duration_sec) {
                        current_sec = total_duration_sec;
                    }
                    if (display != nullptr && (current_sec != last_reported_sec || total_duration_sec != last_reported_total_sec)) {
                        last_reported_sec = current_sec;
                        last_reported_total_sec = total_duration_sec;
                        display->SetMusicProgress(current_sec, total_duration_sec);
                    }
                    if (total_duration_sec > 0 && current_sec >= total_duration_sec) {
                        ESP_LOGI(TAG, "[DETEKSI] Durasi lagu tercapai (%d >= %d s), mengakhiri pemutaran...",
                                 current_sec, total_duration_sec);
                        need_downshift = false;
                        stop_stream = true;
                        break;
                    }
                }

                // Periksa status sinyal live berkala (tiap 5 detik) dengan aman tanpa lag
                if (now_us - last_rssi_check_us > 5000000) {
                    last_rssi_check_us = now_us;
                    wifi_ap_record_t live_ap;
                    if (esp_wifi_sta_get_ap_info(&live_ap) == ESP_OK) {
                        live_rssi = live_ap.rssi;
                    }
                }

                // 1. Ultra-lightweight Speaker Starvation Watchdog (Fast Drop):
                // Jika audio playback queue kosong / speaker tidak bersuara > 2.5s, auto-recovering!
                if (frames_pushed > (detected_frame_duration_ms <= 20 ? 30 : 10)) {
                    if (!playback_idle) {
                        last_audio_active_us = now_us;
                    } else if (now_us - last_audio_active_us > 2500000) { // 2.5s speaker silence (tolerant to jitter)
                        ESP_LOGW(TAG, "Audio starvation detected! Speaker is silent at br=%s, auto-recovering...", current_bitrate.c_str());
#if CONFIG_IDF_TARGET_ESP32S3
                        if (current_bitrate != "12k") {
                            if (current_bitrate == "40k") current_bitrate = "32k";
                            else if (current_bitrate == "32k") current_bitrate = "24k";
                            else if (current_bitrate == "24k") current_bitrate = "16k";
                            else current_bitrate = "12k";
                            need_downshift = true;
                        } else if (downshift_retries < kMaxRetries) {
                            need_downshift = true;
                            downshift_retries++;
                        }
#else
                        if (current_bitrate != "6k") {
                            if (current_bitrate == "24k" || current_bitrate == "16k") current_bitrate = "12k";
                            else if (current_bitrate == "12k") current_bitrate = "8k";
                            else current_bitrate = "6k";
                            need_downshift = true;
                        } else if (downshift_retries < kMaxRetries) {
                            need_downshift = true;
                            downshift_retries++;
                        }
#endif
                        if (need_downshift) {
                            last_downshift_time_us = now_us;
                            upshift_healthy_start_us = now_us;
                            ESP_LOGW(TAG, "Watchdog recovered stream by downshifting/reconnecting to %s at %.2fs",
                                     current_bitrate.c_str(), frames_pushed * frame_sec);
                            break;
                        }
                    }
                }

                // 2. Adaptive Upshift (Slow & Safe Climb - Golden Rule: Drop Fast, Climb Slow):
                // Jika bitrate di bawah 40k, cooldown downshift > 40s, dan antrean decode sehat (>= 8 frame) selama 35s kontinu:
#if CONFIG_IDF_TARGET_ESP32S3
                if (current_bitrate != "40k" && frames_pushed > 20) {
                    size_t q_size = app.GetAudioService().GetDecodeQueueSize();
                    bool queue_healthy = (q_size >= 8 && !playback_idle);

                    bool rssi_qualified = false;
                    std::string next_tier;
                    if (current_bitrate == "12k" && live_rssi >= -84) {
                        next_tier = "16k";
                        rssi_qualified = true;
                    } else if (current_bitrate == "16k" && live_rssi >= -78) {
                        next_tier = "24k";
                        rssi_qualified = true;
                    } else if (current_bitrate == "24k" && live_rssi >= -72) {
                        next_tier = "32k";
                        rssi_qualified = true;
                    } else if (current_bitrate == "32k" && live_rssi >= -65) {
                        next_tier = "40k";
                        rssi_qualified = true;
                    }

                    if (queue_healthy && rssi_qualified) {
                        if (now_us - last_downshift_time_us > 40000000) { // Cooldown 40 detik
                            if (now_us - upshift_healthy_start_us > 35000000) { // 35 detik konsisten sehat
                                ESP_LOGI(TAG, "Network condition excellent! Sustained healthy buffer & RSSI=%d dBm for 35s. Upshifting %s -> %s at %.2fs",
                                         live_rssi, current_bitrate.c_str(), next_tier.c_str(), frames_pushed * frame_sec);
                                current_bitrate = next_tier;
                                need_downshift = true; // Reconnect bersih ke tingkat bitrate lebih tinggi
                                upshift_healthy_start_us = now_us;
                                break;
                            }
                        }
                    } else {
                        upshift_healthy_start_us = now_us; // Reset jika ada fluktuasi
                    }
                }
#endif

                size_t avail = kOggBufCap - ogg_len;
                if (avail == 0) {
                    // Buffer is full without a parsed page. Discard until next OggS magic.
                    size_t next_oggs = 1;
                    while (next_oggs + 4 <= ogg_len && memcmp(ogg_buf.get() + next_oggs, OGGS_MAGIC, 4) != 0) {
                        next_oggs++;
                    }
                    if (next_oggs < ogg_len) {
                        memmove(ogg_buf.get(), ogg_buf.get() + next_oggs, ogg_len - next_oggs);
                        ogg_len -= next_oggs;
                    } else {
                        ogg_len = 0;
                    }
                    avail = kOggBufCap - ogg_len;
                }
                int to_read = (int)std::min((size_t)4096, avail);
                int n = http->Read(reinterpret_cast<char*>(ogg_buf.get() + ogg_len), to_read);
                if (n < 0) {
                    ESP_LOGW(TAG, "HTTP read error or stream closed (ret=%d, total=%u, frames=%d)",
                             n, (unsigned int)total_bytes_streamed, frames_pushed);
                    if (eos_detected || (total_duration_sec > 0 && (frames_pushed * frame_sec >= (total_duration_sec - 3.0f)))) {
                        ESP_LOGI(TAG, "Natural stream completion detected at %.2fs, finishing playback without retry",
                                 frames_pushed * frame_sec);
                        need_downshift = false;
                        break;
                    }
#if CONFIG_IDF_TARGET_ESP32S3
                    if (current_bitrate != "12k") {
                        if (current_bitrate == "40k") current_bitrate = "32k";
                        else if (current_bitrate == "32k") current_bitrate = "24k";
                        else if (current_bitrate == "24k") current_bitrate = "16k";
                        else current_bitrate = "12k";
                        need_downshift = true;
                        last_downshift_time_us = now_us;
                        upshift_healthy_start_us = now_us;
                        ESP_LOGW(TAG, "Connection interrupted mid-stream! Downshifting to %s at %.2fs",
                                 current_bitrate.c_str(), frames_pushed * frame_sec);
                    } else if (downshift_retries < kMaxRetries) {
                        need_downshift = true;
                        downshift_retries++;
                        last_downshift_time_us = now_us;
                        upshift_healthy_start_us = now_us;
                        ESP_LOGW(TAG, "Connection interrupted at 12k! Reconnecting (attempt %d/%d) at %.2fs",
                                 downshift_retries, kMaxRetries, frames_pushed * frame_sec);
                    }
#else
                    if (current_bitrate != "6k") {
                        if (current_bitrate == "24k" || current_bitrate == "16k") current_bitrate = "12k";
                        else if (current_bitrate == "12k") current_bitrate = "8k";
                        else current_bitrate = "6k";
                        need_downshift = true;
                        last_downshift_time_us = now_us;
                        upshift_healthy_start_us = now_us;
                        ESP_LOGW(TAG, "Connection interrupted mid-stream! Downshifting to %s at %.2fs",
                                 current_bitrate.c_str(), frames_pushed * frame_sec);
                    } else if (downshift_retries < kMaxRetries) {
                        need_downshift = true;
                        downshift_retries++;
                        last_downshift_time_us = now_us;
                        upshift_healthy_start_us = now_us;
                        ESP_LOGW(TAG, "Connection interrupted at 6k! Reconnecting (attempt %d/%d) at %.2fs",
                                 downshift_retries, kMaxRetries, frames_pushed * frame_sec);
                    }
#endif
                    break;
                }
                if (n == 0) {
                    ESP_LOGI(TAG, "Stream EOF reached (session_bytes=%u, session_frames=%d, total=%u, frames_pushed=%d)",
                             (unsigned int)session_bytes, session_frames, (unsigned int)total_bytes_streamed, frames_pushed);
                    // YouTube CDN sering memutus TCP stream di tengah lagu (~1-2 menit).
                    // Jika sesi ini sempat memutar data audio, lakukan auto-resume dari offset start_sec.
                    // Namun jika EOS sudah terdeteksi atau lagu sudah di akhir durasi (dalam 3 detik terakhir), jangan reconnect.
                    float played_sec = frames_pushed * frame_sec;
                    bool is_natural_end = eos_detected || (total_duration_sec > 0 && played_sec >= (total_duration_sec - 3.0f));
                    if (!is_natural_end && frames_pushed > 0 && session_bytes > 0 && eof_retries < 2 && !abort_requested_.load()) {
                        eof_retries++;
                        need_downshift = true;
                        ESP_LOGI(TAG, "Potential premature EOF from YouTube/CDN! Auto-resuming at %.2fs (probe %d/2)...",
                                 frames_pushed * frame_sec, eof_retries);
                    } else {
                        if ((session_bytes == 0 || eos_detected) && frames_pushed > 0) {
                            ESP_LOGI(TAG, "Confirmed end of audio stream at %.2fs", frames_pushed * frame_sec);
                        }
                        need_downshift = false;
                    }
                    break;
                }

                ogg_len += n;
                total_bytes_streamed += n;
                session_bytes += n;

                // Demux Ogg pages incrementally
                while (ogg_len >= 27 && !stop_stream) {
                    if (abort_requested_.load()) {
                        ESP_LOGI(TAG, "Audio stream aborted by user request");
                        break;
                    }

                    // Find next OggS magic
                    size_t oggs_pos = 0;
                    bool found = false;
                    while (oggs_pos + 4 <= ogg_len) {
                        if (memcmp(ogg_buf.get() + oggs_pos, OGGS_MAGIC, 4) == 0) {
                            found = true;
                            break;
                        }
                        oggs_pos++;
                    }

                    if (!found) {
                        if (ogg_len >= 3) {
                            memmove(ogg_buf.get(), ogg_buf.get() + ogg_len - 3, 3);
                            ogg_len = 3;
                        }
                        break;
                    }

                    if (oggs_pos > 0) {
                        memmove(ogg_buf.get(), ogg_buf.get() + oggs_pos, ogg_len - oggs_pos);
                        ogg_len -= oggs_pos;
                        if (ogg_len < 27) break;
                    }

                    uint8_t header_type = ogg_buf[5];
                    uint8_t num_segs = ogg_buf[26];
                    size_t hdr_len = 27 + num_segs;
                    if (ogg_len < hdr_len) {
                        break;
                    }

                    size_t body_len = 0;
                    for (size_t i = 0; i < num_segs; ++i) {
                        body_len += ogg_buf[27 + i];
                    }

                    size_t page_total = hdr_len + body_len;
                    if (ogg_len < page_total) {
                        break;
                    }

                    if (header_type & 0x04) {
                        ESP_LOGI(TAG, "Ogg EOS (End Of Stream) flag detected at %.2fs",
                                 frames_pushed * frame_sec);
                        eos_detected = true;
                    }

                    // Extract packets from this page
                    size_t cur = hdr_len;
                    size_t seg_idx = 0;
                    while (seg_idx < num_segs) {
                        size_t pkt_len = 0;
                        size_t pkt_start = cur;
                        bool continued = false;
                        do {
                            uint8_t l = ogg_buf[27 + seg_idx++];
                            pkt_len += l;
                            cur += l;
                            continued = (l == 255);
                        } while (continued && seg_idx < num_segs);

                        if (pkt_len == 0) {
                            continue;
                        }

                        const uint8_t* pkt_ptr = ogg_buf.get() + pkt_start;

                        if (!seen_head) {
                            if (pkt_len >= 19 && memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                                seen_head = true;
                                if (pkt_len >= 16) {
                                    uint32_t head_sr = pkt_ptr[12] | (pkt_ptr[13] << 8) | (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                                    switch (head_sr) {
                                        case 8000:
                                        case 12000:
                                        case 16000:
                                        case 24000:
                                        case 48000:
                                            sample_rate = (int)head_sr;
                                            break;
                                        default:
                                            sample_rate = 48000;
                                            break;
                                    }
                                    ESP_LOGI(TAG, "OpusHead parsed: sample_rate=%d, channels=%d", sample_rate, (int)pkt_ptr[9]);
                                }
                            }
                            continue;
                        }

                        if (!seen_tags) {
                            if (pkt_len >= 8 && memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                                seen_tags = true;
                                ESP_LOGI(TAG, "OpusTags parsed, ready to decode audio");
                            }
                            continue;
                        }

                        if (abort_requested_.load()) {
                            ESP_LOGI(TAG, "Audio stream aborted by user request");
                            break;
                        }

                        // Inspect actual Opus frame duration dynamically
                        int pkt_dur = GetOpusPacketDurationMs(pkt_ptr, pkt_len);
                        if (pkt_dur > 0) {
                            detected_frame_duration_ms = pkt_dur;
                            frame_sec = detected_frame_duration_ms / 1000.0f;
                            if (is_prebuffering && prebuffer.empty()) {
                                prebuffer_target = (detected_frame_duration_ms <= 20) ? 45 : 16;
                                if (live_rssi < -78) {
                                    prebuffer_target = (detected_frame_duration_ms <= 20) ? 60 : 22;
                                }
                            }
                        }

                        // Push raw Opus frame with true stream sample rate and frame duration
                        auto packet = std::make_unique<AudioStreamPacket>();
                        packet->sample_rate = sample_rate;
                        packet->frame_duration = detected_frame_duration_ms;
                        packet->payload.assign(pkt_ptr, pkt_ptr + pkt_len);

                        if (is_prebuffering) {
                            prebuffer.push_back(std::move(packet));
                            if (prebuffer.size() >= prebuffer_target) {
                                ESP_LOGI(TAG, "Pre-buffer ready (%u frames, ~%u ms), flushing to decoder (br=%s, sr=%d, dur=%dms)",
                                         (unsigned int)prebuffer.size(),
                                         (unsigned int)(prebuffer.size() * detected_frame_duration_ms),
                                         current_bitrate.c_str(), sample_rate, detected_frame_duration_ms);
                                for (auto& p : prebuffer) {
                                    app.GetAudioService().PushPacketToDecodeQueue(std::move(p), true);
                                    frames_pushed++;
                                    session_frames++;
                                }
                                prebuffer.clear();
                                is_prebuffering = false;
                                int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                                int frames_played = std::max(0, frames_pushed - q_size);
                                int current_sec = (int)(frames_played * frame_sec);
                                if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
                                if (display != nullptr && (current_sec != last_reported_sec || total_duration_sec != last_reported_total_sec)) {
                                    last_reported_sec = current_sec;
                                    last_reported_total_sec = total_duration_sec;
                                    display->SetMusicProgress(current_sec, total_duration_sec);
                                }
                                ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube berhasil diputar (Playback started): '%s' (ID: %s)", title.c_str(), video_id.c_str());
                                app.Schedule([&app]() {
                                    if (app.GetDeviceState() == kDeviceStateConnecting || app.GetDeviceState() == kDeviceStateNotifying) {
                                        app.SetDeviceState(kDeviceStateIdle);
                                    }
                                    app.SetDeviceState(kDeviceStateSpeaking);
                                });
                            }
                        } else {
                            app.GetAudioService().PushPacketToDecodeQueue(std::move(packet), true);
                            frames_pushed++;
                            session_frames++;
                            if (frames_pushed == 1 || frames_pushed % 50 == 0) {
                                ESP_LOGI(TAG, "Audio frame #%d pushed to decoder (len=%u, dur=%dms, sr=%d, q_size=%u)",
                                         frames_pushed, (unsigned int)pkt_len, detected_frame_duration_ms, sample_rate,
                                         (unsigned int)app.GetAudioService().GetDecodeQueueSize());
                            }
                            if (frames_pushed % 16 == 0) {
                                int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                                int frames_played = std::max(0, frames_pushed - q_size);
                                int current_sec = (int)(frames_played * frame_sec);
                                if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
                                if (display != nullptr && (current_sec != last_reported_sec || total_duration_sec != last_reported_total_sec)) {
                                    last_reported_sec = current_sec;
                                    last_reported_total_sec = total_duration_sec;
                                    display->SetMusicProgress(current_sec, total_duration_sec);
                                }
                                // Deteksi durasi lagu selesai: segera akhiri streaming dan aktifkan mode menyimak (listening)!
                                if (total_duration_sec > 0 && current_sec >= total_duration_sec) {
                                    ESP_LOGI(TAG, "[DETEKSI] Durasi lagu tercapai (%d >= %d s), mengakhiri pemutaran...",
                                             current_sec, total_duration_sec);
                                    need_downshift = false;
                                    stop_stream = true;
                                    break;
                                }
                            }
                        }

                        if (session_frames >= 20) {
                            downshift_retries = 0;
                            eof_retries = 0;
                        }
                    }

                    memmove(ogg_buf.get(), ogg_buf.get() + page_total, ogg_len - page_total);
                    ogg_len -= page_total;

                    if (abort_requested_.load()) {
                        ESP_LOGI(TAG, "Audio stream aborted by user request");
                        break;
                    }
                    if (stop_stream || (eos_detected && (frames_pushed > 0 || !prebuffer.empty()))) {
                        ESP_LOGI(TAG, "EOS / duration page processed (%.2fs), concluding network stream cleanly",
                                 frames_pushed * frame_sec);
                        need_downshift = false;
                        stop_stream = true;
                        break;
                    }
                }
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "StreamAudio exception: %s", e.what());
        }

        // Flush sisa prebuffer jika stream terputus sebelum mencapai target
        if (!prebuffer.empty() && !abort_requested_.load()) {
            for (auto& p : prebuffer) {
                app.GetAudioService().PushPacketToDecodeQueue(std::move(p), true);
                frames_pushed++;
                session_frames++;
            }
            prebuffer.clear();
            int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
            int frames_played = std::max(0, frames_pushed - q_size);
            int current_sec = (int)(frames_played * frame_sec);
            if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
            if (display != nullptr && (current_sec != last_reported_sec || total_duration_sec != last_reported_total_sec)) {
                last_reported_sec = current_sec;
                last_reported_total_sec = total_duration_sec;
                display->SetMusicProgress(current_sec, total_duration_sec);
            }
        }
        prebuffer.clear();

        http->Close();

        if (!need_downshift || abort_requested_.load() || stop_stream) {
            break;
        }
    }
    ESP_LOGI(TAG, "Stream finished (total %u bytes read, frames_pushed=%d, aborted=%d)",
             (unsigned int)total_bytes_streamed, frames_pushed, abort_requested_.load());

    bool was_aborted = abort_requested_.load();
    if (!was_aborted) {
        int drain_timeout_ms = std::max(3000, MAX_DECODE_PACKETS_IN_QUEUE * detected_frame_duration_ms + 1000);
        int wait_ms = 0;
        while (!app.GetAudioService().IsPlaybackIdle() && wait_ms < drain_timeout_ms && !abort_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_ms += 50;
            int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
            int frames_played = std::max(0, frames_pushed - q_size);
            int current_sec = (int)(frames_played * frame_sec);
            if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
            if (display != nullptr && (current_sec != last_reported_sec || total_duration_sec != last_reported_total_sec)) {
                last_reported_sec = current_sec;
                last_reported_total_sec = total_duration_sec;
                display->SetMusicProgress(current_sec, total_duration_sec);
            }
        }
        if (display != nullptr && total_duration_sec > 0) {
            display->SetMusicProgress(total_duration_sec, total_duration_sec);
        }
        ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube selesai diputar tuntas (Playback drained & finished): '%s' (ID: %s)",
                 title.c_str(), video_id.c_str());
    } else {
        app.GetAudioService().ResetDecoder();
        ESP_LOGI(TAG, "[DETEKSI] Pemutaran lagu YouTube dihentikan (Playback stopped): '%s'", title.c_str());
    }

    if (was_aborted) {
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }

    std::string end_status = was_aborted ? "stopped" : "finished";
    ReportStatus(end_status, video_id);

    is_playing_ = false;

    if (bl != nullptr) {
        app.Schedule([bl]() {
            bl->RestoreBrightness();
        });
    }

    // Otomatis aktifkan XiaoZhi untuk berinteraksi setelah lagu selesai diputar
    if (!was_aborted && frames_pushed > 0) {
        app.GetAudioService().ResetDecoder();
        if (display != nullptr) {
            display->StopMusicVisualizer();
            display->ShowNotification("Lagu Selesai Diputar", 2000);
        }

        if (!Board::GetInstance().IsChronchiMode()) {
            ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube selesai diputar: '%s'. Mengaktifkan XiaoZhi untuk berinteraksi.", title.c_str());
            std::string safe_title = title;
            for (auto& c : safe_title) {
                if (c == '"' || c == '\\') c = ' ';
            }
            // Buka kembali kanal audio WebSocket ke server dan aktifkan mode menyimak sebenarnya (mic live)
            app.WakeUpAfterPlayback(safe_title);
        } else {
            ESP_LOGI(TAG, "Lagu selesai di Mode Chronchi. Tetap di mode saat ini tanpa mengaktifkan XiaoZhi.");
            Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
            esp_wifi_set_ps(WIFI_PS_NONE);
            app.Schedule([&app]() {
                app.SetDeviceState(kDeviceStateIdle);
            });
        }
    } else {
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_NONE);
        app.Schedule([&app]() {
            app.SetDeviceState(kDeviceStateIdle);
        });
        if (!Board::GetInstance().IsChronchiMode()) {
            app.GetAudioService().EnableWakeWordDetection(true);
        }
        if (display != nullptr) {
            display->StopMusicVisualizer();
            display->SetChatMessage("assistant", "");
            display->SetChatMessage("system", "");
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
        }
    }
}

void YouTubeAudioPoller::TaskLoop() {
    ESP_LOGI(TAG, "YouTube player ready (on-demand direct playback)");

    while (running_.load()) {
        // Wait for direct tool call (play_requested_), direct query (query_requested_), or trigger (trigger_requested_)
        while (running_.load() && !play_requested_.load() && !query_requested_.load() && !trigger_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (!running_.load()) {
            break;
        }

        // Direct MCP tool call has highest priority
        if (play_requested_.exchange(false)) {
            std::string vid, tit, dur;
            {
                std::lock_guard<std::mutex> lock(song_mutex_);
                vid = pending_video_id_;
                tit = pending_title_;
                dur = pending_duration_;
            }
            if (!vid.empty()) {
                ESP_LOGI(TAG, "Direct tool call playing: \"%s\" (vid=%s, dur=%s)", tit.c_str(), vid.c_str(), dur.c_str());
                StreamAudio(vid, tit, dur);
            }
        }
        // Direct voice query search
        else if (query_requested_.exchange(false)) {
            std::string q;
            {
                std::lock_guard<std::mutex> lock(song_mutex_);
                q = pending_query_;
            }
            if (!q.empty()) {
                ESP_LOGI(TAG, "Searching & streaming song query: \"%s\"", q.c_str());
                std::string vid, tit, dur;
                if (FetchDirect(q, vid, tit, dur)) {
                    StreamAudio(vid, tit, dur);
                }
            }
        }
        // Fallback queue trigger
        else if (trigger_requested_.exchange(false)) {
            int cmd_id = 0;
            std::string video_id;
            std::string title;
            std::string duration;

            if (FetchCommands(cmd_id, video_id, title, duration)) {
                ESP_LOGI(TAG, "Playing requested YouTube song (1 song only): id=%d, title=\"%s\", vid=%s, dur=%s",
                         cmd_id, title.c_str(), video_id.c_str(), duration.c_str());
                AckCommand(cmd_id);
                StreamAudio(video_id, title, duration);
            }
        }
    }
}
