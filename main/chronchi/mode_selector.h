#pragma once

#include <esp_log.h>
#include <driver/gpio.h>
#include <lvgl.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "settings.h"

enum class DeviceMode {
    XIAOZHI = 0,
    CHRONCHI = 1
};

struct SelectorData {
    gpio_num_t gpio;         // GPIO 0 = switch
    gpio_num_t confirm_gpio; // GPIO 46 = confirm
    int width, height;
    DeviceMode selected;
    DeviceMode current_mode;
    lv_obj_t* overlay;
    lv_obj_t* xz_box, *xz_label;
    lv_obj_t* ch_box, *ch_label;
    lv_obj_t* instr;
    int hold_count;
    bool button_was_pressed;
    lv_timer_t* timer;
    bool confirmed;
};

class ModeSelector {
public:
    // Quick boot check (non-blocking)
    static DeviceMode CheckBoot(gpio_num_t button_gpio) {
        Settings settings("chronchi", true);

        // Selalu default ke mode XIAOZHI agar asisten XiaoZhi aktif
        settings.SetInt("mode", (int32_t)DeviceMode::XIAOZHI);
        ESP_LOGI("ModeSelector", "NVS boot mode: XIAOZHI");
        return DeviceMode::XIAOZHI;
    }

    // Launch mode selector UI using LVGL timer (runs in LVGL context)
    static void LaunchSelector(gpio_num_t switch_gpio, gpio_num_t confirm_gpio, int width, int height, DeviceMode current_mode) {
        ESP_LOGI("ModeSelector", "Launching 2-mode selector (%dx%d)...", width, height);
        
        auto* data = new SelectorData();
        data->gpio = switch_gpio;
        data->confirm_gpio = confirm_gpio;
        data->width = width;
        data->height = height;
        data->selected = current_mode;
        data->current_mode = current_mode;
        data->hold_count = 0;
        data->button_was_pressed = false;
        data->timer = nullptr;
        data->confirmed = false;
        
        // Sleek dark theme overlay on active screen
        data->overlay = lv_obj_create(lv_screen_active());
        lv_obj_set_size(data->overlay, width, height);
        lv_obj_align(data->overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(data->overlay, lv_color_hex(0x0A0F1D), 0);
        lv_obj_set_style_bg_opa(data->overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(data->overlay, 0, 0);
        lv_obj_set_style_radius(data->overlay, 0, 0);
        lv_obj_set_style_pad_all(data->overlay, 0, 0);
        lv_obj_clear_flag(data->overlay, LV_OBJ_FLAG_SCROLLABLE);
        
        // Title (responsive spacing for 240x240 and 240x280)
        lv_obj_t* title = lv_label_create(data->overlay);
        lv_label_set_text(title, "SELECT OPERATING MODE");
        lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, (height >= 280) ? 24 : 16);
        
        int box_h = (height >= 280) ? 54 : 46;
        int box_w = width - 28;
        int offset_y1 = (height >= 280) ? -32 : -28;
        int offset_y2 = (height >= 280) ? 36 : 30;

        // 1. XiaoZhi box (Cyan)
        data->xz_box = lv_obj_create(data->overlay);
        lv_obj_set_size(data->xz_box, box_w, box_h);
        lv_obj_align(data->xz_box, LV_ALIGN_CENTER, 0, offset_y1);
        lv_obj_set_style_radius(data->xz_box, 10, 0);
        lv_obj_set_style_pad_all(data->xz_box, 0, 0);
        lv_obj_clear_flag(data->xz_box, LV_OBJ_FLAG_SCROLLABLE);
        
        data->xz_label = lv_label_create(data->xz_box);
        lv_label_set_text(data->xz_label, LV_SYMBOL_WIFI "  XIAOZHI [AI Voice]");
        lv_obj_set_style_text_font(data->xz_label, &lv_font_montserrat_14, 0);
        lv_obj_center(data->xz_label);
        
        // 2. Chronchi box (Emerald Green)
        data->ch_box = lv_obj_create(data->overlay);
        lv_obj_set_size(data->ch_box, box_w, box_h);
        lv_obj_align(data->ch_box, LV_ALIGN_CENTER, 0, offset_y2);
        lv_obj_set_style_radius(data->ch_box, 10, 0);
        lv_obj_set_style_pad_all(data->ch_box, 0, 0);
        lv_obj_clear_flag(data->ch_box, LV_OBJ_FLAG_SCROLLABLE);
        
        data->ch_label = lv_label_create(data->ch_box);
        lv_label_set_text(data->ch_label, LV_SYMBOL_BLUETOOTH "  CHRONCHI [Watch]");
        lv_obj_set_style_text_font(data->ch_label, &lv_font_montserrat_14, 0);
        lv_obj_center(data->ch_label);
        
        // Instruction at bottom
        data->instr = lv_label_create(data->overlay);
        lv_label_set_text(data->instr, "Short: Switch | Hold: Confirm");
        lv_obj_set_style_text_color(data->instr, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(data->instr, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(data->instr, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(data->instr, LV_ALIGN_BOTTOM_MID, 0, (height >= 280) ? -16 : -10);
        
        highlightSelection(data);
        
        // Setup confirm GPIO (46)
        if (confirm_gpio != GPIO_NUM_NC) {
            gpio_config_t confirm_conf = {};
            confirm_conf.pin_bit_mask = (1ULL << (int)confirm_gpio);
            confirm_conf.mode = GPIO_MODE_INPUT;
            confirm_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            confirm_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            confirm_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&confirm_conf);
        }

        // Setup switch GPIO
        if (switch_gpio != GPIO_NUM_NC) {
            gpio_config_t sw_conf = {};
            sw_conf.pin_bit_mask = (1ULL << (int)switch_gpio);
            sw_conf.mode = GPIO_MODE_INPUT;
            sw_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            sw_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            sw_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&sw_conf);
        }

        // LVGL timer polls button every 50ms
        data->timer = lv_timer_create([](lv_timer_t* t) {
            auto* d = static_cast<SelectorData*>(lv_timer_get_user_data(t));
            bool switch_pressed = (gpio_get_level(d->gpio) == 0);
            bool confirm_pressed = false;
            if (d->confirm_gpio != GPIO_NUM_NC && d->confirm_gpio != d->gpio) {
                confirm_pressed = (gpio_get_level(d->confirm_gpio) == 0);
            }
            
            // GPIO 0 = switch mode (short click) or confirm (long press >= 1.5s = 30 ticks)
            if (switch_pressed) {
                d->hold_count++;
                d->button_was_pressed = true;
                if (d->hold_count >= 30 && !d->confirmed) {
                    confirm_pressed = true;
                }
            } else {
                if (d->button_was_pressed && d->hold_count >= 2 && d->hold_count < 30 && !d->confirmed) {
                    // Short click = toggle between XIAOZHI and CHRONCHI
                    if (d->selected == DeviceMode::XIAOZHI) {
                        d->selected = DeviceMode::CHRONCHI;
                    } else {
                        d->selected = DeviceMode::XIAOZHI;
                    }
                    ESP_LOGI("ModeSelector", "Switched to: %s",
                             d->selected == DeviceMode::XIAOZHI ? "XIAOZHI" : "CHRONCHI");
                    highlightSelection(d);
                }
                d->hold_count = 0;
                d->button_was_pressed = false;
            }
            
            // Confirm selection
            if (confirm_pressed && !d->confirmed) {
                d->confirmed = true;
                ESP_LOGI("ModeSelector", "Confirmed: %s",
                         d->selected == DeviceMode::XIAOZHI ? "XIAOZHI" : "CHRONCHI");
                
                // Flash effect
                if (d->selected == DeviceMode::XIAOZHI) {
                    lv_obj_set_style_bg_color(d->xz_box, lv_color_hex(0x38BDF8), 0);
                    lv_obj_set_style_text_color(d->xz_label, lv_color_hex(0x000000), 0);
                } else {
                    lv_obj_set_style_bg_color(d->ch_box, lv_color_hex(0x34D399), 0);
                    lv_obj_set_style_text_color(d->ch_label, lv_color_hex(0x000000), 0);
                }
                
                lv_timer_delete(d->timer);
                d->timer = nullptr;
                
                // Save to NVS and restart after 500ms
                auto* dd = d;
                lv_timer_create([](lv_timer_t* t2) {
                    auto* d2 = static_cast<SelectorData*>(lv_timer_get_user_data(t2));
                    if (d2->overlay && lv_obj_is_valid(d2->overlay)) {
                        lv_obj_del(d2->overlay);
                    }
                    lv_timer_delete(t2);
                    
                    // Save mode to NVS
                    Settings settings("chronchi", true);
                    settings.SetInt("mode", (int32_t)d2->selected);
                    
                    delete d2;
                    
                    ESP_LOGI("ModeSelector", "Restarting...");
                    esp_restart();
                }, 500, dd);
                return;
            }
        }, 50, data);
    }

private:
    static void highlightSelection(SelectorData* d) {
        // Default styling for inactive boxes
        lv_obj_set_style_border_width(d->xz_box, 1, 0);
        lv_obj_set_style_border_color(d->xz_box, lv_color_hex(0x0369A1), 0);
        lv_obj_set_style_bg_color(d->xz_box, lv_color_hex(0x0C1929), 0);
        lv_obj_set_style_text_color(d->xz_label, lv_color_hex(0x38BDF8), 0);

        lv_obj_set_style_border_width(d->ch_box, 1, 0);
        lv_obj_set_style_border_color(d->ch_box, lv_color_hex(0x047857), 0);
        lv_obj_set_style_bg_color(d->ch_box, lv_color_hex(0x06231A), 0);
        lv_obj_set_style_text_color(d->ch_label, lv_color_hex(0x34D399), 0);

        // Highlight selected
        if (d->selected == DeviceMode::XIAOZHI) {
            lv_obj_set_style_border_width(d->xz_box, 3, 0);
            lv_obj_set_style_border_color(d->xz_box, lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_bg_color(d->xz_box, lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_text_color(d->xz_label, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_border_width(d->ch_box, 3, 0);
            lv_obj_set_style_border_color(d->ch_box, lv_color_hex(0x34D399), 0);
            lv_obj_set_style_bg_color(d->ch_box, lv_color_hex(0x059669), 0);
            lv_obj_set_style_text_color(d->ch_label, lv_color_hex(0xFFFFFF), 0);
        }
    }
};
