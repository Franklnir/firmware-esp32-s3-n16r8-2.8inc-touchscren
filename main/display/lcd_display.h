#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <atomic>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* xiaozhi_bottom_nav_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    bool is_zoomed_ = false;  // Camera zoom state (backwards compatibility)
    int zoom_level_ = 1;      // Camera zoom level (1x to 10x)

    // DJ Music visualizer
    lv_obj_t* music_container_ = nullptr;
    lv_obj_t* dj_icon_label_ = nullptr;
    lv_obj_t* music_title_label_ = nullptr;
    lv_obj_t* music_sub_label_ = nullptr;
    lv_obj_t* music_duration_label_ = nullptr;
    lv_obj_t* dj_bars_[7] = {nullptr};
    lv_timer_t* music_anim_timer_ = nullptr;
    int music_anim_phase_ = 0;
    bool music_visualizer_active_ = false;

    int ui_theme_id_ = 2;
    bool is_st7789_ = false;
    lv_obj_t* avatar_circle_ = nullptr;

    void SetupUI_Theme1();
    void SetupUI_Theme2();
    void DestroyUI();
    void SetChatMessage_Theme1(const char* role, const char* content);
    void SetChatMessage_Theme2(const char* role, const char* content);
    void SetPreviewImage_Theme1(std::unique_ptr<LvglImage> image);
    void SetPreviewImage_Theme2(std::unique_ptr<LvglImage> image);
    void ClearChatMessages_Theme1();
    void ClearChatMessages_Theme2();

    void CreateMusicVisualizerUI();
    void CreateBottomNav();

    // Quick Settings Control Center (Jendela Setengah Layar)
    lv_obj_t* cp_backdrop_ = nullptr;
    lv_obj_t* cp_drawer_ = nullptr;
    lv_obj_t* cp_wifi_label_ = nullptr;
    lv_obj_t* cp_text_size_btn_ = nullptr;
    lv_obj_t* cp_text_size_label_ = nullptr;
    lv_obj_t* cp_mode_btn_ = nullptr;
    lv_obj_t* cp_mode_label_ = nullptr;
    lv_obj_t* cp_vol_slider_ = nullptr;
    lv_obj_t* cp_vol_label_ = nullptr;
    lv_obj_t* cp_vol_icon_ = nullptr;
    lv_obj_t* cp_bright_slider_ = nullptr;
    lv_obj_t* cp_bright_label_ = nullptr;
    lv_obj_t* cp_theme_icon_btn_ = nullptr;
    lv_obj_t* cp_theme_icon_label_ = nullptr;
    lv_obj_t* cp_color_box_ = nullptr;
    bool cp_visible_ = false;
    uint32_t cp_theme_last_click_ = 0;
    int cp_theme_color_idx_ = 0;

    // Chat Font Size Control (0: 14px, 1: 16px, 2: 20px, 3: 30px)
    int chat_font_level_ = 1;
    lv_obj_t* text_size_overlay_card_ = nullptr;
    lv_obj_t* text_size_val_label_ = nullptr;

    // Direct Overlay Sliders for Volume & Brightness & Text Size
    lv_obj_t* overlay_backdrop_ = nullptr;
    lv_obj_t* vol_overlay_card_ = nullptr;
    lv_obj_t* vol_overlay_slider_ = nullptr;
    lv_obj_t* vol_overlay_label_ = nullptr;
    lv_obj_t* vol_overlay_icon_ = nullptr;

    lv_obj_t* bright_overlay_card_ = nullptr;
    lv_obj_t* bright_overlay_slider_ = nullptr;
    lv_obj_t* bright_overlay_label_ = nullptr;

    bool overlay_visible_ = false;

    // Modern Idle Dashboard (Jam, Hari, Tanggal, Bulan, Tahun, Suhu, Cuaca)
    lv_obj_t* idle_dashboard_ = nullptr;
    lv_obj_t* idle_time_label_ = nullptr;
    lv_obj_t* idle_sec_label_ = nullptr;
    lv_obj_t* idle_date_card_ = nullptr;
    lv_obj_t* idle_date_label_ = nullptr;
    lv_obj_t* idle_weather_card_ = nullptr;
    lv_obj_t* idle_weather_icon_ = nullptr;
    lv_obj_t* idle_temp_label_ = nullptr;
    lv_obj_t* idle_cond_label_ = nullptr;
    lv_obj_t* idle_loc_label_ = nullptr;
    lv_obj_t* idle_hint_card_ = nullptr;
    lv_obj_t* idle_footer_label_ = nullptr;
    bool idle_dashboard_visible_ = false;

    void CreateIdleDashboard();
    void UpdateIdleDashboard();

    // Wi-Fi Touchscreen Configuration UI
    lv_obj_t* wifi_modal_ = nullptr;
    lv_obj_t* wifi_modal_backdrop_ = nullptr;
    lv_obj_t* wifi_list_view_ = nullptr;
    lv_obj_t* wifi_list_container_ = nullptr;
    lv_obj_t* wifi_scan_status_label_ = nullptr;
    lv_obj_t* wifi_ref_btn_ = nullptr;
    lv_obj_t* wifi_pwd_view_ = nullptr;
    lv_obj_t* wifi_pwd_ssid_label_ = nullptr;
    lv_obj_t* wifi_pwd_ta_ = nullptr;
    lv_obj_t* wifi_pwd_eye_btn_ = nullptr;
    lv_obj_t* wifi_pwd_eye_icon_ = nullptr;
    lv_obj_t* wifi_keyboard_ = nullptr;
    lv_obj_t* wifi_connecting_view_ = nullptr;
    lv_obj_t* wifi_connecting_label_ = nullptr;
    bool wifi_modal_visible_ = false;
    bool wifi_pwd_visible_ = true;
    std::atomic<bool> wifi_scanning_{false};
    std::string wifi_selected_ssid_;

    // Recent Apps Modal (Smartphone Task Switcher in XiaoZhi)
    lv_obj_t* recent_apps_modal_ = nullptr;

    void CreateWifiModal();
    void StartWifiScan();
    void PopulateWifiScanResults(const std::vector<std::tuple<std::string, int, bool>>& aps);
    void ShowWifiPasswordInput(const std::string& ssid);
    void ConnectToWifi(const std::string& ssid, const std::string& password);

    void CreateDirectOverlays();
    void CreateControlCenter();
    void UpdateControlCenterValues();
    void UpdateTextSizeOverlayUI();

    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height);

public:
    ~LcdDisplay();
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    void ShowCaptureFlash();
    void SetZoom(bool zoomed);
    void SetZoomLevel(int level);
    int GetZoomLevel() const { return zoom_level_; }
    int ZoomIn();
    int ZoomOut();
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
    virtual void SetBrightness(uint8_t brightness) override;

    void ShowWifiConfigModal();
    void HideWifiConfigModal();

    virtual int GetUiThemeId() const override { return ui_theme_id_; }
    virtual bool SetUiThemeId(int theme_id) override;
    virtual void SetIsSt7789(bool is_st7789) override { is_st7789_ = is_st7789; }
    virtual bool IsSt7789() const override { return is_st7789_; }

    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);

    // DJ Music Visualizer
    virtual void StartMusicVisualizer(const char* title = nullptr) override;
    virtual void StopMusicVisualizer() override;
    virtual void UpdateMusicProgress(int current_sec, int total_sec) override;
    virtual bool IsMusicVisualizerActive() const override { return music_visualizer_active_; }
    void AnimateMusicVisualizer();

    // Control Center (Jendela Setengah Layar)
    void ToggleControlCenter();
    void HideControlCenter();
    bool IsControlCenterVisible() const { return cp_visible_; }

    // Direct Quick Overlay Sliders
    void ShowVolumeOverlay();
    void ShowBrightnessOverlay();
    void ShowTextSizeOverlay();
    void HideOverlays();
    bool IsOverlayVisible() const { return overlay_visible_; }

    // Recent Apps Modal (Smartphone Task Switcher in XiaoZhi)
    void ShowRecentAppsModal();
    void CloseRecentAppsModal();
    bool IsRecentAppsModalVisible() const { return recent_apps_modal_ != nullptr; }

    void SetChatFontLevel(int level);
    int GetChatFontLevel() const { return chat_font_level_; }
    const lv_font_t* GetChatFont() const;
    void UpdateResolution();
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
};

#endif  // LCD_DISPLAY_H
