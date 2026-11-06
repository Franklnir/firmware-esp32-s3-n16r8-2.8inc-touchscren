#ifndef NOVA_DISPLAY_H
#define NOVA_DISPLAY_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <lvgl.h>
#include "theme_sync.h"

struct SdCardStatus {
    bool inserted = false;
    bool mounted = false;
    char card_name[16] = "";
    uint64_t total_mb = 0;
    uint64_t free_mb = 0;
    int file_count = 0;
    std::vector<std::pair<std::string, size_t>> files;
    std::string error = "";
};

struct OnlineSongItem {
    std::string video_id;
    std::string title;
    std::string duration;
};

struct NovaCallbacks {
    std::function<void(int mode)> switch_mode;
    std::function<void(int screen)> launch_chronchi_screen;
    std::function<void(int cmd)> ble_music_control;
    std::function<void(bool ring)> ble_find_phone;
    std::function<bool(std::string& title, std::string& artist, bool& playing)> get_ble_music;
    std::function<bool(std::string& dist, std::string& dir, std::string& title, std::string& dur, std::string& eta, std::string& spd, bool& active)> get_ble_nav;
    std::function<void(std::string& maneuver, bool& has_icon, const uint8_t*& icon_data)> get_ble_nav_extra;
    std::function<bool(float& temp, std::string& cond, int& hum)> get_ble_weather;
    std::function<bool(int& bat, bool& chg, std::string& net)> get_ble_phone;
    std::function<bool(std::string& app, std::string& title, std::string& msg, std::string& time)> get_ble_notif;
    std::function<bool()> ble_capture_photo;
    std::function<void()> ble_sync_request;
    std::function<int()> get_ble_notif_count;
    std::function<bool(int idx, std::string& app, std::string& title, std::string& msg, std::string& time)> get_ble_notif_at;
    std::function<void()> clear_ble_notifs;
    std::function<bool(float& temp, std::string& cond, int& hum, std::string& loc, int& high, int& low)> get_ble_weather_full;
    std::function<void(uint8_t brightness)> set_brightness;
    std::function<void(int volume)> set_volume;
    std::function<int()> get_volume;
    std::function<void(bool enable)> set_haptic;
    std::function<bool()> get_haptic;
    std::function<bool(int& level, bool& chg, int& mv, int& raw)> get_battery;
    std::function<bool(bool& connected, std::string& name, std::string& mac)> get_ble_status;
    std::function<SdCardStatus()> get_sd_status;
    std::function<bool()> remount_sd;
    std::function<bool()> format_sd;
    std::function<std::string(const std::string& path)> read_text_file;
    std::function<bool(const std::string& path)> play_audio;
    std::function<void()> stop_audio;
    std::function<bool()> is_audio_playing;
    std::function<void(bool repeat)> set_repeat;
    std::function<bool()> get_repeat;
    std::function<std::vector<OnlineSongItem>()> get_online_playlist;
    std::function<void(std::function<void()> on_done)> refresh_online_playlist;
    std::function<bool()> is_fetching_online_playlist;
    std::function<void(const std::string& video_id, const std::string& title, const std::string& duration)> play_online;
    std::function<void()> stop_online;
    std::function<bool()> is_online_playing;
    std::function<bool(const std::string& query, const std::string& title)> download_online;
    std::function<void()> trigger_click;
    std::function<void(int sound_id)> play_sound;
    std::function<float()> get_temperature;
};

class NovaDisplay {
public:
    enum Page : uint8_t {
        PAGE_SPLASH,
        PAGE_HOME,
        PAGE_APPS,
        PAGE_QUICK,
        PAGE_WIFI,
        PAGE_AI,
        PAGE_SETTINGS,
        PAGE_DISPLAY,
        PAGE_AUDIO,
        PAGE_FILES,
        PAGE_BATTERY,
        PAGE_REGION,
        PAGE_SYSTEM,
        PAGE_CHRONOS,
        PAGE_PINLAB,
        PAGE_PLAYLIST,
        PAGE_CALCULATOR,
        PAGE_FLAPPY,
        PAGE_RADIO,
        PAGE_2048,
        PAGE_SNAKE,
        PAGE_BRICK,
        PAGE_MINESWEEPER,
        PAGE_RACER,
        PAGE_PIANO,
        PAGE_SKETCH,
        PAGE_MONITOR,
        PAGE_NAV,
        PAGE_BT_MUSIC,
        PAGE_NOTIF,
        PAGE_WEATHER,
        PAGE_PHONE
    };

    enum IconKind : uint8_t {
        ICON_AI,
        ICON_WIFI,
        ICON_AUDIO,
        ICON_FILES,
        ICON_CHIP,
        ICON_CHRONOS,
        ICON_BRIGHTNESS,
        ICON_SETTINGS,
        ICON_HOME,
        ICON_APPS,
        ICON_MIC,
        ICON_BATTERY,
        ICON_REGION,
        ICON_CALC,
        ICON_GAME,
        ICON_RADIO,
        ICON_PIANO,
        ICON_DRAW,
        ICON_DASHBOARD,
        ICON_NAV,
        ICON_CHAT,
        ICON_WEATHER,
        ICON_PHONE
    };

    NovaDisplay(const NovaCallbacks& callbacks);
    ~NovaDisplay();

    void Init(int width, int height);
    void Update();

    void NavigateTo(Page next);
    void GoBack();
    void NavigateRoot(Page target);

    Page GetCurrentPage() const { return current_page_; }

    void StartWifiScan();
    void ShowWifiPasswordDialog(const std::string& ssid);
    void ShowWifiInputDialog(const std::string& default_ssid = "");
    void ConnectWifi(const std::string& ssid, const std::string& password);
    void ForgetWifi();
    void StartWebHotspot();
    void ShowBluetoothModal();
    void ApplyBrightness(uint8_t value, bool persist = true);
    void ToggleMute();
    void Toggle24Hour();
    void CycleLanguage();
    void CycleTimezone();
    void SyncNtpNow();
    int GetLanguage() const { return language_idx_; }
    int GetTimezone() const { return timezone_idx_; }
    const char* GetLanguageName() const;
    const char* GetTimezoneName() const;
    void TimerAction();
    void TimerReset();
    void ApplyTheme(const std::string& theme_name, bool persist = true);
    void CycleTheme();
    std::string GetCurrentThemeName() const;

    void ApplyBackground(const std::string& bg_id, bool persist = true);
    std::string GetCurrentBackgroundId() const;

    void ApplyTextColor(const std::string& text_color_id, bool persist = true);
    std::string GetCurrentTextColorId() const;
    void UpdateResolvedTextColor();

    void ShowRgbMixerModal();
    void CloseRgbMixerModal();
    void UpdateRgbMixerPreview();

    void ShowGamesModal();
    void CloseGamesModal();

    void ShowRecentAppsModal();
    void CloseRecentAppsModal();
    void CloseAllModals();

    void ShowDownloadModal(const std::string& title, int progress_pct, const std::string& status_text = "");
    void CloseDownloadModal(bool success = true);

    const NovaCallbacks& GetCallbacks() const { return callbacks_; }

    void OnCalcKey(const std::string& key);
    void FlappyReset();
    void FlappyFlap();
    void FlappyTick();
    void OnRadioStationSelect(int idx);

    enum GameSoundEffect : uint8_t {
        SOUND_PADDLE_HIT = 1,
        SOUND_BRICK_HIT = 2,
        SOUND_WALL_HIT = 3,
        SOUND_LOSE_BALL = 4,
        SOUND_WIN = 5,
        SOUND_MINE_CLICK = 6,
        SOUND_MINE_FLAG = 7,
        SOUND_MINE_EXPLODE = 8,
        SOUND_CAR_STEER = 9,
        SOUND_CAR_SCORE = 10,
        SOUND_CAR_CRASH = 11
    };
    void PlaySound(int sound_id) {
        if (callbacks_.play_sound) {
            callbacks_.play_sound(sound_id);
        }
    }

    enum SnakeDir : uint8_t { SNAKE_UP = 0, SNAKE_DOWN, SNAKE_LEFT, SNAKE_RIGHT };
    void Reset2048();
    bool Move2048(int dir);
    void ResetSnake();
    void StartSnake();
    void SnakeTick();
    void ChangeSnakeDir(SnakeDir new_dir);

    // Brick Breaker
    void ResetBrick();
    void StartBrick();
    void BrickTick();
    void SetPaddleX(float x);

    // Minesweeper
    void ResetMinesweeper();
    void OnMineTileClick(int r, int c);
    void ToggleMineMode();
    void MineTimerTick();

    // Highway Racer
    void ResetRacer();
    void StartRacer();
    void RacerTick();
    void ChangeRacerLane(int delta);
    void SpawnTrafficCar(int i, float min_y);

    // Piano
    void BuildPiano();
    void PlayPianoNote(int note_idx);
    void SetPianoInstrument(int mode);

    // Sketchpad
    void BuildSketch();
    void ClearSketchpad();
    void SetSketchColor(lv_color_t color);
    void SetSketchBrushSize(int sz);
    void ToggleSketchEraser();

    // Resource Monitor
    void BuildMonitor();
    void MonitorTick();

    enum BatteryStyle : uint8_t {
        BAT_STYLE_ICON_ONLY = 0,     // Icon batrai saja
        BAT_STYLE_ICON_PERCENT = 1,  // Icon batrai + status persentase angka
        BAT_STYLE_PERCENT_ONLY = 2   // Status persentase angka saja
    };

    void SetBatteryStyle(uint8_t style);
    uint8_t GetBatteryStyle() const { return bat_style_; }

    void ShowNotificationBanner(const std::string& app, const std::string& title, const std::string& message);
    void DismissNotificationBanner();
    void ShowOtpHighlight(const std::string& service, const std::string& otp_code, const std::string& full_message);
    void DismissOtpHighlight();
    void ShowNotificationDetail(int notif_idx);
    void DismissNotificationDetail();

    void ShowBtMusicModal();
    void ShowNavModal();
    void ShowNotifCenterModal();
    void ShowWeatherModal();
    void ShowPhoneModal();

    void UpdateNavData(const std::string& dist, const std::string& dir, const std::string& title,
                       const std::string& dur, const std::string& eta, const std::string& spd, bool active,
                       const std::string& maneuver = "", bool has_icon = false, const uint8_t* icon_data = nullptr);
    void UpdateBtMusicData(const std::string& title, const std::string& artist, bool playing);
    void ShowIncomingCallAlert(const std::string& caller, bool active);
    void ShowFindWatchAlert(bool active);
    void UpdateWeatherFull(float temp, const std::string& cond, int hum, const std::string& loc, int high, int low);
    void UpdatePhoneStatus(int bat, bool chg, const std::string& net);
    void UpdateNotif();

private:
    NovaCallbacks callbacks_;
    int width_ = 240;
    int height_ = 320;
    Page current_page_ = PAGE_HOME;

    static constexpr uint8_t HISTORY_CAPACITY = 16;
    Page page_history_[HISTORY_CAPACITY];
    uint8_t history_depth_ = 0;

    uint8_t brightness_ = 210;
    bool muted_ = false;
    int saved_volume_ = 75;
    bool use_24hour_ = true;
    bool ntp_started_ = false;
    int language_idx_ = 0;   // 0: Bahasa Indonesia, 1: English (US)
    int timezone_idx_ = 0;   // 0: WIB, 1: WITA, 2: WIT, 3: GMT, 4: JST, 5: EST

    // Chronos focus timer
    bool timer_running_ = false;
    uint16_t timer_remaining_seconds_ = 25 * 60;
    uint32_t timer_end_at_ = 0;

    // Wi-Fi state
    struct ScannedAp {
        std::string ssid;
        int rssi;
        bool open;
    };
    int scan_state_ = -1; // -1: never scanned, 0: scanning, 1: complete
    std::vector<ScannedAp> scanned_aps_;
    std::string selected_ssid_;
    std::string wifi_message_ = "Belum ada pemindaian jaringan.";
    bool wifi_connect_pending_ = false;
    uint32_t wifi_connect_started_at_ = 0;
    bool wifi_modal_open_ = false;

    // Live labels & Status bar handles
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* timer_label_ = nullptr;
    lv_obj_t* brightness_value_label_ = nullptr;
    lv_obj_t* battery_raw_label_ = nullptr;
    lv_obj_t* battery_percent_label_ = nullptr;
    lv_obj_t* battery_volt_label_ = nullptr;
    lv_obj_t* battery_state_label_ = nullptr;
    lv_obj_t* wifi_status_label_ = nullptr;
    lv_obj_t* wifi_modal_ = nullptr;
    lv_obj_t* wifi_ssid_input_ = nullptr;
    lv_obj_t* wifi_password_input_ = nullptr;
    lv_obj_t* wifi_keyboard_ = nullptr;
    bool wifi_pwd_visible_ = true;

    // Region & Language live preview labels
    lv_obj_t* region_time_preview_ = nullptr;
    lv_obj_t* region_date_preview_ = nullptr;
    lv_obj_t* region_ntp_status_ = nullptr;

    // Top status bar handles (identik XiaoZhi)
    lv_obj_t* top_bluetooth_icon_ = nullptr;
    lv_obj_t* top_notif_btn_ = nullptr;
    lv_obj_t* top_notif_icon_ = nullptr;
    lv_obj_t* top_notif_label_ = nullptr;
    lv_obj_t* top_wifi_icon_ = nullptr;
    lv_obj_t* top_mute_icon_ = nullptr;
    lv_obj_t* top_battery_icon_ = nullptr;
    lv_obj_t* top_battery_label_ = nullptr;
    lv_obj_t* bt_modal_ = nullptr;
    uint8_t bat_style_ = BAT_STYLE_ICON_PERCENT;

    // Audio page handles
    lv_obj_t* audio_volume_label_ = nullptr;
    lv_obj_t* audio_volume_slider_ = nullptr;

    // Recent Apps Modal (Android Task Switcher)
    lv_obj_t* recent_apps_modal_ = nullptr;

    lv_obj_t* NewRoot();
    void ClearDecoration(lv_obj_t* obj);
    lv_obj_t* MakePanel(lv_obj_t* parent, int x, int y, int width, int height,
                        lv_color_t fill, lv_color_t border, int radius = 14);
    lv_obj_t* MakeLabel(lv_obj_t* parent, const char* text,
                        int x, int y, int width, int height,
                        const lv_font_t* font, lv_color_t color,
                        lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
    lv_obj_t* MakeCenteredLabel(lv_obj_t* parent, const char* text,
                                int x, int y, int width, int height,
                                const lv_font_t* font, lv_color_t color);
    void StyleButton(lv_obj_t* button, lv_color_t fill, lv_color_t border, int radius = 12);
    lv_obj_t* MakeButton(lv_obj_t* parent, const char* text,
                         int x, int y, int width, int height,
                         lv_color_t fill, lv_color_t border, lv_color_t text_color,
                         const lv_font_t* font,
                         lv_event_cb_t callback = nullptr, void* user_data = nullptr);
    lv_obj_t* IconShape(lv_obj_t* parent, int x, int y, int width, int height,
                        lv_color_t color, int radius = 3, bool outline = false, int outline_width = 1);
    lv_obj_t* MakeIcon(lv_obj_t* parent, IconKind kind, int x, int y, lv_color_t color, int size = 26);
    void MakeHeader(lv_obj_t* root, const char* title, bool show_back = true);
    void MakeBottomNav(lv_obj_t* root, Page active);

    void AppTile(lv_obj_t* root, int x, int y, const char* name,
                 const char* description, IconKind icon, lv_color_t accent, Page destination);
    void ChronchiTile(lv_obj_t* root, int x, int y, const char* name,
                     const char* description, IconKind icon, lv_color_t accent, int screen);
    void SettingsRow(lv_obj_t* root, int y, const char* name,
                     const char* detail, IconKind icon, lv_color_t accent, Page target);
    lv_obj_t* MakeBrightnessSlider(lv_obj_t* parent, int x, int y, int width);

    void BuildSplash();
    void BuildHome();
    void BuildApps();
    void BuildQuick();
    void BuildWifi();
    void BuildAi();
    void BuildSettings();
    void BuildDisplay();
    void BuildAudio();
    void BuildFiles();
    void BuildBattery();
    void BuildRegion();
    void BuildSystem();
    void BuildChronos();
    void BuildPinLab();
    void BuildPlaylist();
    void BuildCalculator();
    void BuildFlappy();
    void BuildRadio();
    void Build2048();
    void BuildSnake();
    void BuildBrick();
    void BuildMinesweeper();
    void BuildRacer();
    void BuildNav();
    void BuildBtMusic();
    void BuildNotif();
    void BuildWeather();
    void BuildPhone();
    void BuildCurrentPage();

    void ShowFileModal(const std::string& filename, size_t size);
    void ShowRebootModal();
    lv_obj_t* file_modal_ = nullptr;
    std::string current_audio_file_ = "";

    void ShowOnlinePlayerModal(const std::string& title, const std::string& video_id, const std::string& duration = "");
    void ShowAudioPlayerModal(const std::string& title, const std::string& source, bool is_online, size_t file_size = 0, const std::string& duration = "");
    lv_obj_t* online_modal_ = nullptr;



    lv_obj_t* notif_banner_ = nullptr;
    lv_obj_t* call_alert_modal_ = nullptr;
    lv_obj_t* find_watch_modal_ = nullptr;
    lv_timer_t* notif_banner_timer_ = nullptr;

    lv_obj_t* notif_detail_modal_ = nullptr;
    lv_obj_t* otp_modal_ = nullptr;
    lv_obj_t* otp_countdown_label_ = nullptr;
    lv_timer_t* otp_timer_ = nullptr;
    int otp_remaining_sec_ = 30;

    lv_obj_t* bt_music_modal_ = nullptr;
    lv_obj_t* bt_music_title_ = nullptr;
    lv_obj_t* bt_music_artist_ = nullptr;
    lv_obj_t* bt_music_play_lbl_ = nullptr;

    lv_obj_t* nav_modal_ = nullptr;
    lv_obj_t* nav_modal_dist_ = nullptr;
    lv_obj_t* nav_modal_dir_ = nullptr;
    lv_obj_t* nav_modal_title_ = nullptr;
    uint8_t* nav_icon_buf_ = nullptr;
    lv_image_dsc_t nav_icon_dsc_;
    std::string last_nav_maneuver_ = "";
    bool last_nav_has_icon_ = false;
    uint8_t last_nav_icon_data_[288] = {0};
    void RenderNavIcon(const std::string& maneuver, const std::string& directions, bool has_icon, const uint8_t* icon_data);

    int file_hub_filter_ = 1; // 1: Folder Musik (/sdcard/music), 0: Semua Berkas (/sdcard)

    lv_obj_t* dl_modal_ = nullptr;
    lv_obj_t* dl_card_ = nullptr;
    lv_obj_t* dl_bar_ = nullptr;
    lv_obj_t* dl_pct_label_ = nullptr;
    lv_obj_t* dl_status_label_ = nullptr;
    lv_obj_t* dl_title_label_ = nullptr;
    lv_obj_t* dl_folder_label_ = nullptr;
    lv_obj_t* dl_icon_label_ = nullptr;
    lv_obj_t* dl_hdr_label_ = nullptr;
    lv_timer_t* dl_timer_ = nullptr;

    void UpdateLiveLabels();

    // --- Calculator App State ---
    std::string calc_expr_ = "";
    std::string calc_val_ = "0";
    double calc_accum_ = 0.0;
    std::string calc_pending_op_ = "";
    bool calc_reset_next_ = false;
    bool calc_has_result_ = false;
    lv_obj_t* calc_expr_lbl_ = nullptr;
    lv_obj_t* calc_val_lbl_ = nullptr;

    // --- Flappy Bird Game State ---
    enum FlappyState : uint8_t { FLAPPY_IDLE, FLAPPY_RUNNING, FLAPPY_OVER };
    FlappyState flappy_state_ = FLAPPY_IDLE;
    lv_timer_t* flappy_timer_ = nullptr;
    int flappy_score_ = 0;
    int flappy_best_ = 0;
    float flappy_bird_y_ = 100.0f;
    float flappy_bird_vy_ = 0.0f;
    float flappy_pipe_x_ = 240.0f;
    float flappy_pipe_gap_y_ = 80.0f;
    lv_obj_t* flappy_box_ = nullptr;
    lv_obj_t* flappy_bird_ = nullptr;
    lv_obj_t* flappy_pipe_top_ = nullptr;
    lv_obj_t* flappy_pipe_bot_ = nullptr;
    lv_obj_t* flappy_score_lbl_ = nullptr;
    lv_obj_t* flappy_best_lbl_ = nullptr;
    lv_obj_t* flappy_card_ = nullptr;
    lv_obj_t* flappy_card_title_ = nullptr;
    lv_obj_t* flappy_card_sub_ = nullptr;
    lv_obj_t* flappy_card_btn_ = nullptr;

    // --- Radio Streaming State ---
    int radio_active_idx_ = -1;

    // --- 2048 Game State ---
    int board_2048_[4][4] = {{0}};
    int score_2048_ = 0;
    int best_2048_ = 0;
    bool game_over_2048_ = false;
    bool won_2048_ = false;
    lv_obj_t* tiles_2048_[4][4] = {{nullptr}};
    lv_obj_t* tile_labels_2048_[4][4] = {{nullptr}};
    lv_obj_t* score_lbl_2048_ = nullptr;
    lv_obj_t* best_lbl_2048_ = nullptr;
    lv_obj_t* card_2048_ = nullptr;
    lv_obj_t* card_title_2048_ = nullptr;
    lv_obj_t* card_sub_2048_ = nullptr;
    lv_obj_t* card_btn_2048_ = nullptr;
    lv_point_t touch_start_2048_ = {0, 0};

    void SpawnTile2048();
    bool CanMove2048();
    void UpdateUI2048();

    // --- Retro Snake Game State ---
    enum SnakeState : uint8_t { SNAKE_IDLE = 0, SNAKE_RUNNING, SNAKE_OVER };
    struct SnakePoint { int8_t x; int8_t y; };
    static constexpr int SNAKE_GRID_W = 21;
    static constexpr int SNAKE_GRID_H = 15;
    static constexpr int SNAKE_MAX_SEGS = 48;
    SnakePoint snake_body_[SNAKE_MAX_SEGS];
    int snake_len_ = 3;
    SnakeDir snake_dir_ = SNAKE_RIGHT;
    SnakeDir snake_next_dir_ = SNAKE_RIGHT;
    SnakePoint snake_food_ = {10, 7};
    SnakeState snake_state_ = SNAKE_IDLE;
    int snake_score_ = 0;
    int snake_best_ = 0;
    lv_timer_t* snake_timer_ = nullptr;
    lv_obj_t* snake_arena_ = nullptr;
    lv_obj_t* snake_segs_[SNAKE_MAX_SEGS] = {nullptr};
    lv_obj_t* snake_food_obj_ = nullptr;
    lv_obj_t* snake_score_lbl_ = nullptr;
    lv_obj_t* snake_best_lbl_ = nullptr;
    lv_obj_t* snake_card_ = nullptr;
    lv_obj_t* snake_card_title_ = nullptr;
    lv_obj_t* snake_card_sub_ = nullptr;
    lv_obj_t* snake_card_btn_ = nullptr;
    lv_point_t touch_start_snake_ = {0, 0};

    void SpawnSnakeFood();
    void UpdateSnakeUI();

    // --- Brick Breaker State ---
    enum BrickState : uint8_t { BRICK_IDLE, BRICK_RUNNING, BRICK_OVER, BRICK_WON };
    BrickState brick_state_ = BRICK_IDLE;
    int brick_score_ = 0;
    int brick_best_ = 0;
    int brick_lives_ = 3;
    float ball_x_ = 106.0f;
    float ball_y_ = 175.0f;
    float ball_vx_ = 2.4f;
    float ball_vy_ = -3.0f;
    float paddle_x_ = 88.0f;
    bool bricks_[5][6];
    int bricks_left_ = 30;
    lv_timer_t* brick_timer_ = nullptr;
    lv_obj_t* brick_arena_ = nullptr;
    lv_obj_t* brick_ball_obj_ = nullptr;
    lv_obj_t* brick_paddle_obj_ = nullptr;
    lv_obj_t* brick_objs_[5][6] = {{nullptr}};
    lv_obj_t* brick_score_lbl_ = nullptr;
    lv_obj_t* brick_lives_lbl_ = nullptr;
    lv_obj_t* brick_best_lbl_ = nullptr;
    lv_obj_t* brick_card_ = nullptr;
    lv_obj_t* brick_card_title_ = nullptr;
    lv_obj_t* brick_card_sub_ = nullptr;
    lv_obj_t* brick_card_btn_ = nullptr;

    void UpdateBrickUI();

    // --- Minesweeper State ---
    enum MineState : uint8_t { MINE_PLAYING, MINE_WON, MINE_LOST };
    static constexpr int MINE_ROWS = 8;
    static constexpr int MINE_COLS = 8;
    static constexpr int MINE_TOTAL_MINES = 10;
    MineState mine_state_ = MINE_PLAYING;
    bool mine_grid_[MINE_ROWS][MINE_COLS];
    uint8_t mine_adjacent_[MINE_ROWS][MINE_COLS];
    bool mine_revealed_[MINE_ROWS][MINE_COLS];
    bool mine_flagged_[MINE_ROWS][MINE_COLS];
    bool mine_flag_mode_ = false;
    bool mine_first_click_ = true;
    int mine_flags_left_ = 10;
    int mine_time_sec_ = 0;
    lv_timer_t* mine_timer_ = nullptr;
    lv_obj_t* mine_btns_[MINE_ROWS][MINE_COLS] = {{nullptr}};
    lv_obj_t* mine_lbls_[MINE_ROWS][MINE_COLS] = {{nullptr}};
    lv_obj_t* mine_face_btn_ = nullptr;
    lv_obj_t* mine_face_lbl_ = nullptr;
    lv_obj_t* mine_count_lbl_ = nullptr;
    lv_obj_t* mine_timer_lbl_ = nullptr;
    lv_obj_t* mine_mode_btn_ = nullptr;

    void RevealMine(int r, int c);
    void ToggleMineFlag(int r, int c);
    void CheckMineWin();
    void UpdateMinesweeperUI();

    // --- Retro Highway Racer State ---
    enum RacerState : uint8_t { RACER_IDLE, RACER_RUNNING, RACER_CRASHED };
    RacerState racer_state_ = RACER_IDLE;
    int racer_player_lane_ = 1;
    int racer_score_ = 0;
    int racer_best_ = 0;
    int racer_speed_ = 80;
    struct TrafficCar {
        int lane;
        float y;
        float speed;
        lv_color_t color;
        bool active;
    };
    static constexpr int MAX_TRAFFIC = 3;
    TrafficCar traffic_[MAX_TRAFFIC];
    float road_stripe_y_ = 0.0f;
    lv_timer_t* racer_timer_ = nullptr;
    lv_obj_t* racer_road_ = nullptr;
    lv_obj_t* racer_player_car_ = nullptr;
    lv_obj_t* racer_traffic_cars_[MAX_TRAFFIC] = {nullptr};
    lv_obj_t* racer_stripes_[8] = {nullptr};
    lv_obj_t* racer_speed_lbl_ = nullptr;
    lv_obj_t* racer_score_lbl_ = nullptr;
    lv_obj_t* racer_best_lbl_ = nullptr;
    lv_obj_t* racer_card_ = nullptr;
    lv_obj_t* racer_card_title_ = nullptr;
    lv_obj_t* racer_card_sub_ = nullptr;
    lv_obj_t* racer_card_btn_ = nullptr;

    void UpdateRacerUI();

    // --- Piano State ---
    int piano_instrument_ = 0; // 0: Piano, 1: 8-Bit, 2: Organ
    lv_obj_t* piano_status_lbl_ = nullptr;
    lv_obj_t* piano_inst_btns_[3] = {nullptr};
    lv_obj_t* piano_white_keys_[8] = {nullptr};
    lv_obj_t* piano_black_keys_[5] = {nullptr};

    // --- Sketchpad State ---
    lv_obj_t* sketch_canvas_ = nullptr;
    lv_color_t* sketch_buf_ = nullptr;
    int sketch_buf_w_ = 0;
    int sketch_buf_h_ = 0;
    lv_color_t sketch_color_ = lv_color_hex(0xFFFFFF);
    int sketch_brush_size_ = 2;
    bool sketch_eraser_ = false;
    lv_point_t sketch_prev_pt_ = {-1, -1};
    lv_obj_t* sketch_tool_lbl_ = nullptr;
    lv_obj_t* sketch_size_btn_ = nullptr;

    // --- Resource Monitor State ---
    lv_timer_t* monitor_timer_ = nullptr;
    lv_obj_t* mon_cpu_lbl_ = nullptr;
    lv_obj_t* mon_sram_bar_ = nullptr;
    lv_obj_t* mon_sram_lbl_ = nullptr;
    lv_obj_t* mon_psram_bar_ = nullptr;
    lv_obj_t* mon_psram_lbl_ = nullptr;
    lv_obj_t* mon_temp_lbl_ = nullptr;
    lv_obj_t* mon_wifi_lbl_ = nullptr;
    lv_obj_t* mon_uptime_lbl_ = nullptr;

    // --- Display Page Scroll Offsets ---
    int display_scroll_y_ = 0;
    int display_pal_scroll_x_ = 0;
    int display_bg_scroll_x_ = 0;
    int display_tc_scroll_x_ = 0;

    // --- RGB Mixer Modal State ---
    lv_obj_t* rgb_modal_ = nullptr;
    lv_obj_t* rgb_preview_box_ = nullptr;
    lv_obj_t* rgb_hex_lbl_ = nullptr;
    lv_obj_t* rgb_slider_r_ = nullptr;
    lv_obj_t* rgb_slider_g_ = nullptr;
    lv_obj_t* rgb_slider_b_ = nullptr;
    lv_obj_t* rgb_val_r_ = nullptr;
    lv_obj_t* rgb_val_g_ = nullptr;
    lv_obj_t* rgb_val_b_ = nullptr;
    uint8_t mixer_r_ = 20;
    uint8_t mixer_g_ = 30;
    uint8_t mixer_b_ = 50;

    // --- Games Hub Modal State ---
    lv_obj_t* games_modal_ = nullptr;
};

#endif // NOVA_DISPLAY_H
