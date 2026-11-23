import os

# 1. Patch main/youtube_audio_poller.cc
yt_file = "main/youtube_audio_poller.cc"
if os.path.exists(yt_file):
    with open(yt_file, "r", encoding="utf-8") as f:
        content = f.read()

    target = """#ifdef CONFIG_CONNECTION_SERVER_URL
    if (found_domain.empty()) {
        std::string cfg_url = CONFIG_CONNECTION_SERVER_URL;
        if (!cfg_url.empty()) {
            found_domain = cfg_url;
        }
    }
#endif"""
    replacement = """#ifdef CONFIG_CONNECTION_SERVER_URL
    if (found_domain.empty()) {
        std::string cfg_url = CONFIG_CONNECTION_SERVER_URL;
        if (!cfg_url.empty()) {
            found_domain = cfg_url;
        }
    }
#endif

    // Fallback default domain so YouTube streaming works out-of-the-box
    if (found_domain.empty()) {
        found_domain = "xiaozhiscig.biz.id";
    }"""

    if target in content and "Fallback default domain" not in content:
        content = content.replace(target, replacement, 1)
        with open(yt_file, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched main/youtube_audio_poller.cc successfully")

# 2. Patch managed_components/78__esp-wifi-connect/include/wifi_configuration_ap.h
h_file = "managed_components/78__esp-wifi-connect/include/wifi_configuration_ap.h"
if os.path.exists(h_file):
    with open(h_file, "r", encoding="utf-8") as f:
        content = f.read()
    if "server_domain_" not in content:
        target = "    std::string ota_url_;"
        replacement = "    std::string ota_url_;\n    std::string server_domain_ = \"xiaozhiscig.biz.id\";"
        content = content.replace(target, replacement, 1)
        with open(h_file, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched wifi_configuration_ap.h successfully")

# 3. Patch managed_components/78__esp-wifi-connect/wifi_configuration_ap.cc
cc_file = "managed_components/78__esp-wifi-connect/wifi_configuration_ap.cc"
if os.path.exists(cc_file):
    with open(cc_file, "r", encoding="utf-8") as f:
        content = f.read()

    if "server_domain" not in content:
        # Load server_domain from NVS in constructor
        target = """        // 读取WiFi功率"""
        replacement = """        // 读取 Server Domain
        char s_dom[128] = {0};
        size_t s_dom_size = sizeof(s_dom);
        if (nvs_get_str(nvs, "server_domain", s_dom, &s_dom_size) == ESP_OK && s_dom_size > 0 && s_dom[0] != '\\0') {
            server_domain_ = s_dom;
        } else {
            server_domain_ = "xiaozhiscig.biz.id";
        }

        // 读取WiFi功率"""
        content = content.replace(target, replacement, 1)

        # In /submit
        target = """            this_->Save(ssid_str, password_str);"""
        replacement = """            cJSON *s_dom_item = cJSON_GetObjectItemCaseSensitive(json, "server_domain");
            if (s_dom_item && cJSON_IsString(s_dom_item) && s_dom_item->valuestring && strlen(s_dom_item->valuestring) > 0) {
                nvs_handle_t nvs_w;
                if (nvs_open("wifi", NVS_READWRITE, &nvs_w) == ESP_OK) {
                    nvs_set_str(nvs_w, "server_domain", s_dom_item->valuestring);
                    nvs_commit(nvs_w);
                    nvs_close(nvs_w);
                    this_->server_domain_ = s_dom_item->valuestring;
                }
            }

            this_->Save(ssid_str, password_str);"""
        content = content.replace(target, replacement, 1)

        # In /advanced/config
        target = """            cJSON_AddStringToObject(json, "ota_url", this_->ota_url_.c_str());"""
        replacement = """            cJSON_AddStringToObject(json, "ota_url", this_->ota_url_.c_str());
            cJSON_AddStringToObject(json, "server_domain", this_->server_domain_.c_str());"""
        content = content.replace(target, replacement, 1)

        # In /advanced/submit
        target = """            // 保存OTA URL"""
        replacement = """            cJSON *s_dom_adv = cJSON_GetObjectItemCaseSensitive(json, "server_domain");
            if (s_dom_adv && cJSON_IsString(s_dom_adv) && s_dom_adv->valuestring && strlen(s_dom_adv->valuestring) > 0) {
                err = nvs_set_str(nvs, "server_domain", s_dom_adv->valuestring);
                if (err == ESP_OK) {
                    this_->server_domain_ = s_dom_adv->valuestring;
                }
            }

            // 保存OTA URL"""
        content = content.replace(target, replacement, 1)

        with open(cc_file, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched wifi_configuration_ap.cc successfully")

# 4. Patch managed_components/78__esp-wifi-connect/assets/wifi_configuration.html
html_file = "managed_components/78__esp-wifi-connect/assets/wifi_configuration.html"
if os.path.exists(html_file):
    with open(html_file, "r", encoding="utf-8") as f:
        content = f.read()

    # Add toggle password eye icon
    if "toggle_password" not in content:
        old_pwd = """                <p>
                    <label for="password" data-lang="password">Password:</label>
                    <input type="password" id="password" name="password" autocomplete="off" dir="auto">
                </p>"""
        new_pwd = """                <p>
                    <label for="password" data-lang="password">Password:</label>
                    <div style="display: flex; align-items: center; gap: 8px;">
                        <input type="password" id="password" name="password" autocomplete="off" dir="auto" style="flex: 1; min-width: 0;">
                        <button type="button" class="icon-btn" id="toggle_password" onclick="togglePasswordVisibility()" aria-label="Toggle password visibility" title="Lihat/Sembunyikan Sandi" style="width: 44px; height: 42px; border-radius: var(--radius-sm); display: flex; align-items: center; justify-content: center; cursor: pointer; color: var(--text-muted);">
                            <svg id="eye_icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="width: 20px; height: 20px;">
                                <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
                                <circle cx="12" cy="12" r="3"/>
                            </svg>
                        </button>
                    </div>
                </p>"""
        if old_pwd in content:
            content = content.replace(old_pwd, new_pwd, 1)

        style_target = ".icon-btn:hover {"
        style_replacement = "#toggle_password:hover { color: var(--accent); border-color: var(--accent); }\n        .icon-btn:hover {"
        if style_target in content:
            content = content.replace(style_target, style_replacement, 1)

        js_target = "function clearOtaUrl() {"
        js_replacement = """function togglePasswordVisibility() {
            const pwd = document.getElementById('password');
            const eye = document.getElementById('eye_icon');
            const btn = document.getElementById('toggle_password');
            if (pwd.type === 'password') {
                pwd.type = 'text';
                btn.style.color = 'var(--accent)';
                eye.innerHTML = '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>';
            } else {
                pwd.type = 'password';
                btn.style.color = 'var(--text-muted)';
                eye.innerHTML = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
            }
        }

        function clearOtaUrl() {"""
        if js_target in content:
            content = content.replace(js_target, js_replacement, 1)

    if "server_domain" not in content:
        target = """                <p>
                    <label for="password" data-lang="password">Password:</label>"""
        # Place server domain after password
        target_after = "</p>"
        pass_idx = content.find("id=\"password\"")
        if pass_idx != -1:
            end_p = content.find("</p>", pass_idx)
            if end_p != -1:
                insert_pos = end_p + 4
                s_dom_html = """
                <p>
                    <label for="server_domain">Domain Server (YouTube / Audio Server):</label>
                    <input type="text" id="server_domain" name="server_domain" autocomplete="off" autocapitalize="none" spellcheck="false" dir="ltr" placeholder="xiaozhiscig.biz.id" value="xiaozhiscig.biz.id">
                </p>"""
                content = content[:insert_pos] + s_dom_html + content[insert_pos:]

        # In submitForm payload
        target_sf = """            const payload = {
                ssid: ssid.value,
                password: document.getElementById('password').value
            };"""
        replacement_sf = """            const domElem = document.getElementById('server_domain');
            const payload = {
                ssid: ssid.value,
                password: document.getElementById('password').value,
                server_domain: (domElem && domElem.value.trim()) ? domElem.value.trim() : 'xiaozhiscig.biz.id'
            };"""
        if target_sf in content:
            content = content.replace(target_sf, replacement_sf, 1)

        # In loadAdvancedConfig
        target_lac = """                const otaUrl = typeof data.ota_url === 'string' ? data.ota_url : '';"""
        replacement_lac = """                if (data.server_domain && document.getElementById('server_domain')) {
                    document.getElementById('server_domain').value = data.server_domain;
                }
                const otaUrl = typeof data.ota_url === 'string' ? data.ota_url : '';"""
        if target_lac in content:
            content = content.replace(target_lac, replacement_lac, 1)

        # In submitAdvancedForm
        target_saf = """            const config = {
                ota_url: otaUrl,"""
        replacement_saf = """            const domElem = document.getElementById('server_domain');
            const config = {
                server_domain: (domElem && domElem.value.trim()) ? domElem.value.trim() : 'xiaozhiscig.biz.id',
                ota_url: otaUrl,"""
        if target_saf in content:
            content = content.replace(target_saf, replacement_saf, 1)

    with open(html_file, "w", encoding="utf-8") as f:
        f.write(content)
    print("Patched wifi_configuration.html successfully")
