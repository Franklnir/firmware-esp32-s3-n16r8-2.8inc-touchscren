/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>

#include "application.h"
#include "display.h"
#include "lcd_display.h"
#include "oled_display.h"
#include "board.h"
#include "wifi_board.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"
#include "youtube_audio_poller.h"
#include "system_info.h"
#include <esp_heap_caps.h>
#include <dirent.h>
#include <sys/stat.h>

#define TAG "MCP"

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides real-time information of the device, including: battery level percent, charging status (sedang di-cas atau tidak), audio speaker volume, screen brightness, background color/theme, network status, etc.\n"
        "Use this tool for: \n"
        "1. Checking device status (e.g. status baterai, lagi di-cas atau tidak, warna background saat ini, volume, wifi)\n"
        "2. As the first step to control device settings.",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.battery.get_status",
        "Dapatkan status baterai saat ini: persentase daya baterai (0-100%), voltase, dan status apakah sedang di-cas (charging) atau tidak. "
        "Gunakan tool ini ketika pengguna menanyakan status baterai, sisa baterai, persentase baterai, atau apakah baterai sedang di-cas / charger dicolok atau dicabut (misal 'baterai lagi di cas gak', 'cek status baterai', 'sisa baterai berapa', 'apakah lagi ngecas').",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            int level = 0;
            bool charging = false, discharging = false;
            board.GetBatteryLevel(level, charging, discharging);

            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "battery_level_percent", level);
            cJSON_AddBoolToObject(root, "is_charging", charging);
            cJSON_AddStringToObject(root, "charging_status", charging ? "Sedang di-cas (Charging ⚡)" : "Tidak di-cas (Menggunakan daya baterai)");

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display) {
        AddTool("self.screen.switch_theme",
            "Ganti atau beralih tata letak tema tampilan layar (Thema 1: Tampilan Default Robot dengan emoji, Thema 2: Tampilan Chat Bubble WeChat / WhatsApp). "
            "Gunakan tool ini ketika pengguna meminta ganti tema, ubah tema, ganti thema, beralih tema, ganti ke tema 2, ganti ke tema 1, atau minta tampilkan tema seperti di gambar (misal 'ganti thema', 'ganti tema', 'ubah tema', 'ganti tema 2', 'pindah ke tema 1', 'pakai tema 2', 'tampilkan tema seperti di gambar').\n"
            "Args:\n"
            "  `theme_id`: (Opsional) Angka tema yang diinginkan (1 untuk Thema 1 Default Robot, 2 untuk Thema 2 Chat Bubble). Jika 0 atau tidak ditentukan, akan otomatis beralih ke tema berikutnya.",
            PropertyList({
                Property("theme_id", kPropertyTypeInteger, 0, 2)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                int cur = display->GetUiThemeId();
                int target = properties["theme_id"].value<int>();
                if (target <= 0) {
                    target = (cur == 1) ? 2 : 1;
                }
                bool ok = display->SetUiThemeId(target);
                if (ok) {
                    if (target == 2) {
                        return std::string("Berhasil berganti ke Thema 2 (Tampilan Chat Bubble WeChat / WhatsApp).");
                    } else {
                        return std::string("Berhasil berganti ke Thema 1 (Tampilan Default Robot dengan emoji).");
                    }
                }
                return std::string("Gagal mengganti tema layar.");
            });

        AddTool("self.screen.get_theme",
            "Dapatkan dan baca informasi tema layar saat ini secara lengkap: tata letak UI (Thema 1: Default Robot atau Thema 2: Chat Bubble WeChat seperti pada gambar), warna background/tema aktif (hitam, putih, biru, dll), dan tipe layar. "
            "Gunakan tool ini ketika pengguna meminta baca tema saat ini, tema saat ini apa, cek tema sekarang, tema berapa yang aktif, atau tanya tampilan apa yang sedang digunakan (misal 'baca thema saat ini', 'tema saat ini apa', 'cek tema', 'tema berapa sekarang', 'tampilan apa saat ini').",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                int ui_theme_id = display->GetUiThemeId();
                bool is_st7789 = display->IsSt7789();
                std::string ui_theme_name = (ui_theme_id == 2)
                    ? "Thema 2 (Tampilan Chat Bubble WeChat / WhatsApp)"
                    : "Thema 1 (Tampilan Default Robot dengan emoji)";

                std::string bg_name = "dark";
                std::string hex_color = "#000000";
                std::string bg_desc = "Hitam (Dark)";
                if (display->GetTheme()) {
                    bg_name = display->GetTheme()->name();
                    if (bg_name == "dark") { hex_color = "#000000"; bg_desc = "Hitam (Dark)"; }
                    else if (bg_name == "light") { hex_color = "#FFE600"; bg_desc = "Kuning (Cyber Yellow)"; }
                    else if (bg_name == "blue") { hex_color = "#0A2F6E"; bg_desc = "Biru (Midnight Navy)"; }
                    else if (bg_name == "red") { hex_color = "#7F1D1D"; bg_desc = "Merah (Crimson Red)"; }
                    else if (bg_name == "green") { hex_color = "#064E3B"; bg_desc = "Hijau (Emerald Green)"; }
                    else if (bg_name == "purple") { hex_color = "#4C1D95"; bg_desc = "Ungu (Royal Purple)"; }
                    else if (bg_name == "orange") { hex_color = "#7C2D12"; bg_desc = "Oranye (Sunset Orange)"; }
                    else if (bg_name == "white" || bg_name == "wechat") { hex_color = "#EBEBEB"; bg_desc = "Putih / Abu-abu Terang (WeChat Style)"; }
                    else if (bg_name == "pink") { hex_color = "#831843"; bg_desc = "Merah Muda (Hot Pink)"; }
                    else if (bg_name == "cyan") { hex_color = "#0E7490"; bg_desc = "Biru Laut / Toska (Cyan)"; }
                    else { bg_desc = bg_name; }
                }

                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "ui_theme_id", ui_theme_id);
                cJSON_AddStringToObject(root, "ui_theme_name", ui_theme_name.c_str());
                cJSON_AddStringToObject(root, "background_color", bg_desc.c_str());
                cJSON_AddStringToObject(root, "background_color_name", bg_name.c_str());
                cJSON_AddStringToObject(root, "hex_color", hex_color.c_str());
                cJSON_AddBoolToObject(root, "is_st7789", is_st7789);
                cJSON_AddStringToObject(root, "display_type", is_st7789 ? "ST7789" : "LCD");

                std::string summary = "Saat ini perangkat menggunakan " + ui_theme_name +
                                      " dengan warna background " + bg_desc + ".";
                cJSON_AddStringToObject(root, "summary", summary.c_str());

                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str ? json_str : "{}");
                if (json_str) free(json_str);
                cJSON_Delete(root);
                return result;
            });

        AddTool("self.screen.set_theme",
            "GANTI TEMA TAMPILAN / TATA LETAK LAYAR (Theme Layout Mode):\n"
            "- Tema 2 ('2' / 'chat' / 'whatsapp' / 'wechat' / 'gambar'): Mode Tampilan Chat WhatsApp / WeChat (gelembung balon pesan chat hijau untuk user di kanan, putih untuk asisten di kiri, avatar kuning di kiri atas, baterai di samping wifi di kanan atas, persis seperti di gambar).\n"
            "- Tema 1 ('1' / 'robot' / 'default'): Mode Tampilan Robot bawaan (emoji robot besar di tengah layar dan teks subtitle di bawah).\n"
            "GUNAKAN TOOL INI ketika pengguna meminta: 'ganti tema', 'ganti thema', 'tema 2', 'thema 2', 'ganti ke tema 2', 'tema whatsapp', 'tema seperti di whatsapp', 'tema seperti di gambar', 'tema chat', 'tema 1', 'ganti ke tema 1', 'tampilan robot'.\n"
            "PERINGATAN: JANGAN gunakan tool ini untuk mengganti warna background! Untuk ganti warna latar belakang gunakan `self.screen.set_background_color`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                std::string t_lower = theme_name;
                std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::tolower);

                int target_id = 2; // Default to Tema 2 (Chat WhatsApp)
                if (t_lower == "1" || t_lower == "tema 1" || t_lower == "thema 1" || t_lower == "theme 1" ||
                    t_lower == "tampilan 1" || t_lower == "default" || t_lower == "robot" || t_lower == "satu" ||
                    t_lower.find("tema 1") != std::string::npos || t_lower.find("robot") != std::string::npos) {
                    target_id = 1;
                } else if (t_lower == "switch" || t_lower == "toggle" || t_lower == "ganti" || t_lower.empty() ||
                           t_lower == "ganti tema" || t_lower == "ganti thema" || t_lower == "next") {
                    target_id = (display->GetUiThemeId() == 1) ? 2 : 1;
                } else {
                    target_id = 2;
                }

                bool ok = display->SetUiThemeId(target_id);
                if (ok) {
                    if (target_id == 2) {
                        return std::string("Berhasil berganti ke Tema 2 (Tampilan Chat Bubble seperti WhatsApp / di gambar).");
                    } else {
                        return std::string("Berhasil berganti ke Tema 1 (Tampilan Default Robot dengan emoji).");
                    }
                }
                return std::string("Gagal mengganti tema tampilan layar.");
            });

        AddTool("self.screen.set_background_color",
            "HANYA UNTUK GANTI WARNA LATAR BELAKANG / BACKGROUND SAJA (misal 'ganti warna background jadi hitam', 'ubah background jadi biru', 'ganti latar belakang merah'). "
            "Pilihan warna: hitam, putih, biru, merah, hijau, ungu, kuning, oranye, pink, cyan. "
            "PERINGATAN: JANGAN GUNAKAN TOOL INI jika pengguna meminta 'ganti tema', 'tema 2', 'thema 2', 'tema whatsapp', atau 'tema chat'!",
            PropertyList({
                Property("color", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                std::string color = properties["color"].value<std::string>();
                std::string col_lower = color;
                std::transform(col_lower.begin(), col_lower.end(), col_lower.begin(), ::tolower);

                // Intercept if AI mistakenly calls set_background_color when user asks to switch theme layout
                if (col_lower.find("tema 2") != std::string::npos || col_lower.find("thema 2") != std::string::npos ||
                    col_lower == "2" || col_lower.find("chat") != std::string::npos || col_lower.find("whatsapp") != std::string::npos ||
                    col_lower.find("wechat") != std::string::npos || col_lower.find("gambar") != std::string::npos) {
                    display->SetUiThemeId(2);
                    return std::string("Berhasil berganti ke Tema 2 (Tampilan Chat Bubble WhatsApp seperti pada gambar).");
                }
                if (col_lower.find("tema 1") != std::string::npos || col_lower.find("thema 1") != std::string::npos ||
                    col_lower == "1" || col_lower.find("robot") != std::string::npos) {
                    display->SetUiThemeId(1);
                    return std::string("Berhasil berganti ke Tema 1 (Tampilan Default Robot).");
                }
                if (col_lower.find("tema") != std::string::npos || col_lower.find("thema") != std::string::npos) {
                    int next_id = (display->GetUiThemeId() == 1) ? 2 : 1;
                    display->SetUiThemeId(next_id);
                    return std::string("Berhasil beralih tema ke Tema ") + std::to_string(next_id) +
                       (next_id == 2 ? " (Tampilan Chat Bubble WhatsApp seperti pada gambar)." : " (Tampilan Default Robot).");
                }

                std::string theme_name = col_lower;
                if (col_lower.empty() || col_lower == "acak" || col_lower == "random" || col_lower == "next" ||
                    col_lower == "ganti" || col_lower == "baground" || col_lower == "background" || col_lower == "lain") {
                    static const std::vector<std::string> themes = {"blue", "red", "green", "purple", "orange", "pink", "cyan", "white", "light", "dark"};
                    std::string cur_theme = display->GetTheme() ? display->GetTheme()->name() : "dark";
                    auto it = std::find(themes.begin(), themes.end(), cur_theme);
                    if (it != themes.end() && (it + 1) != themes.end()) {
                        theme_name = *(it + 1);
                    } else {
                        theme_name = themes[0];
                    }
                } else if (col_lower == "hitam" || col_lower == "black" || col_lower == "gelap") theme_name = "dark";
                else if (col_lower == "kuning" || col_lower == "yellow" || col_lower == "terang") theme_name = "light";
                else if (col_lower == "biru" || col_lower == "blue" || col_lower == "navy") theme_name = "blue";
                else if (col_lower == "merah" || col_lower == "red" || col_lower == "crimson") theme_name = "red";
                else if (col_lower == "hijau" || col_lower == "green" || col_lower == "emerald") theme_name = "green";
                else if (col_lower == "ungu" || col_lower == "purple" || col_lower == "violet") theme_name = "purple";
                else if (col_lower == "oranye" || col_lower == "orange" || col_lower == "jingga") theme_name = "orange";
                else if (col_lower == "putih" || col_lower == "white") theme_name = "white";
                else if (col_lower == "pink" || col_lower == "merah muda") theme_name = "pink";
                else if (col_lower == "cyan" || col_lower == "toska" || col_lower == "biru muda") theme_name = "cyan";

                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return std::string("Warna background layar berhasil diubah ke: ") + theme_name;
                }

                if (col_lower.rfind("#", 0) == 0 && col_lower.length() >= 7) {
                    auto custom_theme = theme_manager.GetTheme("custom");
                    if (!custom_theme) {
                        auto dark = theme_manager.GetTheme("dark");
                        custom_theme = new LvglTheme("custom");
                        if (dark) {
                            custom_theme->set_text_font(dark->text_font());
                            custom_theme->set_icon_font(dark->icon_font());
                            custom_theme->set_large_icon_font(dark->large_icon_font());
                            custom_theme->set_emoji_font(dark->emoji_font());
                        }
                        theme_manager.RegisterTheme("custom", custom_theme);
                    }
                    lv_color_t bg_col = LvglTheme::ParseColor(col_lower);
                    custom_theme->set_background_color(bg_col);
                    custom_theme->set_chat_background_color(bg_col);
                    custom_theme->set_text_color(lv_color_white());
                    custom_theme->set_user_bubble_color(lv_color_hex(0x00E5FF));
                    custom_theme->set_assistant_bubble_color(lv_color_hex(0x222222));
                    custom_theme->set_border_color(lv_color_hex(0x444444));
                    display->SetTheme(custom_theme);
                    return std::string("Warna background layar berhasil diubah ke kode HEX: ") + col_lower;
                }
                return std::string("Gagal mengubah warna background. Pilihan: hitam, putih, biru, merah, hijau, ungu, kuning, oranye, pink, cyan.");
            });

        AddTool("self.screen.get_background_color",
            "Dapatkan status tema dan warna background layar saat ini secara lengkap (tata letak tema aktif Thema 1 / Thema 2, nama warna, kode HEX). "
            "Gunakan tool ini ketika pengguna menanyakan tema apa atau warna background apa yang sedang dipakai (misal: 'baca tema saat ini', 'warna background saat ini apa', 'apa warna layar sekarang', 'cek tema sekarang').",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                int ui_theme_id = display->GetUiThemeId();
                bool is_st7789 = display->IsSt7789();
                std::string ui_theme_name = (ui_theme_id == 2)
                    ? "Thema 2 (Tampilan Chat Bubble WhatsApp seperti di gambar)"
                    : "Thema 1 (Tampilan Default Robot dengan emoji)";

                std::string theme_name = "dark";
                std::string hex_color = "#000000";
                std::string color_description = "Hitam (Dark)";

                if (display && display->GetTheme()) {
                    theme_name = display->GetTheme()->name();
                    if (theme_name == "dark") { hex_color = "#000000"; color_description = "Hitam (Dark)"; }
                    else if (theme_name == "light") { hex_color = "#FFE600"; color_description = "Kuning (Cyber Yellow)"; }
                    else if (theme_name == "blue") { hex_color = "#071329"; color_description = "Biru (Midnight Navy)"; }
                    else if (theme_name == "red") { hex_color = "#240505"; color_description = "Merah (Crimson Red)"; }
                    else if (theme_name == "green") { hex_color = "#041F10"; color_description = "Hijau (Emerald Green)"; }
                    else if (theme_name == "purple") { hex_color = "#19082B"; color_description = "Ungu (Royal Amethyst)"; }
                    else if (theme_name == "orange") { hex_color = "#261002"; color_description = "Oranye (Sunset Orange)"; }
                    else if (theme_name == "white") { hex_color = "#EBEBEB"; color_description = "Putih / Abu-abu Terang (WeChat Style)"; }
                    else if (theme_name == "pink") { hex_color = "#260717"; color_description = "Merah Muda (Hot Pink)"; }
                    else if (theme_name == "cyan") { hex_color = "#031B21"; color_description = "Biru Laut / Toska (Cyan)"; }
                    else {
                        color_description = theme_name;
                    }
                }

                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "ui_theme_id", ui_theme_id);
                cJSON_AddStringToObject(root, "ui_theme_name", ui_theme_name.c_str());
                cJSON_AddStringToObject(root, "current_color", color_description.c_str());
                cJSON_AddStringToObject(root, "theme_name", theme_name.c_str());
                cJSON_AddStringToObject(root, "hex_color", hex_color.c_str());
                cJSON_AddBoolToObject(root, "is_st7789", is_st7789);

                std::string summary = "Saat ini perangkat menggunakan " + ui_theme_name +
                                      " dengan warna background " + color_description +
                                      (is_st7789 ? " pada layar jenis ST7789." : ".");
                cJSON_AddStringToObject(root, "summary", summary.c_str());

                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str ? json_str : "{}");
                if (json_str) free(json_str);
                cJSON_Delete(root);
                return result;
            });

        AddTool("self.weather.set_weather",
            "Atur informasi cuaca, suhu, kota, atau kelembaban yang ditampilkan pada dashboard idle layar standby perangkat (misal: 'atur cuaca cerah suhu 28 derajat di Jakarta', 'set suhu 25', 'cuaca hari ini hujan di Bandung').",
            PropertyList({
                Property("city", kPropertyTypeString, std::string("")),
                Property("temperature", kPropertyTypeInteger, 999),
                Property("condition", kPropertyTypeString, std::string("")),
                Property("humidity", kPropertyTypeInteger, 999)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                Settings weather_settings("weather", true);
                std::string city = properties["city"].value<std::string>();
                int temp = properties["temperature"].value<int>();
                std::string cond = properties["condition"].value<std::string>();
                int humid = properties["humidity"].value<int>();

                if (!city.empty()) {
                    weather_settings.SetString("city", city);
                }
                if (temp != 999) {
                    weather_settings.SetInt("temp", temp);
                }
                if (!cond.empty()) {
                    weather_settings.SetString("condition", cond);
                }
                if (humid != 999) {
                    weather_settings.SetInt("humidity", humid);
                }

                if (display) {
                    display->UpdateStatusBar(true);
                }
                return true;
            });

        AddTool("self.weather.get_weather",
            "Dapatkan status cuaca, suhu, kota, dan kelembaban udara yang sedang ditampilkan di layar standby idle.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                Settings weather_settings("weather", true);
                int temp = weather_settings.GetInt("temp", 28);
                std::string cond = weather_settings.GetString("condition", "Cerah Berawan");
                std::string city = weather_settings.GetString("city", "Jakarta");
                int humid = weather_settings.GetInt("humidity", 72);

                cJSON* root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "city", city.c_str());
                cJSON_AddNumberToObject(root, "temperature", temp);
                cJSON_AddStringToObject(root, "condition", cond.c_str());
                cJSON_AddNumberToObject(root, "humidity", humid);

                char* json_str = cJSON_PrintUnformatted(root);
                std::string result(json_str ? json_str : "{}");
                if (json_str) free(json_str);
                cJSON_Delete(root);
                return result;
            });
    }
#endif

    AddTool("self.search_music",
        "CARI INFORMASI LAGU YOUTUBE (Search YouTube Song Info & Duration):\n"
        "Gunakan tool ini ketika pengguna meminta MEMUTAR ATAU MENDOWNLOAD lagu (seperti 'putar lagu [judul]', 'download lagu [judul]', 'simpan lagu [judul] ke memori', dsb.) untuk mencari tahu judul lengkap resmi dan durasi lagu di YouTube SEBELUM menanyakan konfirmasi ke pengguna.\n"
        "Tool ini menghubungi YouTube dan mengembalikan informasi lagu (judul resmi, durasi, video ID).\n"
        "Args:\n"
        "  `query`: Judul lagu atau kata kunci pencarian yang diminta pengguna (misal: 'alan walker faded', 'tulus teh hijau').\n"
        "Return:\n"
        "  JSON berisi 'title' (judul resmi lengkap), 'duration' (format menit:detik, misal '3:48' atau '3:33'), 'video_id', dan status pencarian.",
        PropertyList({Property("query", kPropertyTypeString)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto query = properties["query"].value<std::string>();
            std::string video_id, title, duration;
            bool ok = YouTubeAudioPoller::GetInstance().FetchSongInfo(query, video_id, title, duration);
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "found", ok);
            cJSON_AddStringToObject(root, "query", query.c_str());
            cJSON_AddStringToObject(root, "title", ok ? title.c_str() : query.c_str());
            cJSON_AddStringToObject(root, "duration", (!duration.empty()) ? duration.c_str() : "sekitar 3-4 menit");
            cJSON_AddStringToObject(root, "video_id", video_id.c_str());
            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool("self.play_music",
        "PUTAR MUSIK / LAGU DARI YOUTUBE (Play Music / YouTube Song):\n"
        "Gunakan tool ini untuk memutar lagu atau musik dari YouTube melalui speaker perangkat.\n"
        "PENTING (ALUR WAJIB KONFIRMASI DENGAN JUDUL & DURASI LAGU):\n"
        "1. KETIKA PENGGUNA MEMINTA MEMUTAR LAGU (misal: 'putar lagu Alan Walker Faded', 'setel lagu Tulus', 'play youtube', dsb.):\n"
        "   - JANGAN LANGSUNG MEMUTAR LAGU!\n"
        "   - Panggil tool 'self.search_music' terlebih dahulu untuk mencari tahu informasi lagu di YouTube (judul lengkap resmi dan durasinya).\n"
        "   - Setelah mendapatkan informasi lagu (atau jika kamu sudah tahu detail lagunya), tanyakan konfirmasi kepada pengguna dengan menyebutkan judul lengkap dan durasinya secara ramah, contoh:\n"
        "     'Apakah kamu mau memutar lagu berdurasi [durasi, misal: 3 menit 33 detik] dengan judul \"[Judul Lengkap Lagu]\" di YouTube? Jawab ya atau lanjut untuk memutar, atau batal untuk membatalkan.'\n"
        "2. HANYA PANGGIL TOOL 'self.play_music' SETELAH PENGGUNA MEMBERIKAN KONFIRMASI SETUJU (misal pengguna menjawab: 'ya', 'yes', 'putar', 'lanjut', 'oke', 'mainkan', 'silakan', 'setuju').\n"
        "3. JIKA PENGGUNA MEMBATALKAN (misal: 'batal', 'jangan', 'tidak', 'enggak jadi'), JANGAN panggil tool ini dan sampaikan bahwa pemutaran lagu dibatalkan.\n"
        "4. KETIKA LAGU SELESAI DIPUTAR, sistem perangkat akan otomatis memberi tahu bahwa lagu telah selesai. Tugasmu adalah langsung bicara menyapa pengguna dengan ramah, beritahukan bahwa lagunya sudah selesai diputar, lalu tanyakan tanggapan pengguna tentang lagu tersebut dan tawarkan apakah ingin melanjutkan pembahasan topik yang tadi dibicarakan atau memutar lagu lain.\n"
        "Args:\n"
        "  `query`: Judul lagu atau video ID yang telah dikonfirmasi oleh pengguna (misal: 'alan walker faded', '60ItHLz5WEA', 'tulus teh hijau').",
        PropertyList({Property("query", kPropertyTypeString)}),
        [](const PropertyList& properties) -> ReturnValue {
            auto query = properties["query"].value<std::string>();
            YouTubeAudioPoller::GetInstance().PlayQuery(query);
            return "Baik, lagu '" + query + "' sedang dicari dan segera diputar dari YouTube.";
        });

    AddTool("self.stop_music",
        "HENTIKAN / STOP PEMUTARAN MUSIK (Stop Music):\n"
        "Gunakan tool ini ketika pengguna meminta: 'stop lagu', 'hentikan lagu', 'matikan musik', 'berhenti memutar lagu', 'stop musik', atau 'hentikan pemutaran musik' (baik yang diputar dari YouTube maupun dari kartu memori SD).",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            YouTubeAudioPoller::GetInstance().Abort();
            board.StopWav();
            auto display = board.GetDisplay();
            if (display) {
                display->StopMusicVisualizer();
            }
            return "Pemutaran musik telah dihentikan.";
        });

    AddTool("self.sdcard.download_music",
        "DOWNLOAD LAGU YOUTUBE KE KARTU MEMORI (Download YouTube Song to SD Card):\n"
        "Gunakan tool ini untuk mengunduh lagu dari YouTube dan menyimpannya secara permanen ke folder '/sdcard/music/' di kartu memori agar dapat diputar offline kapan saja.\n"
        "PENTING (ALUR WAJIB KONFIRMASI DENGAN JUDUL & DURASI LAGU):\n"
        "1. KETIKA PENGGUNA MEMINTA MENDOWNLOAD / MENYIMPAN LAGU (misal: 'download lagu Alan Walker Faded', 'simpan lagu Tulus ke memori', 'unduh musik [judul]'):\n"
        "   - JANGAN LANGSUNG MEMANGGIL TOOL INI!\n"
        "   - LANGKAH 1: Panggil tool 'self.search_music' terlebih dahulu untuk mencari informasi lagu di YouTube (judul resmi lengkap dan durasinya).\n"
        "   - LANGKAH 2: Tanyakan konfirmasi kepada pengguna dengan menyebutkan judul lengkap dan durasinya secara ramah dan jelas, contoh:\n"
        "     'Saya menemukan lagu berdurasi [durasi, misal: 3 menit 32 detik] dengan judul \"[Judul Lengkap Resmi]\" di YouTube. Apakah Anda yakin ingin mendownload dan menyimpannya ke kartu memori? Jawab ya atau lanjut untuk mulai mendownload, atau batal untuk membatalkan.'\n"
        "   - LANGKAH 3: HANYA panggil tool 'self.sdcard.download_music' jika pengguna telah menjawab konfirmasi setuju (misal: 'ya', 'yes', 'download', 'unduh', 'simpan', 'lanjut', 'oke', 'silakan', 'setuju').\n"
        "2. JIKA PENGGUNA MEMBATALKAN (misal: 'batal', 'tidak', 'jangan', 'enggak jadi'), JANGAN panggil tool ini dan beritahukan bahwa download dibatalkan.\n"
        "3. SETELAH TOOL INI DIPANGGIL: JANGAN PERNAH mengatakan 'berjalan di latar belakang' atau 'di background'! Sampaikan secara ramah dan jelas bahwa Anda sedang mengunduh lagu tersebut langsung ke folder musik di kartu memori, dan pengguna dapat melihat animasi proses unduhan dari 0% sampai 100% langsung di layar perangkat hingga sukses.\n"
        "Args:\n"
        "  `query`: Judul lagu atau kata kunci atau video ID yang telah dikonfirmasi pengguna.\n"
        "  `title`: Judul resmi lengkap dari hasil pencarian (opsional).",
        PropertyList({
            Property("query", kPropertyTypeString),
            Property("title", kPropertyTypeString, "")
        }),
        [&board](const PropertyList& properties) -> ReturnValue {
            auto query = properties["query"].value<std::string>();
            std::string title = "";
            if (properties.Has("title")) {
                title = properties["title"].value<std::string>();
            }
            if (title.empty()) title = query;

            if (!board.IsSdCardMounted()) {
                return "Gagal: Kartu MicroSD belum terpasang atau tidak terbaca di perangkat. Harap pastikan kartu memori sudah terpasang.";
            }

            bool ok = YouTubeAudioPoller::GetInstance().DownloadSong(query, title);
            if (!ok) {
                return "Proses download lain sedang berlangsung di layar. Harap tunggu hingga selesai.";
            }
            return "Baik, saya sedang mengunduh lagu '" + title + "' langsung ke folder musik '/sdcard/music/' di kartu memori. Anda dapat melihat animasi proses unduhannya dari 0% sampai 100% langsung di layar perangkat.";
        });

    AddTool("self.sdcard.list_music",
        "LIHAT DAFTAR LAGU DI KARTU MEMORI (List Offline Songs on SD Card):\n"
        "Gunakan tool ini untuk melihat daftar semua file lagu / musik yang tersimpan di folder musik kartu memori ('/sdcard/music/' atau '/sdcard/').\n"
        "Panggil tool ini ketika pengguna bertanya: 'ada lagu apa saja di memori?', 'lihat lagu di kartu memori', 'daftar musik yang sudah didownload', 'cek lagu offline', dsb.",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            if (!board.IsSdCardMounted()) {
                return "Kartu MicroSD belum terpasang atau tidak terbaca di perangkat.";
            }

            cJSON* root = cJSON_CreateObject();
            cJSON* songs_arr = cJSON_CreateArray();
            int count = 0;

            auto scan_dir = [&](const char* dir_path) {
                DIR* dir = opendir(dir_path);
                if (dir) {
                    struct dirent* entry;
                    while ((entry = readdir(dir)) != nullptr) {
                        if (entry->d_name[0] == '.') continue;
                        std::string name = entry->d_name;
                        std::string ext = "";
                        size_t dot = name.rfind('.');
                        if (dot != std::string::npos) {
                            ext = name.substr(dot);
                            for (auto& c : ext) c = tolower(c);
                        }
                        if (ext == ".wav" || ext == ".mp3" || ext == ".pcm") {
                            count++;
                            cJSON* item = cJSON_CreateObject();
                            cJSON_AddNumberToObject(item, "no", count);
                            cJSON_AddStringToObject(item, "filename", name.c_str());
                            std::string full_p = std::string(dir_path) + "/" + name;
                            struct stat fs;
                            if (stat(full_p.c_str(), &fs) == 0) {
                                cJSON_AddNumberToObject(item, "size_kb", fs.st_size / 1024);
                            }
                            cJSON_AddItemToArray(songs_arr, item);
                        }
                    }
                    closedir(dir);
                }
            };

            scan_dir("/sdcard/music");
            scan_dir("/sdcard");

            cJSON_AddNumberToObject(root, "total_songs", count);
            cJSON_AddItemToObject(root, "songs", songs_arr);

            char* json_str = cJSON_PrintUnformatted(root);
            std::string res(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);

            if (count == 0) {
                return "Belum ada lagu yang tersimpan di kartu memori. Pengguna dapat mendownload lagu dari YouTube dengan meminta download (misal: 'download lagu Alan Walker Faded ke memori').";
            }
            return res;
        });

    AddTool("self.sdcard.play_music",
        "PUTAR LAGU OFFLINE DARI KARTU MEMORI (Play Offline Music from SD Card):\n"
        "Gunakan tool ini untuk memutar lagu yang telah tersimpan di kartu memori ('/sdcard/music/' atau '/sdcard/') secara offline tanpa koneksi internet.\n"
        "Args:\n"
        "  `song_name`: Judul lagu atau nama berkas yang ingin diputar (misal: 'faded', 'tulus', 'Alan_Walker_Faded.wav', atau nomor lagu).\n"
        "  `repeat`: Opsional boolean. Set true jika pengguna meminta mengulang lagu terus menerus (loop), atau false jika hanya diputar 1 kali.\n",
        PropertyList({
            Property("song_name", kPropertyTypeString),
            Property("repeat", kPropertyTypeBoolean, false)
        }),
        [&board](const PropertyList& properties) -> ReturnValue {
            auto target = properties["song_name"].value<std::string>();
            if (target.empty()) {
                return "Harap sebutkan judul lagu yang ingin diputar dari kartu memori.";
            }

            if (!board.IsSdCardMounted()) {
                return "Kartu MicroSD belum terpasang atau tidak terbaca di perangkat.";
            }

            bool repeat = false;
            if (properties.Has("repeat")) {
                repeat = properties["repeat"].value<bool>();
            }
            board.SetWavRepeat(repeat);

            std::string target_lower = target;
            for (auto& c : target_lower) c = tolower(c);

            // Hentikan YouTube player jika sedang jalan
            YouTubeAudioPoller::GetInstance().Abort();

            std::string matched_path = "";
            std::string matched_name = "";

            auto search_dir = [&](const char* dir_path) {
                DIR* dir = opendir(dir_path);
                if (dir) {
                    struct dirent* entry;
                    while ((entry = readdir(dir)) != nullptr) {
                        if (entry->d_name[0] == '.') continue;
                        std::string name = entry->d_name;
                        std::string name_lower = name;
                        for (auto& c : name_lower) c = tolower(c);

                        // Periksa kecocokan substring
                        if (name_lower.find(target_lower) != std::string::npos) {
                            matched_path = std::string(dir_path) + "/" + name;
                            matched_name = name;
                            break;
                        }
                    }
                    closedir(dir);
                }
            };

            search_dir("/sdcard/music");
            if (matched_path.empty()) {
                search_dir("/sdcard");
            }

            if (matched_path.empty()) {
                return "Lagu '" + target + "' tidak ditemukan di kartu memori. Anda dapat memanggil tool 'self.sdcard.list_music' untuk mengecek daftar lagu yang tersedia.";
            }

            bool ok = board.PlayWav(matched_path);
            if (!ok) {
                return "Gagal memutar berkas audio: " + matched_name;
            }

            return "Sedang memutar lagu '" + matched_name + "' secara offline dari kartu memori" + (repeat ? " dengan mode ulang terus (repeat loop)." : ".");
        });

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Always remember you have a camera. If the user asks you to see something, use this tool to take a photo and then explain it.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                {
                    // Lower priority only during camera hardware capture
                    TaskPriorityReset priority_reset(1);
                    if (!camera->Capture()) {
                        throw std::runtime_error("Failed to capture photo");
                    }
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });

        AddTool("self.camera.start_streaming",
            "Start camera streaming to the display. The camera feed will be shown on the TFT screen in real-time.\\n"
            "The user can still talk to XiaoZhi normally while streaming is active.\\n"
            "Use this when the user wants to see what the camera sees.\\n"
            "Return:\\n"
            "  A message confirming streaming has started.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (camera->IsStreaming()) {
                    return std::string("Camera streaming is already active.");
                }
                if (camera->StartStreaming()) {
                    return std::string("Camera streaming started. You can now see the camera feed on the display.");
                } else {
                    throw std::runtime_error("Failed to start camera streaming");
                }
            });

        AddTool("self.camera.stop_streaming",
            "Stop camera streaming. The display will return to normal mode.\\n"
            "Use this when the user wants to stop seeing the camera feed.\\n"
            "Return:\\n"
            "  A message confirming streaming has stopped.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->IsStreaming()) {
                    return std::string("Camera streaming is not active.");
                }
                camera->StopStreaming();
                return std::string("Camera streaming stopped. Display returned to normal mode.");
            });

        AddTool("self.camera.get_streaming_resolutions",
            "Dapatkan daftar resolusi kamera yang didukung untuk live streaming dan resolusi streaming yang sedang aktif saat ini.\\n"
            "Gunakan tool ini ketika pengguna bertanya tentang resolusi kamera apa saja yang didukung, menanyakan pilihan resolusi streaming, atau menanyakan resolusi yang sedang aktif.\\n"
            "Maksimal resolusi streaming adalah VGA (640x480).\\n"
            "Return:\\n"
            "  Daftar resolusi streaming yang didukung beserta status resolusi saat ini.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                return camera->GetSupportedStreamingResolutions();
            });

        AddTool("self.camera.set_streaming_resolution",
            "Ubah resolusi live streaming kamera.\\n"
            "Gunakan tool ini ketika pengguna meminta mengubah, menaikkan, atau memilih resolusi streaming kamera.\\n"
            "Pilihan resolusi streaming yang didukung:\\n"
            "- 'qvga' (320x240, default streaming)\\n"
            "- 'vga' (640x480, resolusi maksimal streaming)\\n"
            "PENTING: Maksimal resolusi streaming dibatasi hingga VGA (640x480). Resolusi di atas VGA tidak didukung.\\n"
            "Resolusi untuk mengambil foto (self.camera.take_photo) selalu tetap dan wajib QVGA (320x240).\\n"
            "Args:\\n"
            "  `resolution`: Pilihan resolusi streaming ('qvga' atau 'vga').\\n"
            "Return:\\n"
            "  Pesan konfirmasi keberhasilan atau penjelasan jika gagal.",
            PropertyList({Property("resolution", kPropertyTypeString)}),
            [camera](const PropertyList& properties) -> ReturnValue {
                auto res = properties["resolution"].value<std::string>();
                if (camera->SetStreamingResolution(res)) {
                    return "Resolusi streaming berhasil diubah ke: " + camera->GetStreamingResolution();
                } else {
                    return "Gagal mengubah resolusi streaming. Resolusi yang diminta tidak didukung atau melebihi batas maksimal VGA (640x480). Pilihan yang tersedia: qvga (320x240, default) dan vga (640x480, maksimal).";
                }
            });

        AddTool("self.camera.zoom_in",
            "Zoom in tampilan kamera (perbesar 1 tingkat). Maksimal zoom hingga 10x.\\n"
            "Hanya berfungsi ketika streaming kamera aktif di layar.\\n"
            "Gunakan tool ini ketika pengguna meminta memperbesar tampilan kamera (zoom in).\\n"
            "Return:\\n"
            "  Konfirmasi status zoom kamera.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->IsStreaming()) {
                    return std::string("Zoom hanya bisa digunakan saat live streaming aktif. Silakan mulai streaming kamera terlebih dahulu.");
                }
                auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                if (display) {
                    int level = display->ZoomIn();
                    return "Kamera di-zoom in ke " + std::to_string(level) + "x (maksimal 10x).";
                }
                return std::string("Gagal mengubah zoom tampilan.");
            });

        AddTool("self.camera.zoom_out",
            "Zoom out tampilan kamera (perkecil 1 tingkat). Minimal hingga 1x (normal view).\\n"
            "Hanya berfungsi ketika streaming kamera aktif di layar.\\n"
            "Gunakan tool ini ketika pengguna meminta memperkecil tampilan kamera (zoom out).\\n"
            "Return:\\n"
            "  Konfirmasi status zoom kamera.",
            PropertyList(std::vector<Property>()),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->IsStreaming()) {
                    return std::string("Zoom hanya bisa digunakan saat live streaming aktif. Silakan mulai streaming kamera terlebih dahulu.");
                }
                auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                if (display) {
                    int level = display->ZoomOut();
                    return "Kamera di-zoom out ke " + std::to_string(level) + "x (minimal 1x).";
                }
                return std::string("Gagal mengubah zoom tampilan.");
            });

        AddTool("self.camera.set_mirror",
            "Atur mode cermin kamera (mirror horizontal).\\n"
            "Gunakan tool ini ketika pengguna meminta mengaktifkan atau menonaktifkan mode cermin kamera (misal 'ubah jadi cermin', 'aktifkan mode cermin', 'matikan mode cermin', 'jangan cermin').\\n"
            "Args:\\n"
            "  `enabled`: true untuk mengaktifkan mode cermin (seperti cermin selfie), false untuk mode normal (teks terbaca normal).\\n"
            "Return:\\n"
            "  Konfirmasi status mode cermin kamera.",
            PropertyList({Property("enabled", kPropertyTypeBoolean)}),
            [camera](const PropertyList& properties) -> ReturnValue {
                bool enabled = properties["enabled"].value<bool>();
                if (camera->SetHMirror(enabled)) {
                    return std::string("Mode cermin kamera berhasil diubah ke: ") + (enabled ? "Aktif (Cermin ON)" : "Nonaktif (Normal / Cermin OFF)");
                }
                return std::string("Gagal mengubah mode cermin kamera.");
            });
    }

    AddTool("self.wifi.reset",
        "Enter Wi-Fi configuration / reset mode. Use this tool when the user asks to reset Wi-Fi, change Wi-Fi, change SSID, delete Wi-Fi credentials, or reconfigure Wi-Fi (e.g. 'tolong hapus wifi', 'ganti wifi', 'ganti ssid', 'coba masuk ke konfigurasi wifi').\n"
        "IMPORTANT:\n"
        "1. When the user asks to reset or change Wi-Fi (e.g. 'tolong hapus wifi', 'ganti wifi', 'ganti ssid', 'masuk ke konfigurasi wifi'), you MUST ALWAYS ask for confirmation first by saying 'Apakah Anda yakin ingin masuk ke mode konfigurasi Wi-Fi? Jawab ya atau yes untuk konfirmasi, atau batal untuk membatalkan.' and DO NOT call this tool yet.\n"
        "2. Only call this tool when the user explicitly confirms (e.g. 'yes', 'ya', 'konfirmasi', 'setuju').\n"
        "3. If the user cancels (e.g. 'batal', 'batalkan', 'enggak jadi', 'tidak'), do NOT call this tool and simply reply that the action was canceled.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            auto wifi_board = dynamic_cast<WifiBoard*>(&board);
            if (wifi_board != nullptr) {
                wifi_board->EnterWifiConfigMode();
                return std::string("Memasuki mode konfigurasi Wi-Fi...");
            }
            return std::string("Gagal: Papan tidak mendukung mode konfigurasi Wi-Fi.");
        });

    AddTool("self.get_hardware_specs",
        "Dapatkan spesifikasi lengkap perangkat keras (hardware) dari alat ini (chip ESP32-S3, RAM/PSRAM, Flash, sensor kamera, layar LCD/OLED, audio mic & speaker, baterai, dan pinout). "
        "Gunakan tool ini ketika pengguna menanyakan spesifikasi hardware, komponen perangkat, chip/prosesor apa yang dipakai, RAM, memori, kamera, layar, atau pinout (misal 'spesifikasi alat ini apa aja', 'alat ini pakai chip apa', 'spek hardware', 'berapa RAM dan memorinya', 'kamera apa yang dipakai', 'layar apa yang dipakai').",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            int batt_level = 0;
            bool charging = false, discharging = false;
            board.GetBatteryLevel(batt_level, charging, discharging);

            size_t free_heap = esp_get_free_heap_size();
            size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_name", "XiaoZhi AI Smart Assistant");
            cJSON_AddStringToObject(root, "board_sku", BOARD_NAME);
            cJSON_AddStringToObject(root, "chip", "ESP32-S3 (Xtensa Dual-Core 32-bit LX7 @ 240 MHz, Wi-Fi 2.4GHz 802.11 b/g/n & Bluetooth 5 BLE)");
            cJSON_AddStringToObject(root, "flash_memory", "16 MB Flash (Quad/Octal SPI)");
            cJSON_AddStringToObject(root, "psram_memory", "8 MB Octal SPI PSRAM (80 MHz)");
            cJSON_AddStringToObject(root, "sram_internal", "512 KB Internal SRAM");
            cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_heap);
            cJSON_AddNumberToObject(root, "free_psram_bytes", (double)free_psram);

            cJSON* cam = cJSON_CreateObject();
            cJSON_AddStringToObject(cam, "sensor", "OmniVision OV5640 5MP (auto-detected, mendukung juga OV2640/OV3660/GC2145)");
            cJSON_AddStringToObject(cam, "interface", "DVP 8-bit Parallel (D0=11, D1=9, D2=8, D3=10, D4=12, D5=18, D6=17, D7=16, XCLK=15, PCLK=13, VSYNC=6, HREF=7, SIOD=4, SIOC=5)");
            cJSON_AddStringToObject(cam, "live_stream_resolution", "QVGA 320x240 RGB565 (15 FPS) / VGA 640x480");
            cJSON_AddStringToObject(cam, "photo_capture_resolution", "QVGA 320x240 JPEG (Direct vision input)");
            cJSON_AddItemToObject(root, "camera", cam);

            cJSON* dsp = cJSON_CreateObject();
            cJSON_AddStringToObject(dsp, "panel", "ST7789 IPS LCD (240x280 / 240x240, 1.69\" / 1.54\" 8-Pin SPI)");
            cJSON_AddStringToObject(dsp, "pinout", "CS=45, SCL=19, SDA=20, DC=47, RST=21, Backlight BL=38 (PWM)");
            cJSON_AddStringToObject(dsp, "alternative_display", "OLED 128x64 SSD1306/SH1106 I2C (SDA=20, SCL=19) atau Headless");
            cJSON_AddItemToObject(root, "display", dsp);

            cJSON* aud = cJSON_CreateObject();
            cJSON_AddStringToObject(aud, "type", "I2S Simplex Codec");
            cJSON_AddStringToObject(aud, "microphone", "INMP441 Digital MEMS Microphone (SCK=2, WS=1, DIN=42)");
            cJSON_AddStringToObject(aud, "speaker_amplifier", "MAX98357A I2S Class D Audio Amp 3W (BCLK=40, WS=41, DOUT=39)");
            cJSON_AddItemToObject(root, "audio", aud);

            cJSON* pwr = cJSON_CreateObject();
            cJSON_AddStringToObject(pwr, "battery_support", "3.7V Li-ion / LiPo Battery via ADC1 CH2 (GPIO 3)");
            cJSON_AddNumberToObject(pwr, "battery_level_percent", batt_level);
            cJSON_AddBoolToObject(pwr, "charging", charging);
            cJSON_AddItemToObject(root, "power", pwr);

            cJSON* btn = cJSON_CreateObject();
            cJSON_AddNumberToObject(btn, "total_buttons", 3);
            cJSON_AddStringToObject(btn, "boot_action_button", "GPIO 0");
            cJSON_AddStringToObject(btn, "volume_up_button", "GPIO 14");
            cJSON_AddStringToObject(btn, "volume_down_button", "GPIO 46");
            cJSON_AddItemToObject(root, "buttons", btn);

            cJSON* led = cJSON_CreateObject();
            cJSON_AddStringToObject(led, "blue", "AI Berbicara (Speaking) / Pemutaran Musik YouTube");
            cJSON_AddStringToObject(led, "red", "AI Mendengarkan (Listening)");
            cJSON_AddStringToObject(led, "off", "Standby / Idle");
            cJSON_AddItemToObject(root, "indicator_led", led);

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);
            return result;
        });

    AddTool("self.get_buttons_guide",
        "Dapatkan panduan lengkap dan fungsi dari ketiga (3) tombol fisik pada perangkat ini (Tombol BOOT, Tombol Volume Atas, Tombol Volume Bawah). "
        "Gunakan tool ini ketika pengguna menanyakan fungsi tombol, apa saja tombol pada perangkat, tombol ini fungsinya buat apa, atau cara menggunakan tombol fisik (misal 'fungsi 3 tombol di alat ini apa aja', 'tombol boot buat apa', 'tombol volume atas buat apa', 'tombol volume bawah fungsinya apa', 'bagaimana cara pakai tombol').",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "title", "Panduan Fungsi 3 Tombol Fisik Perangkat");

            cJSON* buttons = cJSON_CreateArray();

            // Button 1: BOOT / ACTION
            cJSON* b1 = cJSON_CreateObject();
            cJSON_AddStringToObject(b1, "name", "Tombol 1: BOOT / ACTION");
            cJSON_AddNumberToObject(b1, "gpio", 0);
            cJSON* f1 = cJSON_CreateObject();
            cJSON_AddStringToObject(f1, "single_click", "Mulai atau hentikan percakapan AI (Toggle Chat). Jika AI sedang berbicara, klik 1x akan menyela/menghentikan ucapan AI (Interrupt). Jika sedang memutar lagu YouTube, klik 1x akan menghentikan pemutaran musik.");
            cJSON_AddStringToObject(f1, "double_click", "Memutar lagu uji coba musik YouTube ('remix').");
            cJSON_AddStringToObject(f1, "long_press", "Masuk ke mode Konfigurasi Wi-Fi (AP Hotspot Portal) untuk mengubah atau menghubungkan jaringan Wi-Fi.");
            cJSON_AddStringToObject(f1, "boot_press", "Jika ditekan saat perangkat baru dinyalakan (booting), masuk ke mode Chronchi Smartwatch.");
            cJSON_AddItemToObject(b1, "functions", f1);
            cJSON_AddItemToArray(buttons, b1);

            // Button 2: VOLUME UP
            cJSON* b2 = cJSON_CreateObject();
            cJSON_AddStringToObject(b2, "name", "Tombol 2: VOLUME UP (Volume Tambah)");
            cJSON_AddNumberToObject(b2, "gpio", 14);
            cJSON* f2 = cJSON_CreateObject();
            cJSON_AddStringToObject(f2, "single_click", "Jika live streaming kamera sedang aktif di layar: Zoom IN (memperbesar tampilan kamera hingga maksimal 10x). Pada mode biasa: Menaikkan volume suara speaker sebesar 10% (maksimal 100%).");
            cJSON_AddStringToObject(f2, "double_click", "Memutar orientasi tampilan layar LCD sebesar 180 derajat (Rotate Display).");
            cJSON_AddStringToObject(f2, "long_press", "Mengatur volume suara speaker langsung ke tingkat maksimal (100% Max Volume).");
            cJSON_AddItemToObject(b2, "functions", f2);
            cJSON_AddItemToArray(buttons, b2);

            // Button 3: VOLUME DOWN
            cJSON* b3 = cJSON_CreateObject();
            cJSON_AddStringToObject(b3, "name", "Tombol 3: VOLUME DOWN (Volume Kurang)");
            cJSON_AddNumberToObject(b3, "gpio", 46);
            cJSON* f3 = cJSON_CreateObject();
            cJSON_AddStringToObject(f3, "single_click", "Jika live streaming kamera sedang aktif di layar: Zoom OUT (memperkecil tampilan kamera hingga normal 1x). Pada mode biasa: Menurunkan volume suara speaker sebesar 10% (minimal 0%).");
            cJSON_AddStringToObject(f3, "double_click", "Berpindah ke mode Chronchi (Mode Jam Meja Pintar / Smartwatch BLE).");
            cJSON_AddStringToObject(f3, "long_press", "Langsung membisukan speaker (Mute 0%).");
            cJSON_AddItemToObject(b3, "functions", f3);
            cJSON_AddItemToArray(buttons, b3);

            cJSON_AddItemToObject(root, "buttons", buttons);

            char* json_str = cJSON_PrintUnformatted(root);
            std::string result(json_str ? json_str : "{}");
            if (json_str) free(json_str);
            cJSON_Delete(root);
            return result;
        });

    // Restore the original tools list to the end of the tools list without duplicates
    for (auto orig_tool : original_tools) {
        auto it = std::find_if(tools_.begin(), tools_.end(), [orig_tool](const McpTool* t) {
            return t->name() == orig_tool->name();
        });
        if (it != tools_.end()) {
            ESP_LOGW(TAG, "Duplicate tool '%s' detected, replacing with board tool", orig_tool->name().c_str());
            delete *it;
            *it = orig_tool;
        } else {
            tools_.push_back(orig_tool);
        }
    }
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddUserOnlyTool("self.reboot", "Reboot the system",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                ESP_LOGW(TAG, "User requested reboot");
                vTaskDelay(pdMS_TO_TICKS(1000));

                app.Reboot();
            });
            return true;
        });

    // Firmware upgrade
    AddUserOnlyTool("self.upgrade_firmware", "Upgrade firmware from a specific URL. This will download and install the firmware, then reboot the device.",
        PropertyList({
            Property("url", kPropertyTypeString, "The URL of the firmware binary file to download and install")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());
            
            auto& app = Application::GetInstance();
            app.Schedule([url, &app]() {
                bool success = app.UpgradeFirmware(url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
            });
            
            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                if (dynamic_cast<OledDisplay*>(display)) {
                    cJSON_AddBoolToObject(json, "monochrome", true);
                } else {
                    cJSON_AddBoolToObject(json, "monochrome", false);
                }
                return json;
            });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool("self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("quality", kPropertyTypeInteger, 80, 1, 100)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());
                
                // 构造multipart/form-data请求体
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";
                
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 文件字段头部
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG数据
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart尾部
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif // CONFIG_LV_USE_SNAPSHOT
    }
#endif // HAVE_LVGL

    // Assets download url (always registered — Settings storage works regardless of partition layout)
    AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                Settings settings("assets", true);
                settings.SetString("download_url", url);
                return true;
            });
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    auto it = std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); });
    if (it != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added, replacing with new tool", tool->name().c_str());
        delete *it;
        *it = tool;
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
