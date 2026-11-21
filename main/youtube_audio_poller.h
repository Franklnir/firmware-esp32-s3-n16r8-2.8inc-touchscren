#ifndef YOUTUBE_AUDIO_POLLER_H_
#define YOUTUBE_AUDIO_POLLER_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <atomic>
#include <mutex>

class YouTubeAudioPoller {
public:
    static YouTubeAudioPoller& GetInstance();

    void Start();
    static std::string GetServerDomain();
    static void InvalidateServerDomainCache();
    static std::string GetBaseUrl();
    static std::string GetStreamUrl();
    void Stop();
    bool IsPlaying() const { return is_playing_.load(); }
    void Abort();
    void TriggerFetch();
    void PlaySong(const std::string& video_id, const std::string& title, const std::string& duration = "");
    void PlayQuery(const std::string& query);
    bool SearchSong(const std::string& query, std::string& video_id, std::string& title, std::string& duration);
    bool FetchSongInfo(const std::string& query, std::string& video_id, std::string& title, std::string& duration) {
        return SearchSong(query, video_id, title, duration);
    }
    bool HasPendingSong();
    void ClearPendingSong() {
        std::lock_guard<std::mutex> lock(song_mutex_);
        has_pending_confirmation_ = false;
        pending_video_id_.clear();
        pending_title_.clear();
        pending_duration_.clear();
    }

private:
    YouTubeAudioPoller();
    ~YouTubeAudioPoller();

    void TaskLoop();
    bool FetchDirect(const std::string& query, std::string& video_id, std::string& title, std::string& duration);
    bool FetchDirect(const std::string& query, std::string& video_id, std::string& title);
    bool FetchCommands(int& cmd_id, std::string& video_id, std::string& title, std::string& duration);
    bool FetchCommands(int& cmd_id, std::string& video_id, std::string& title);
    bool AckCommand(int cmd_id);
    bool ReportStatus(const std::string& status, const std::string& video_id = "");
    void StreamAudio(const std::string& video_id, const std::string& title, const std::string& duration = "");

    TaskHandle_t task_handle_ = nullptr;
    StackType_t* task_stack_ = nullptr;
    StaticTask_t* task_buffer_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> abort_requested_{false};
    std::atomic<bool> trigger_requested_{false};
    std::atomic<bool> play_requested_{false};
    std::atomic<bool> query_requested_{false};
    std::atomic<bool> has_pending_confirmation_{false};
    std::mutex song_mutex_;
    std::string pending_video_id_;
    std::string pending_title_;
    std::string pending_duration_;
    std::string pending_query_;
};

#endif // YOUTUBE_AUDIO_POLLER_H_
