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

#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio/audio_service.h"
#include "protocols/protocol.h"
#include "assets/lang_config.h"
#include "esp_audio_dec.h"
#include "esp_opus_dec.h"
#include <sys/stat.h>
#include <dirent.h>
#include <esp_memory_utils.h>

static const char* TAG = "YTPoller";
static std::string s_cached_server_domain = "";
static std::mutex s_domain_mutex;

void YouTubeAudioPoller::SetServerDomain(const std::string& domain) {
    std::lock_guard<std::mutex> lock(s_domain_mutex);
    s_cached_server_domain = domain;
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
#ifdef CONFIG_CONNECTION_SERVER_URL
        std::string cfg_url = CONFIG_CONNECTION_SERVER_URL;
        if (!cfg_url.empty()) {
            return cfg_url;
        }
#endif
        return "";
    }

    nvs_handle_t nvs;
    char buf[128] = {};
    size_t len = sizeof(buf);
    std::string found_domain;
    // 1. Check "wifi" namespace (configured via Wi-Fi AP portal)
    if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_str(nvs, "server_domain", buf, &len) == ESP_OK && len > 0 && buf[0] != '\0') {
            found_domain = buf;
        }
        nvs_close(nvs);
    }
    // 2. Fallback to "system" namespace
    if (found_domain.empty()) {
        len = sizeof(buf);
        if (nvs_open("system", NVS_READONLY, &nvs) == ESP_OK) {
            if (nvs_get_str(nvs, "server_domain", buf, &len) == ESP_OK && len > 0 && buf[0] != '\0') {
                found_domain = buf;
            }
            nvs_close(nvs);
        }
    }
#ifdef CONFIG_CONNECTION_SERVER_URL
    if (found_domain.empty()) {
        std::string cfg_url = CONFIG_CONNECTION_SERVER_URL;
        if (!cfg_url.empty()) {
            found_domain = cfg_url;
        }
    }
#endif

    if (!found_domain.empty()) {
        std::lock_guard<std::mutex> lock(s_domain_mutex);
        s_cached_server_domain = found_domain;
    }
    return found_domain;
}

std::string YouTubeAudioPoller::GetBaseUrl() {
    std::string domain = GetServerDomain();
    if (domain.empty()) {
        return "";
    }
    if (domain.find("http://") == 0 || domain.find("https://") == 0) {
        return domain;
    }
    return "https://" + domain;
}

std::string YouTubeAudioPoller::GetStreamUrl() {
    std::string domain = GetServerDomain();
    if (domain.empty()) {
        return "";
    }
    if (domain.find("http://") == 0 || domain.find("https://") == 0) {
        return domain;
    }
    return "https://" + domain;
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

void YouTubeAudioPoller::PlaySong(const std::string& video_id, const std::string& title, const std::string& duration) {
    if (video_id.empty()) {
        return;
    }
    Start();
    if (is_playing_.load()) {
        Abort();
    }
    ESP_LOGI(TAG, "PlaySong called directly: title=\"%s\", vid=%s, dur=%s", title.c_str(), video_id.c_str(), duration.c_str());
    {
        std::lock_guard<std::mutex> lock(song_mutex_);
        pending_video_id_ = video_id;
        pending_title_ = title.empty() ? "YouTube Song" : title;
        pending_duration_ = duration;
    }
    play_requested_ = true;
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

bool YouTubeAudioPoller::DownloadSong(const std::string& query_or_video_id, const std::string& title) {
    if (query_or_video_id.empty()) {
        return false;
    }
    if (is_downloading_.load()) {
        ESP_LOGW(TAG, "Download sedang berjalan, permintaan baru diabaikan");
        return false;
    }
    ESP_LOGI(TAG, "DownloadSong dipanggil: query/vid=\"%s\", title=\"%s\"", query_or_video_id.c_str(), title.c_str());
    {
        std::lock_guard<std::mutex> lock(song_mutex_);
        pending_dl_video_id_ = query_or_video_id;
        pending_dl_title_ = title.empty() ? query_or_video_id : title;
    }
    download_requested_ = true;
    return true;
}

void YouTubeAudioPoller::AbortDownload() {
    if (is_downloading_.load()) {
        ESP_LOGI(TAG, "Membatalkan download lagu YouTube");
        abort_download_ = true;
    }
}

bool YouTubeAudioPoller::FetchDirect(const std::string& query, std::string& video_id, std::string& title) {
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
    std::string url = GetStreamUrl() + "/api/audio/play_direct?q=" + encoded_q + "&mac=" + mac;
    ESP_LOGI(TAG, "FetchDirect querying URL: %s", url.c_str());
    http->SetTimeout(12000);
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());

    bool opened = http->Open("GET", url);
    int status = opened ? http->GetStatusCode() : 0;
    if (status == 301 || status == 302) {
        std::string loc = http->GetResponseHeader("Location");
        if (!loc.empty()) {
            http->Close();
            url = loc;
            http->SetTimeout(12000);
            http->SetHeader("User-Agent", "ESP32-XiaoZhi");
            http->SetHeader("Device-Id", mac.c_str());
            http->SetHeader("X-Device-Mac", mac.c_str());
            opened = http->Open("GET", url);
            status = opened ? http->GetStatusCode() : 0;
        }
    }

    if (!opened || status != 200) {
        ESP_LOGW(TAG, "FetchDirect HTTP returned %d (opened=%d)", status, (int)opened);
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    if (body.empty()) return false;

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return false;

    bool found = false;
    cJSON* vid_item = cJSON_GetObjectItem(root, "video_id");
    cJSON* title_item = cJSON_GetObjectItem(root, "title");
    if (cJSON_IsString(vid_item)) {
        video_id = vid_item->valuestring;
        title = cJSON_IsString(title_item) ? title_item->valuestring : query;
        found = true;
    }
    cJSON_Delete(root);
    return found;
}

bool YouTubeAudioPoller::FetchSongInfo(const std::string& query, std::string& video_id, std::string& title, std::string& duration) {
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
    std::string url = GetStreamUrl() + "/api/audio/play_direct?q=" + encoded_q + "&mac=" + mac;
    ESP_LOGI(TAG, "FetchSongInfo querying URL: %s", url.c_str());
    http->SetTimeout(12000);
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());

    bool opened = http->Open("GET", url);
    int status = opened ? http->GetStatusCode() : 0;
    if (status == 301 || status == 302) {
        std::string loc = http->GetResponseHeader("Location");
        if (!loc.empty()) {
            http->Close();
            url = loc;
            http->SetTimeout(12000);
            http->SetHeader("User-Agent", "ESP32-XiaoZhi");
            http->SetHeader("Device-Id", mac.c_str());
            http->SetHeader("X-Device-Mac", mac.c_str());
            opened = http->Open("GET", url);
            status = opened ? http->GetStatusCode() : 0;
        }
    }

    if (!opened || status != 200) {
        ESP_LOGW(TAG, "FetchSongInfo HTTP returned %d (opened=%d)", status, (int)opened);
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    if (body.empty()) return false;

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return false;

    bool found = false;
    cJSON* vid_item = cJSON_GetObjectItem(root, "video_id");
    cJSON* title_item = cJSON_GetObjectItem(root, "title");
    cJSON* dur_item = cJSON_GetObjectItem(root, "duration");
    if (cJSON_IsString(vid_item)) {
        video_id = vid_item->valuestring;
        title = cJSON_IsString(title_item) ? title_item->valuestring : query;
        if (cJSON_IsString(dur_item) && dur_item->valuestring[0] != '\0') {
            duration = dur_item->valuestring;
        } else if (cJSON_IsNumber(dur_item)) {
            char dur_buf[16];
            int s = dur_item->valueint;
            snprintf(dur_buf, sizeof(dur_buf), "%02d:%02d", s / 60, s % 60);
            duration = dur_buf;
        } else {
            duration = "";
        }
        found = true;
    }
    cJSON_Delete(root);
    return found;
}

bool YouTubeAudioPoller::FetchCommands(int& cmd_id, std::string& video_id, std::string& title) {
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
    std::string url = GetBaseUrl() + "/api/device/audio/commands?mac=" + mac + "&chip=" + chip;

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

            if (cJSON_IsNumber(id_item) && cJSON_IsString(vid_item)) {
                cmd_id = id_item->valueint;
                video_id = vid_item->valuestring;
                title = cJSON_IsString(title_item) ? title_item->valuestring : "YouTube Song";
                found = true;
            }
        }
    }

    cJSON_Delete(root);
    return found;
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
        app.SetDeviceState(kDeviceStateIdle);
        return;
    }

    // Close any open audio channel to the server so mic does not record speaker output or hit 30s listening timeout
    app.CloseAudioChannel();
    app.SetDeviceState(kDeviceStateSpeaking);  // Show "speaking" state during YouTube playback

    // Clear any pending play/query requests that accumulated during TTS wait
    play_requested_ = false;
    query_requested_ = false;

    int total_duration_sec = ParseDurationToSeconds(duration);
    if (duration != "LIVE" && total_duration_sec == 0 && !video_id.empty()) {
        std::string res_vid, res_title, res_dur;
        if (FetchSongInfo(video_id, res_vid, res_title, res_dur)) {
            total_duration_sec = ParseDurationToSeconds(res_dur);
        }
    }

    std::string display_title = "[PLAY] " + title;
    if (display != nullptr) {
        display->StartMusicVisualizer(title.c_str());
        display->UpdateMusicProgress(0, total_duration_sec);
        display->ShowNotification(display_title.c_str(), 5000);
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
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        app.SetDeviceState(kDeviceStateIdle);
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
        app.SetDeviceState(kDeviceStateIdle);
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
            std::string hdr_dur = http->GetResponseHeader("X-Audio-Duration");
            if (hdr_dur.empty()) {
                hdr_dur = http->GetResponseHeader("X-Song-Duration");
            }
            if (!hdr_dur.empty()) {
                int parsed = ParseDurationToSeconds(hdr_dur);
                if (parsed > 0) {
                    total_duration_sec = parsed;
                    ESP_LOGI(TAG, "Received duration from stream header: %s (%d sec)", hdr_dur.c_str(), total_duration_sec);
                    if (display != nullptr) {
                        display->UpdateMusicProgress((int)(frames_pushed * frame_sec), total_duration_sec);
                    }
                }
            }
        }

        size_t ogg_len = 0;
        bool seen_head = false;
        bool seen_tags = false;
        int sample_rate = 48000;
        bool need_downshift = false;
        int64_t last_audio_active_us = esp_timer_get_time();
        size_t session_bytes = 0;
        int session_frames = 0;
        int64_t last_progress_sync_us = 0;

        // Dynamic Pre-Buffer (Jitter Buffer):
        // Target ~240ms - 400ms buffer based on RSSI and frame duration
        size_t prebuffer_target = (detected_frame_duration_ms <= 20) ? 12 : 4;
        if (frames_pushed > 0 || current_bitrate != "40k") {
            prebuffer_target = (detected_frame_duration_ms <= 20) ? ((live_rssi >= -70) ? 12 : 20) : ((live_rssi >= -70) ? 4 : 8);
        } else if (live_rssi >= -68) {
            prebuffer_target = (detected_frame_duration_ms <= 20) ? 10 : 2;
        } else if (live_rssi >= -76) {
            prebuffer_target = (detected_frame_duration_ms <= 20) ? 14 : 4;
        } else {
            prebuffer_target = (detected_frame_duration_ms <= 20) ? 20 : 8;
        }
        std::vector<std::unique_ptr<AudioStreamPacket>> prebuffer;
        bool is_prebuffering = true;

        try {
            while (running_.load() && !abort_requested_.load()) {
                int64_t now_us = esp_timer_get_time();
                bool playback_idle = app.GetAudioService().IsPlaybackIdle();

                // Real-time smooth progress sync (runs every 500ms based on actually heard frames)
                if (now_us - last_progress_sync_us >= 500000) {
                    last_progress_sync_us = now_us;
                    int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                    int frames_played = frames_pushed - q_size;
                    if (frames_played < 0) frames_played = 0;
                    int current_sec = (int)(frames_played * frame_sec);
                    if (total_duration_sec > 0 && current_sec > total_duration_sec) {
                        current_sec = total_duration_sec;
                    }
                    if (display != nullptr) {
                        display->UpdateMusicProgress(current_sec, total_duration_sec);
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
                // Jika bitrate di bawah 48k, cooldown downshift > 40s, dan antrean decode sehat (>= 8 frame) selama 35s kontinu:
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
                    // Namun jika lagu sudah di akhir durasi (dalam 3 detik terakhir), jangan reconnect agar tidak stuck.
                    float played_sec = frames_pushed * frame_sec;
                    bool is_natural_end = (total_duration_sec > 0 && played_sec >= (total_duration_sec - 3.0f));
                    if (!is_natural_end && frames_pushed > 0 && session_bytes > 0 && eof_retries < 2 && !abort_requested_.load()) {
                        eof_retries++;
                        need_downshift = true;
                        ESP_LOGI(TAG, "Potential premature EOF from YouTube/CDN! Auto-resuming at %.2fs (probe %d/2)...",
                                 frames_pushed * frame_sec, eof_retries);
                    } else {
                        if (session_bytes == 0 && frames_pushed > 0) {
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
                while (ogg_len >= 27) {
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
                                int frames_played = frames_pushed - q_size;
                                if (frames_played < 0) frames_played = 0;
                                int current_sec = (int)(frames_played * frame_sec);
                                if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
                                if (display != nullptr) {
                                    display->UpdateMusicProgress(current_sec, total_duration_sec);
                                }
                                ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube berhasil diputar (Playback started): '%s' (ID: %s)", title.c_str(), video_id.c_str());
                                app.Schedule([&app]() {
                                    app.SetDeviceState(kDeviceStateSpeaking);
                                });
                            }
                        } else {
                            app.GetAudioService().PushPacketToDecodeQueue(std::move(packet), true);
                            frames_pushed++;
                            session_frames++;
                            if (frames_pushed == 1 || frames_pushed % 50 == 0) {
                                ESP_LOGI(TAG, "Audio frame #%d pushed to decoder (len=%u, dur=%dms, sr=%d, q_size=%u)",
                                         frames_pushed, (unsigned int)pkt_len, detected_frame_duration_ms, sample_rate, (unsigned int)app.GetAudioService().GetDecodeQueueSize());
                            }
                            if (display != nullptr && (frames_pushed % 16 == 0)) {
                                int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
                                int frames_played = frames_pushed - q_size;
                                if (frames_played < 0) frames_played = 0;
                                int current_sec = (int)(frames_played * frame_sec);
                                if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
                                display->UpdateMusicProgress(current_sec, total_duration_sec);
                                if (total_duration_sec > 0 && current_sec >= total_duration_sec) {
                                    ESP_LOGI(TAG, "Duration reached (%d >= %d), ending stream immediately", current_sec, total_duration_sec);
                                    need_downshift = false;
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
                }
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "StreamAudio exception: %s", e.what());
        }

        // Flush sisa prebuffer jika stream terputus sebelum mencapai target
        if (!prebuffer.empty()) {
            for (auto& p : prebuffer) {
                app.GetAudioService().PushPacketToDecodeQueue(std::move(p), true);
                frames_pushed++;
                session_frames++;
            }
            prebuffer.clear();
            int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
            int frames_played = frames_pushed - q_size;
            if (frames_played < 0) frames_played = 0;
            int current_sec = (int)(frames_played * frame_sec);
            if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
            if (display != nullptr) {
                display->UpdateMusicProgress(current_sec, total_duration_sec);
            }
        }

        http->Close();

        if (!need_downshift || abort_requested_.load()) {
            break;
        }
    }
    ESP_LOGI(TAG, "Stream finished (total %u bytes read, frames_pushed=%d, aborted=%d)",
             (unsigned int)total_bytes_streamed, frames_pushed, abort_requested_.load());

    bool was_aborted = abort_requested_.load();
    if (!was_aborted) {
        int wait_ms = 0;
        while (!app.GetAudioService().IsPlaybackIdle() && wait_ms < 1200 && !abort_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_ms += 50;
            int q_size = (int)app.GetAudioService().GetDecodeQueueSize();
            int frames_played = frames_pushed - q_size;
            if (frames_played < 0) frames_played = 0;
            int current_sec = (int)(frames_played * frame_sec);
            if (total_duration_sec > 0 && current_sec > total_duration_sec) current_sec = total_duration_sec;
            if (display != nullptr) {
                display->UpdateMusicProgress(current_sec, total_duration_sec);
            }
        }
        if (display != nullptr && total_duration_sec > 0) {
            display->UpdateMusicProgress(total_duration_sec, total_duration_sec);
        }
        ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube selesai diputar (Playback finished): '%s' (ID: %s)", title.c_str(), video_id.c_str());
    } else {
        app.GetAudioService().ResetDecoder();
        ESP_LOGI(TAG, "[DETEKSI] Pemutaran lagu YouTube dihentikan (Playback stopped): '%s'", title.c_str());
    }

    if (was_aborted) {
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }

    std::string end_status = was_aborted ? "stopped" : "finished";
    ReportStatus(end_status, video_id);

    is_playing_ = false;

    // Otomatis aktifkan XiaoZhi untuk berinteraksi setelah lagu selesai diputar
    if (!was_aborted && frames_pushed > 0) {
        if (Board::GetInstance().GetWavRepeat()) {
            ESP_LOGI(TAG, "Mode Ulang Aktif: Memutar kembali lagu YouTube '%s'", title.c_str());
            app.GetAudioService().ResetDecoder();
            vTaskDelay(pdMS_TO_TICKS(400));
            StreamAudio(video_id, title, duration);
            return;
        }

        ESP_LOGI(TAG, "[DETEKSI] Lagu YouTube selesai diputar: '%s'. Mengaktifkan XiaoZhi untuk berinteraksi.", title.c_str());
        std::string safe_title = title;
        for (auto& c : safe_title) {
            if (c == '"' || c == '\\') c = ' ';
        }

        // Bersihkan decoder audio & hentikan visualizer
        app.GetAudioService().ResetDecoder();
        if (display != nullptr) {
            display->StopMusicVisualizer();
            display->ShowNotification("Lagu Selesai Diputar", 2000);
        }

        // Buka kembali kanal audio WebSocket ke server dan aktifkan mode menyimak sebenarnya (mic live)
        app.Schedule([&app, safe_title]() {
            app.WakeUpAfterPlayback(safe_title);
        });
    } else {
        app.Schedule([&app]() {
            app.SetDeviceState(kDeviceStateIdle);
        });
        app.GetAudioService().EnableWakeWordDetection(true);
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
    ESP_LOGI(TAG, "YouTube player ready (on-demand direct playback & offline download)");

    while (running_.load()) {
        // Wait for direct tool call (play_requested_), direct query (query_requested_), trigger (trigger_requested_), or download (download_requested_)
        while (running_.load() && !play_requested_.load() && !query_requested_.load() && !trigger_requested_.load() && !download_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (!running_.load()) {
            break;
        }

        // Direct MCP download request to SD card
        if (download_requested_.exchange(false)) {
            std::string q_or_vid, tit;
            {
                std::lock_guard<std::mutex> lock(song_mutex_);
                q_or_vid = pending_dl_video_id_;
                tit = pending_dl_title_;
            }
            std::string vid = q_or_vid;
            if (vid.length() != 11 || vid.find(' ') != std::string::npos) {
                std::string resolved_vid, resolved_title, duration;
                if (FetchSongInfo(q_or_vid, resolved_vid, resolved_title, duration)) {
                    vid = resolved_vid;
                    if (tit.empty() || tit == q_or_vid) tit = resolved_title;
                }
            }
            if (!vid.empty()) {
                ExecuteDownload(vid, tit);
            }
        }
        // Direct MCP tool call has highest priority
        else if (play_requested_.exchange(false)) {
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
                if (FetchSongInfo(q, vid, tit, dur)) {
                    StreamAudio(vid, tit, dur);
                } else if (FetchDirect(q, vid, tit)) {
                    StreamAudio(vid, tit);
                }
            }
        }
        // Fallback queue trigger
        else if (trigger_requested_.exchange(false)) {
            int cmd_id = 0;
            std::string video_id;
            std::string title;

            if (FetchCommands(cmd_id, video_id, title)) {
                ESP_LOGI(TAG, "Playing requested YouTube song (1 song only): id=%d, title=\"%s\", vid=%s",
                         cmd_id, title.c_str(), video_id.c_str());
                AckCommand(cmd_id);
                StreamAudio(video_id, title);
            }
        }
    }
}

void YouTubeAudioPoller::ExecuteDownload(const std::string& video_id, const std::string& title) {
    ESP_LOGI(TAG, "ExecuteDownload started: title=\"%s\", vid=%s", title.c_str(), video_id.c_str());

    auto& app = Application::GetInstance();
    auto display = Board::GetInstance().GetDisplay();

    // Tunggu jika ada audio/TTS sebelumnya yang sedang bicara (maks 4 detik)
    int wait_ms = 0;
    while (!app.GetAudioService().IsPlaybackIdle() && wait_ms < 4000 && !abort_download_.load()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ms += 100;
    }

    if (!Board::GetInstance().IsSdCardMounted()) {
        DIR* test_dir = opendir("/sdcard");
        if (!test_dir) {
            ESP_LOGE(TAG, "Direktori /sdcard tidak tersedia, batalkan download");
            return;
        }
        closedir(test_dir);
    }
    mkdir("/sdcard/music", 0777);

    std::string safe_title = title;
    for (char& c : safe_title) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '\n' || c == '\r') {
            c = '_';
        }
    }
    while (!safe_title.empty() && safe_title.back() == ' ') safe_title.pop_back();
    while (!safe_title.empty() && safe_title.front() == ' ') safe_title.erase(0, 1);
    if (safe_title.empty()) {
        safe_title = "youtube_" + video_id;
    }
    if (safe_title.length() > 50) {
        safe_title = safe_title.substr(0, 50);
    }

    std::string file_path = "/sdcard/music/" + safe_title + ".wav";
    FILE* f = fopen(file_path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "Gagal membuat berkas audio: %s", file_path.c_str());
        return;
    }

    uint8_t dummy_hdr[44] = {0};
    fwrite(dummy_hdr, 1, 44, f);

    esp_opus_dec_cfg_t dec_cfg = {
        .sample_rate = 24000,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID,
        .self_delimited = false,
    };
    void* opus_dec = nullptr;
    esp_err_t err = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), &opus_dec);
    if (err != ESP_OK || !opus_dec) {
        ESP_LOGE(TAG, "Gagal membuka Opus decoder: %d", err);
        fclose(f);
        remove(file_path.c_str());
        return;
    }

    is_downloading_ = true;
    abort_download_ = false;

    // Masuk ke MODE UNDUH LAGU eksklusif (tidak berjalan di latar belakang)
    app.CloseAudioChannel(); // Tutup audio channel mic ke server agar tidak menyela
    app.GetAudioService().EnableWakeWordDetection(false); // Nonaktifkan wake word selama download
    app.SetDeviceState(kDeviceStateSpeaking); // Kunci state ke speaking/busy agar fokus pada layar
    if (display) {
        display->SetStatus("⬇️ MODE UNDUH LAGU");
        display->SetEmotion("thinking");
    }
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    esp_wifi_set_ps(WIFI_PS_NONE);

    Board::GetInstance().ShowDownloadProgress(safe_title.c_str(), 0, "Menghubungkan ke server...");

    auto network = Board::GetInstance().GetNetwork();
    auto http = network ? network->CreateHttp(0) : nullptr;
    if (!http) {
        ESP_LOGE(TAG, "Gagal membuat HTTP client");
        fclose(f);
        esp_opus_dec_close(opus_dec);
        remove(file_path.c_str());
        is_downloading_ = false;
        Board::GetInstance().CloseDownloadProgress(false);
        if (display) {
            display->SetStatus("❌ UNDUHAN GAGAL");
            display->SetEmotion("sad");
        }
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        app.GetAudioService().EnableWakeWordDetection(true);
        app.Schedule([&app]() { app.SetDeviceState(kDeviceStateIdle); });
        return;
    }

    char url_buf[256];
    std::string mac = SystemInfo::GetMacAddress();
    std::string chip = CONFIG_IDF_TARGET;
    snprintf(url_buf, sizeof(url_buf), "%s/api/audio/stream/%s?br=40k&mac=%s&chip=%s",
             GetStreamUrl().c_str(), video_id.c_str(), mac.c_str(), chip.c_str());

    http->SetTimeout(12000);
    http->SetHeader("User-Agent", "ESP32-XiaoZhi");
    http->SetHeader("Device-Id", mac.c_str());
    http->SetHeader("X-Device-Mac", mac.c_str());
    http->SetHeader("X-Device-Chip", chip.c_str());

    ESP_LOGI(TAG, "Menghubungkan ke stream URL download: %s", url_buf);
    bool dl_opened = http->Open("GET", url_buf);
    int dl_status = dl_opened ? http->GetStatusCode() : 0;
    if (dl_status == 301 || dl_status == 302) {
        std::string loc = http->GetResponseHeader("Location");
        if (!loc.empty()) {
            http->Close();
            http->SetTimeout(12000);
            http->SetHeader("User-Agent", "ESP32-XiaoZhi");
            http->SetHeader("Device-Id", mac.c_str());
            http->SetHeader("X-Device-Mac", mac.c_str());
            http->SetHeader("X-Device-Chip", chip.c_str());
            dl_opened = http->Open("GET", loc);
            dl_status = dl_opened ? http->GetStatusCode() : 0;
        }
    }
    if (!dl_opened || dl_status != 200) {
        ESP_LOGE(TAG, "Koneksi stream download gagal (HTTP %d, opened=%d)", dl_status, (int)dl_opened);
        http->Close();
        fclose(f);
        esp_opus_dec_close(opus_dec);
        remove(file_path.c_str());
        is_downloading_ = false;
        Board::GetInstance().CloseDownloadProgress(false);
        if (display) {
            display->SetChatMessage("system", "❌ Koneksi unduhan gagal!");
            display->SetStatus("❌ UNDUHAN GAGAL");
            display->SetEmotion("sad");
        }
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        app.GetAudioService().EnableWakeWordDetection(true);
        app.Schedule([&app]() { app.SetDeviceState(kDeviceStateIdle); });
        return;
    }

    size_t stream_content_len = http->GetBodyLength();
    Board::GetInstance().ShowDownloadProgress(safe_title.c_str(), 5, "Mulai download stream...");

    static const size_t kOggCap = 4096;
    uint8_t* raw_buf = (uint8_t*)heap_caps_malloc(kOggCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw_buf) raw_buf = (uint8_t*)malloc(kOggCap);
    std::unique_ptr<uint8_t[], void(*)(void*)> ogg_buf(raw_buf, [](void* p) { free(p); });

    size_t ogg_len = 0;
    bool seen_head = false;
    bool seen_tags = false;
    uint32_t total_pcm_bytes = 0;
    size_t total_bytes_received = 0;
    int last_reported_pct = 5;
    uint32_t last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);
    static const uint8_t OGGS_MAGIC[4] = {'O', 'g', 'g', 'S'};
    std::vector<int16_t> pcm_out(2880);

    while (running_.load() && !abort_download_.load()) {
        size_t avail = kOggCap - ogg_len;
        if (avail == 0) {
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
            avail = kOggCap - ogg_len;
        }

        int to_read = (int)std::min((size_t)2048, avail);
        int n = http->Read(reinterpret_cast<char*>(ogg_buf.get() + ogg_len), to_read);
        if (n <= 0) {
            break;
        }
        ogg_len += n;
        total_bytes_received += n;

        // Hitung persentase dinamis dari 5% sampai 95%
        int cur_pct = 5;
        if (stream_content_len > 0) {
            cur_pct = 5 + (int)(((uint64_t)total_bytes_received * 90) / stream_content_len);
        } else {
            // Estimasi ~1.2MB total data stream Opus untuk 1 lagu
            size_t est_total = 1200 * 1024;
            cur_pct = 5 + (int)(((uint64_t)total_bytes_received * 90) / (total_bytes_received + est_total / 2));
        }
        if (cur_pct > 95) cur_pct = 95;

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (cur_pct != last_reported_pct && (now_ms - last_update_ms >= 200 || cur_pct == 95)) {
            last_reported_pct = cur_pct;
            last_update_ms = now_ms;
            char stat_buf[64];
            if (total_pcm_bytes >= 1024 * 1024) {
                snprintf(stat_buf, sizeof(stat_buf), "PCM: %.1f MB (%d%%)", (double)total_pcm_bytes / (1024.0 * 1024.0), cur_pct);
            } else {
                snprintf(stat_buf, sizeof(stat_buf), "PCM: %u KB (%d%%)", (unsigned)(total_pcm_bytes / 1024), cur_pct);
            }
            Board::GetInstance().ShowDownloadProgress(safe_title.c_str(), cur_pct, stat_buf);
        }

        // Parse Ogg pages
        while (ogg_len >= 27) {
            if (abort_download_.load()) break;

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

            uint8_t num_segs = ogg_buf[26];
            size_t hdr_len = 27 + num_segs;
            if (ogg_len < hdr_len) break;

            size_t body_len = 0;
            for (size_t i = 0; i < num_segs; ++i) {
                body_len += ogg_buf[27 + i];
            }
            size_t page_total = hdr_len + body_len;
            if (ogg_len < page_total) break;

            // Extract Opus packets
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

                if (pkt_len == 0) continue;
                const uint8_t* pkt_ptr = ogg_buf.get() + pkt_start;

                if (!seen_head) {
                    if (pkt_len >= 19 && memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                        seen_head = true;
                    }
                    continue;
                }
                if (!seen_tags) {
                    if (pkt_len >= 8 && memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                        seen_tags = true;
                    }
                    continue;
                }

                // Decode packet
                esp_audio_dec_in_raw_t raw = {
                    .buffer = const_cast<uint8_t*>(pkt_ptr),
                    .len = (uint32_t)pkt_len,
                    .consumed = 0,
                    .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
                };
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = (uint8_t*)pcm_out.data(),
                    .len = (uint32_t)(pcm_out.size() * sizeof(int16_t)),
                    .decoded_size = 0,
                };
                esp_audio_dec_info_t dec_info = {};
                if (esp_opus_dec_decode(opus_dec, &raw, &out_frame, &dec_info) == ESP_AUDIO_ERR_OK && out_frame.decoded_size > 0) {
                    fwrite(pcm_out.data(), 1, out_frame.decoded_size, f);
                    total_pcm_bytes += out_frame.decoded_size;
                }
            }

            memmove(ogg_buf.get(), ogg_buf.get() + page_total, ogg_len - page_total);
            ogg_len -= page_total;
        }
    }

    if (total_pcm_bytes > 0 && !abort_download_.load()) {
        uint8_t wav_header[44];
        memcpy(wav_header, "RIFF", 4);
        uint32_t file_size = 36 + total_pcm_bytes;
        memcpy(wav_header + 4, &file_size, 4);
        memcpy(wav_header + 8, "WAVEfmt ", 8);
        uint32_t subchunk1_size = 16;
        uint16_t audio_format = 1; // PCM
        uint16_t num_channels = 1; // Mono
        uint32_t sample_rate = 24000;
        uint32_t byte_rate = sample_rate * num_channels * sizeof(int16_t);
        uint16_t block_align = num_channels * sizeof(int16_t);
        uint16_t bits_per_sample = 16;
        memcpy(wav_header + 16, &subchunk1_size, 4);
        memcpy(wav_header + 20, &audio_format, 2);
        memcpy(wav_header + 22, &num_channels, 2);
        memcpy(wav_header + 24, &sample_rate, 4);
        memcpy(wav_header + 28, &byte_rate, 4);
        memcpy(wav_header + 32, &block_align, 2);
        memcpy(wav_header + 34, &bits_per_sample, 2);
        memcpy(wav_header + 36, "data", 4);
        memcpy(wav_header + 40, &total_pcm_bytes, 4);

        fseek(f, 0, SEEK_SET);
        fwrite(wav_header, 1, 44, f);
        fclose(f);
        ESP_LOGI(TAG, "Download selesai & tersimpan di: %s (%lu bytes PCM)", file_path.c_str(), (unsigned long)total_pcm_bytes);

        char finish_stat[64];
        if (total_pcm_bytes >= 1024 * 1024) {
            snprintf(finish_stat, sizeof(finish_stat), "100%% Sukses (%.2f MB)", (double)total_pcm_bytes / (1024.0 * 1024.0));
        } else {
            snprintf(finish_stat, sizeof(finish_stat), "100%% Sukses (%u KB)", (unsigned)(total_pcm_bytes / 1024));
        }
        Board::GetInstance().ShowDownloadProgress(safe_title.c_str(), 100, finish_stat);
        if (display) {
            display->SetStatus("✅ UNDUHAN SUKSES");
            display->SetEmotion("happy");
            display->ShowNotification("Unduhan Musik Selesai!", 4000);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        fclose(f);
        remove(file_path.c_str());
        ESP_LOGW(TAG, "Download dibatalkan atau data kosong, file dihapus");
        Board::GetInstance().CloseDownloadProgress(false);
        if (display) {
            display->SetStatus("❌ UNDUHAN GAGAL");
            display->SetEmotion("sad");
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    http->Close();
    esp_opus_dec_close(opus_dec);
    is_downloading_ = false;

    // Pulihkan mode daya, WiFi PS, wake word, dan kembalikan state ke Idle
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    app.GetAudioService().EnableWakeWordDetection(true);
    app.Schedule([&app]() {
        app.SetDeviceState(kDeviceStateIdle);
    });
}

std::vector<YouTubeAudioPoller::OnlineSongInfo> YouTubeAudioPoller::FetchOnlinePlaylist() {
    return {};
}


std::vector<YouTubeAudioPoller::OnlineSongInfo> YouTubeAudioPoller::GetCachedPlaylist() {
    return {};
}

void YouTubeAudioPoller::RefreshPlaylistAsync(std::function<void()> on_complete) {
    if (on_complete) {
        on_complete();
    }
}

