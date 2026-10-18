#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <lvgl.h>

// Forward declarations from chronchi_ble.h
struct ChronchiNotification;
struct ChronchiNavigation;
struct ChronchiWeather;
struct ChronchiPhoneStatus;
struct ChronchiMusic;

class ChronchiDisplay {
public:
    enum ScreenState { HOME, NAVIGATION, NOTIFICATION, WEATHER, MUSIC, STATUS, ALERT, QR_PAIRING };
    bool google_maps_active_ = false;
    uint32_t theme_color_ = 0x00E5FF;
    uint32_t current_layout_ = 0;
    bool light_mode_ = false;

    using MusicControlCallback = std::function<void(int cmd)>;
    using CameraCaptureCallback = std::function<void()>;
    using FindPhoneCallback = std::function<void(bool active)>;

    ChronchiDisplay();
    ~ChronchiDisplay();

    void init(int width, int height);
    void showHome();
    void showQrPairing();
    void setBleInfo(const char* name, const char* mac, const char* qr_data = nullptr);
    bool isBtConnected() const { return last_bt_connected_; }
    void showNavigation(const ChronchiNavigation& nav);
    void setNavIcon(const char* maneuver);
    void showNotification(const ChronchiNotification& notif);
    void showWeather(const ChronchiWeather& weather);
    void showPhoneStatus(const ChronchiPhoneStatus& status);
    void showMusic(const ChronchiMusic& music);
    void showFindWatchAlert(bool active);
    void showCameraPrompt(bool ready);
    void showAlarmAlert(int hour, int minute);
    void showReminderAlert(const char* title, const char* message);
    void set24HourFormat(bool is_24h);
    bool is24HourFormat() const { return is_24h_format_; }
    void updateTime(int hour, int minute, int second);
    void updateBattery(uint8_t level, bool charging);
    void updateWifi(bool connected);
    void updateBtStatus(bool connected);
    void updateHomeWeather(const ChronchiWeather& weather);
    void setWatchfaceTheme(uint32_t color, uint32_t layout);
    void toggleColorMode();
    void setColorMode(bool light);
    bool isLightMode() const { return light_mode_; }
    void cycleTheme();
    void setTheme(int theme);
    void setThemeColor(uint32_t rgb);
    void cycleScreen();
    void setNavigationIcon(uint8_t* icon_data, int size);
    void setScreenState(ScreenState s);
    ScreenState getScreenState() const { return state_; }

    void setMusicControlCallback(MusicControlCallback cb) { music_ctrl_cb_ = std::move(cb); }
    void setCameraCaptureCallback(CameraCaptureCallback cb) { camera_capture_cb_ = std::move(cb); }
    void setFindPhoneCallback(FindPhoneCallback cb) { find_phone_cb_ = std::move(cb); }

    void ShowRecentAppsModal();
    void CloseRecentAppsModal();
    bool IsRecentAppsVisible() const { return recent_apps_modal_ != nullptr; }
    void updateResolution();

    void showVolumeOverlay();
    void showBrightnessOverlay();
    void hideOverlays();

private:
    void createHomeScreen();
    void createQrScreen();
    void renderQrCode(const char* text);
    void createNavScreen();
    void createNotifScreen();
    void createWeatherScreen();
    void createMusicScreen();
    void createStatusScreen();
    void createAlertScreen();
    void createNavbar();
    void applyColorMode();
    void hideAllScreens();
    void showScreen(lv_obj_t* screen);

    int width_ = 240;
    int height_ = 280;
    ScreenState state_ = HOME;
    ScreenState prev_state_ = HOME;
    bool is_24h_format_ = true;

    // Home screen (borderless authentic Chronchi watchface)
    lv_obj_t* home_screen_ = nullptr;
    lv_obj_t* home_border_ = nullptr;
    lv_obj_t* home_line_ = nullptr;
    lv_obj_t* home_chronchi_card_ = nullptr;
    lv_obj_t* home_chronchi_label_ = nullptr;
    lv_obj_t* home_time_card_ = nullptr;
    lv_obj_t* home_time_label_ = nullptr;
    lv_obj_t* home_date_card_ = nullptr;
    lv_obj_t* home_date_label_ = nullptr;
    lv_obj_t* home_bt_card_ = nullptr;
    lv_obj_t* home_bt_label_ = nullptr;
    lv_obj_t* home_batt_card_ = nullptr;
    lv_obj_t* home_batt_label_ = nullptr;
    lv_obj_t* home_batt_bar_ = nullptr;
    lv_obj_t* home_batt_bar_fill_ = nullptr;
    lv_obj_t* home_status_card_ = nullptr;
    lv_obj_t* home_status_label_ = nullptr;

    // Nav screen
    lv_obj_t* nav_screen_ = nullptr;
    lv_obj_t* nav_header_card_ = nullptr;
    lv_obj_t* nav_header_label_ = nullptr;
    lv_obj_t* nav_time_label_ = nullptr;
    lv_obj_t* nav_main_card_ = nullptr;
    lv_obj_t* nav_icon_img_ = nullptr;
    lv_obj_t* nav_title_label_ = nullptr;
    lv_obj_t* nav_street_label_ = nullptr;
    lv_obj_t* nav_stats_card_ = nullptr;
    lv_obj_t* nav_dist_label_ = nullptr;
    lv_obj_t* nav_dur_label_ = nullptr;
    lv_obj_t* nav_eta_label_ = nullptr;
    lv_obj_t* nav_speed_label_ = nullptr;

    // Notif screen
    lv_obj_t* notif_screen_ = nullptr;
    lv_obj_t* notif_header_badge_ = nullptr;
    lv_obj_t* notif_app_label_ = nullptr;
    lv_obj_t* notif_time_label_ = nullptr;
    lv_obj_t* notif_card_ = nullptr;
    lv_obj_t* notif_line_ = nullptr;
    lv_obj_t* notif_title_label_ = nullptr;
    lv_obj_t* notif_msg_label_ = nullptr;
    lv_obj_t* notif_icon_label_ = nullptr;
    lv_timer_t* notif_timer_ = nullptr;

    // Weather screen
    lv_obj_t* weather_screen_ = nullptr;
    lv_obj_t* weather_header_card_ = nullptr;
    lv_obj_t* weather_header_label_ = nullptr;
    lv_obj_t* weather_main_card_ = nullptr;
    lv_obj_t* weather_temp_label_ = nullptr;
    lv_obj_t* weather_cond_label_ = nullptr;
    lv_obj_t* weather_range_label_ = nullptr;
    lv_obj_t* weather_stats_card_ = nullptr;
    lv_obj_t* weather_humid_label_ = nullptr;
    lv_obj_t* weather_press_label_ = nullptr;
    lv_obj_t* weather_loc_card_ = nullptr;
    lv_obj_t* weather_loc_label_ = nullptr;

    // Music screen
    lv_obj_t* music_screen_ = nullptr;
    lv_obj_t* music_header_card_ = nullptr;
    lv_obj_t* music_back_btn_ = nullptr;
    lv_obj_t* music_header_label_ = nullptr;
    lv_obj_t* music_card_ = nullptr;
    lv_obj_t* music_app_label_ = nullptr;
    lv_obj_t* music_state_label_ = nullptr;
    lv_obj_t* music_title_label_ = nullptr;
    lv_obj_t* music_artist_label_ = nullptr;
    lv_obj_t* music_recom_label_ = nullptr;
    lv_obj_t* music_bar_ = nullptr;
    lv_obj_t* music_bar_fill_ = nullptr;
    lv_obj_t* music_controls_row_ = nullptr;
    lv_obj_t* music_prev_btn_ = nullptr;
    lv_obj_t* music_play_btn_ = nullptr;
    lv_obj_t* music_play_label_ = nullptr;
    lv_obj_t* music_next_btn_ = nullptr;
    lv_obj_t* music_vol_row_ = nullptr;
    lv_obj_t* music_vol_down_btn_ = nullptr;
    lv_obj_t* music_vol_up_btn_ = nullptr;
    lv_obj_t* music_hint_card_ = nullptr;
    lv_obj_t* music_hint_label_ = nullptr;

    // Status screen (phone battery, network, etc.)
    lv_obj_t* status_screen_ = nullptr;
    lv_obj_t* status_header_card_ = nullptr;
    lv_obj_t* status_header_label_ = nullptr;
    lv_obj_t* status_bat_card_ = nullptr;
    lv_obj_t* status_phone_bat_title_ = nullptr;
    lv_obj_t* status_phone_bat_label_ = nullptr;
    lv_obj_t* status_bat_bar_ = nullptr;
    lv_obj_t* status_bat_bar_fill_ = nullptr;
    lv_obj_t* status_net_card_ = nullptr;
    lv_obj_t* status_network_label_ = nullptr;
    lv_obj_t* status_signal_label_ = nullptr;
    lv_obj_t* status_net_hint_label_ = nullptr;
    lv_obj_t* status_find_phone_btn_ = nullptr;
    lv_obj_t* status_find_phone_lbl_ = nullptr;
    lv_obj_t* status_bt_card_ = nullptr;
    lv_obj_t* status_bt_label_ = nullptr;

    // Interactive callbacks
    MusicControlCallback music_ctrl_cb_;
    CameraCaptureCallback camera_capture_cb_;
    FindPhoneCallback find_phone_cb_;
    bool find_phone_active_ = false;
    bool is_camera_mode_ = false;

    // Alert screen (Find Watch, Alarm, Camera trigger)
    lv_obj_t* alert_screen_ = nullptr;
    lv_obj_t* alert_card_ = nullptr;
    lv_obj_t* alert_icon_label_ = nullptr;
    lv_obj_t* alert_title_label_ = nullptr;
    lv_obj_t* alert_msg_label_ = nullptr;
    lv_obj_t* alert_btn_ = nullptr;
    lv_obj_t* alert_btn_label_ = nullptr;

    // Nav icon buffer
    uint8_t* nav_icon_buf_ = nullptr;
    lv_image_dsc_t nav_icon_dsc_{};
    std::string last_maneuver_;

    // QR Pairing / Barcode screen
    lv_obj_t* qr_screen_ = nullptr;
    lv_obj_t* qr_header_card_ = nullptr;
    lv_obj_t* qr_header_label_ = nullptr;
    lv_obj_t* qr_card_ = nullptr;
    lv_obj_t* qr_code_obj_ = nullptr;
    uint8_t* qr_img_buf_ = nullptr;
    lv_image_dsc_t qr_img_dsc_{};
    lv_obj_t* qr_info_card_ = nullptr;
    lv_obj_t* qr_name_label_ = nullptr;
    lv_obj_t* qr_mac_label_ = nullptr;
    lv_obj_t* qr_clock_btn_ = nullptr;

    std::string ble_device_name_ = "Chronchi";
    std::string ble_mac_address_ = "00:00:00:00:00:00";
    std::string qr_data_ = "https://chronos.ke/";

    bool last_bt_connected_ = false;
    uint8_t last_batt_level_ = 100;
    bool last_batt_charging_ = false;

    // Unified Chronchi Top Navbar (visible on every screen via lv_layer_top)
    lv_obj_t* chronchi_navbar_ = nullptr;
    lv_obj_t* navbar_batt_label_ = nullptr;
    lv_obj_t* navbar_bt_btn_ = nullptr;
    lv_obj_t* navbar_bt_label_ = nullptr;
    lv_obj_t* navbar_vol_btn_ = nullptr;
    lv_obj_t* navbar_vol_label_ = nullptr;
    lv_obj_t* navbar_wifi_btn_ = nullptr;
    lv_obj_t* navbar_wifi_label_ = nullptr;
    lv_obj_t* navbar_xz_btn_ = nullptr;
    lv_obj_t* navbar_br_btn_ = nullptr;
    lv_obj_t* navbar_th_btn_ = nullptr;
    lv_obj_t* navbar_th_lbl_ = nullptr;

    // Unified Chronchi Bottom Navigation Bar (layaknya HP: Back, Home, Recent Apps)
    lv_obj_t* chronchi_bottom_nav_ = nullptr;
    lv_obj_t* recent_apps_modal_ = nullptr;
    void createBottomNav();

    // Nova-Styled Overlays (Volume & Brightness Slider Cards)
    lv_obj_t* overlay_backdrop_ = nullptr;
    lv_obj_t* vol_overlay_card_ = nullptr;
    lv_obj_t* vol_overlay_slider_ = nullptr;
    lv_obj_t* vol_overlay_label_ = nullptr;
    lv_obj_t* vol_overlay_icon_ = nullptr;
    lv_obj_t* bright_overlay_card_ = nullptr;
    lv_obj_t* bright_overlay_slider_ = nullptr;
    lv_obj_t* bright_overlay_label_ = nullptr;
};
