#ifndef YOUTUBE_AUDIO_POLLER_H_
#define YOUTUBE_AUDIO_POLLER_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>

class YouTubeAudioPoller {
public:
    static YouTubeAudioPoller& GetInstance();

    void Start();
    static void SetServerDomain(const std::string& domain);
    static std::string GetServerDomain();
    static std::string GetBaseUrl();
    static std::string GetStreamUrl();
    void Stop();
    bool IsPlaying() const { return is_playing_.load(); }
    void Abort();
    void TriggerFetch();
    void PlaySong(const std::string& video_id, const std::string& title, const std::string& duration = "");
    void PlayQuery(const std::string& query);
    bool FetchSongInfo(const std::string& query, std::string& video_id, std::string& title, std::string& duration);
    struct OnlineSongInfo {
        std::string video_id;
        std::string title;
        std::string duration;
    };

    bool DownloadSong(const std::string& query_or_video_id, const std::string& title);
    bool IsDownloading() const { return is_downloading_.load(); }
    void AbortDownload();
    std::vector<OnlineSongInfo> FetchOnlinePlaylist();
    std::vector<OnlineSongInfo> GetCachedPlaylist();
    void RefreshPlaylistAsync(std::function<void()> on_complete = nullptr);
    bool IsFetchingPlaylist() const { return is_fetching_playlist_.load(); }

private:
    YouTubeAudioPoller();
    ~YouTubeAudioPoller();

    void TaskLoop();
    bool FetchDirect(const std::string& query, std::string& video_id, std::string& title);
    bool FetchCommands(int& cmd_id, std::string& video_id, std::string& title);
    bool AckCommand(int cmd_id);
    bool ReportStatus(const std::string& status, const std::string& video_id = "");
    void StreamAudio(const std::string& video_id, const std::string& title, const std::string& duration = "");
    void ExecuteDownload(const std::string& video_id, const std::string& title);

    TaskHandle_t task_handle_ = nullptr;
    StackType_t* task_stack_ = nullptr;
    StaticTask_t* task_buffer_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> is_playing_{false};
    std::atomic<bool> abort_requested_{false};
    std::atomic<bool> trigger_requested_{false};
    std::atomic<bool> play_requested_{false};
    std::atomic<bool> query_requested_{false};
    std::atomic<bool> download_requested_{false};
    std::atomic<bool> is_downloading_{false};
    std::atomic<bool> abort_download_{false};
    std::mutex song_mutex_;
    std::string pending_video_id_;
    std::string pending_title_;
    std::string pending_duration_;
    std::string pending_query_;
    std::string pending_dl_video_id_;
    std::string pending_dl_title_;

    std::vector<OnlineSongInfo> cached_playlist_;
    std::mutex playlist_mutex_;
    std::atomic<bool> is_fetching_playlist_{false};
    std::atomic<bool> has_real_playlist_{false};
};

#endif // YOUTUBE_AUDIO_POLLER_H_
