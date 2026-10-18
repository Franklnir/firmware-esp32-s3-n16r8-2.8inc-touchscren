#include "sdkconfig.h"

#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <esp_log.h>
#include <img_converters.h>

#include "esp32_camera.h"
#include "board.h"
#include "display.h"
#include "lvgl_display.h"
#include "mcp_server.h"
#include "system_info.h"
#include "settings.h"
#include "jpg/image_to_jpeg.h"
#include "esp_timer.h"
#include "application.h"

#define TAG "Esp32Camera"

#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
#if CONFIG_XIAOZHI_CAMERA_HMIRROR
static constexpr bool kConfiguredHMirror = true;
#else
static constexpr bool kConfiguredHMirror = false;
#endif
#if CONFIG_XIAOZHI_CAMERA_VFLIP
static constexpr bool kConfiguredVFlip = true;
#else
static constexpr bool kConfiguredVFlip = false;
#endif
#endif

Esp32Camera::Esp32Camera(const camera_config_t &config) : config_(config) {
    photo_framesize_ = FRAMESIZE_QVGA; // Mandatory QVGA (320x240) for photo capture

    // Default streaming resolution is always QVGA (320x240 RGB565).
    // Streaming resolution changes are strictly volatile (stored in RAM only, never saved to NVS)
    // so turning off / rebooting the ESP32 always reverts to the default QVGA resolution.
    stream_framesize_ = FRAMESIZE_QVGA;
    stream_pixformat_ = PIXFORMAT_RGB565;
    stream_res_str_ = "qvga";

    config_.frame_size = stream_framesize_;
    config_.pixel_format = stream_pixformat_;
    config_.fb_count = 2;
    config_.fb_location = CAMERA_FB_IN_PSRAM;
    config_.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        if (s->id.PID == GC0308_PID) {
            s->set_hmirror(s, 0); // Control camera mirror: 1 for mirror, 0 for normal
        }
        s->set_hmirror(s, hmirror_enabled_ ? 1 : 0);
        s->set_vflip(s, vflip_enabled_ ? 1 : 0);
        std::string sensor_name = "Camera";
        if (s->id.PID == OV2640_PID) sensor_name = "OV2640";
        else if (s->id.PID == OV3660_PID) sensor_name = "OV3660";
        else if (s->id.PID == OV5640_PID) sensor_name = "OV5640";

        ESP_LOGI(TAG, "%s initialized (PID=0x%x): stream=%s (%dx%d, format=%d), photo=QVGA (320x240)",
                 sensor_name.c_str(),
                 s->id.PID,
                 stream_res_str_.c_str(),
                 resolution[config_.frame_size].width,
                 resolution[config_.frame_size].height,
                 config_.pixel_format);
    }

    streaming_on_ = true;
}

Esp32Camera::~Esp32Camera() {
    StopStreaming();
    if (streaming_on_) {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        if (current_fb_) {
            esp_camera_fb_return(current_fb_);
            current_fb_ = nullptr;
        }
        if (captured_jpeg_buf_) {
            heap_caps_free(captured_jpeg_buf_);
            captured_jpeg_buf_ = nullptr;
            captured_jpeg_len_ = 0;
        }
        if (encode_buf_) {
            heap_caps_free(encode_buf_);
            encode_buf_ = nullptr;
            encode_buf_size_ = 0;
        }
        esp_camera_deinit();
        streaming_on_ = false;
    }
}

void Esp32Camera::SetExplainUrl(const std::string &url, const std::string &token) {
    explain_url_ = url;
    explain_token_ = token;
}

void Esp32Camera::ShowCapturedPhotoOnDisplay() {
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return;
    }

    if (captured_jpeg_buf_ != nullptr && captured_jpeg_len_ > 0) {
        esp_jpeg_image_cfg_t jpeg_cfg = {};
        jpeg_cfg.indata = captured_jpeg_buf_;
        jpeg_cfg.indata_size = captured_jpeg_len_;
        
        esp_jpeg_image_output_t out_info = {};
        if (esp_jpeg_get_image_info(&jpeg_cfg, &out_info) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get JPEG info for preview");
            return;
        }

        // Determine downscale factor to fit display (e.g. 240x280)
        esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_1_8;
        int divisor = 8;
        if (out_info.width < 1000) {
            scale = JPEG_IMAGE_SCALE_1_4;
            divisor = 4;
        }
        if (out_info.width < 500) {
            scale = JPEG_IMAGE_SCALE_1_2;
            divisor = 2;
        }

        uint16_t target_w = (out_info.width + divisor - 1) / divisor;
        uint16_t target_h = (out_info.height + divisor - 1) / divisor;
        size_t preview_size = target_w * target_h * 2;

        uint8_t* preview_buf = (uint8_t*)heap_caps_malloc(preview_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (preview_buf == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for preview display", preview_size);
            return;
        }

        jpeg_cfg.outbuf = preview_buf;
        jpeg_cfg.outbuf_size = preview_size;
        jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
        jpeg_cfg.out_scale = scale;
        jpeg_cfg.flags.swap_color_bytes = 0;  // 0 = native little-endian for LVGL RGB565

        esp_jpeg_image_output_t decoded_img = {};
        if (esp_jpeg_decode(&jpeg_cfg, &decoded_img) == ESP_OK) {
            ESP_LOGI(TAG, "Captured photo preview: %dx%d (original %dx%d), showing on display",
                     decoded_img.width, decoded_img.height, out_info.width, out_info.height);
            display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                preview_buf, decoded_img.output_len, decoded_img.width, decoded_img.height,
                decoded_img.width * 2, LV_COLOR_FORMAT_RGB565));
        } else {
            ESP_LOGE(TAG, "Failed to decode JPEG for preview display");
            heap_caps_free(preview_buf);
        }
    } else if (current_fb_ != nullptr && current_fb_->format == PIXFORMAT_RGB565) {
        size_t pixel_count = current_fb_->width * current_fb_->height;
        size_t data_size = pixel_count * 2;
        uint8_t *preview_data = (uint8_t *)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (preview_data != nullptr) {
            uint16_t *src = (uint16_t *)current_fb_->buf;
            uint16_t *dst = (uint16_t *)preview_data;
            for (size_t i = 0; i < pixel_count; i++) {
                dst[i] = __builtin_bswap16(src[i]);
            }
            display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                preview_data, data_size, current_fb_->width, current_fb_->height,
                current_fb_->width * 2, LV_COLOR_FORMAT_RGB565));
        }
    }
}

bool Esp32Camera::Capture() {
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    if (!streaming_on_) {
        return false;
    }

    // Trigger capture animation (flash effect)
    if (capture_callback_) {
        capture_callback_();
    }

    bool was_streaming = is_streaming_.load();

    // Pause streaming thread to safely capture photo
    stream_paused_.store(true);

    // Wait until any active streaming frame in flight is safely returned
    for (int wait_count = 0; wait_count < 25 && stream_frame_busy_.load(); wait_count++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    std::lock_guard<std::mutex> lock(camera_mutex_);

    // Clean up previous photo buffer
    if (captured_jpeg_buf_ != nullptr) {
        heap_caps_free(captured_jpeg_buf_);
        captured_jpeg_buf_ = nullptr;
        captured_jpeg_len_ = 0;
        captured_width_ = 0;
        captured_height_ = 0;
    }

    // Photo is strictly and mandatory QVGA (320x240)
    photo_framesize_ = FRAMESIZE_QVGA;

    if (config_.frame_size == FRAMESIZE_QVGA && config_.pixel_format == PIXFORMAT_RGB565) {
        // Stream is already QVGA RGB565! Grab a frame directly, no reconfiguration needed.
        camera_fb_t* fb = nullptr;
        for (int retry = 0; retry < 3; retry++) {
            fb = esp_camera_fb_get();
            if (fb && fb->len > 0) break;
            if (fb) {
                esp_camera_fb_return(fb);
                fb = nullptr;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }

        if (fb) {
            ESP_LOGI(TAG, "Converting QVGA RGB565 to JPEG for photo capture...");
            uint8_t* out_jpg = nullptr;
            size_t out_len = 0;
            bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80,
                              &out_jpg, &out_len);
            if (ok && out_jpg != nullptr && out_len > 0) {
                captured_jpeg_buf_ = out_jpg;
                captured_jpeg_len_ = out_len;
                captured_width_ = fb->width;
                captured_height_ = fb->height;
                ESP_LOGI(TAG, "Photo captured (QVGA JPEG via fmt2jpg): %dx%d, %zu bytes",
                         captured_width_, captured_height_, captured_jpeg_len_);
            } else {
                ESP_LOGE(TAG, "fmt2jpg conversion failed");
            }
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGE(TAG, "Failed to get frame in QVGA RGB565 mode");
        }
    } else if (config_.frame_size == FRAMESIZE_QVGA && config_.pixel_format == PIXFORMAT_JPEG) {
        // Stream is already QVGA JPEG! Grab directly.
        camera_fb_t* fb = nullptr;
        for (int retry = 0; retry < 3; retry++) {
            fb = esp_camera_fb_get();
            if (fb && fb->len > 0) break;
            if (fb) {
                esp_camera_fb_return(fb);
                fb = nullptr;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }

        if (fb) {
            captured_jpeg_buf_ = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (captured_jpeg_buf_) {
                memcpy(captured_jpeg_buf_, fb->buf, fb->len);
                captured_jpeg_len_ = fb->len;
                captured_width_ = fb->width;
                captured_height_ = fb->height;
                ESP_LOGI(TAG, "Photo captured (QVGA JPEG direct): %dx%d, %zu bytes",
                         captured_width_, captured_height_, captured_jpeg_len_);
            }
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGE(TAG, "Failed to get frame in QVGA JPEG mode");
        }
    } else {
        // Stream was at another resolution (VGA, SVGA, XGA, HD, SXGA, UXGA).
        // Temporarily reconfigure camera to QVGA JPEG for photo capture.
        ESP_LOGI(TAG, "Temporarily switching camera from %dx%d to QVGA (320x240) JPEG for photo...",
                 resolution[config_.frame_size].width, resolution[config_.frame_size].height);

        camera_config_t photo_config = config_;
        photo_config.frame_size = FRAMESIZE_QVGA;
        photo_config.pixel_format = PIXFORMAT_JPEG;
        photo_config.jpeg_quality = 12;
        photo_config.fb_count = 1;
        photo_config.grab_mode = CAMERA_GRAB_LATEST;
        photo_config.fb_location = CAMERA_FB_IN_PSRAM;

        esp_err_t reconf_err = esp_camera_reconfigure(&photo_config);
        if (reconf_err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            // Discard 1 frame
            camera_fb_t *discard_fb = esp_camera_fb_get();
            if (discard_fb) {
                esp_camera_fb_return(discard_fb);
            }
            vTaskDelay(pdMS_TO_TICKS(30));

            camera_fb_t *photo_fb = esp_camera_fb_get();
            if (photo_fb && photo_fb->len > 0) {
                captured_jpeg_buf_ = (uint8_t*)heap_caps_malloc(photo_fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (captured_jpeg_buf_) {
                    memcpy(captured_jpeg_buf_, photo_fb->buf, photo_fb->len);
                    captured_jpeg_len_ = photo_fb->len;
                    captured_width_ = photo_fb->width;
                    captured_height_ = photo_fb->height;
                    ESP_LOGI(TAG, "Photo captured (reconfigured QVGA JPEG): %dx%d, %zu bytes",
                             captured_width_, captured_height_, captured_jpeg_len_);
                }
                esp_camera_fb_return(photo_fb);
            } else {
                if (photo_fb) esp_camera_fb_return(photo_fb);
                ESP_LOGE(TAG, "Failed to capture photo frame after reconfigure");
            }

            // Restore stream configuration
            ESP_LOGI(TAG, "Restoring stream configuration (%dx%d, format=%d)...",
                     resolution[config_.frame_size].width, resolution[config_.frame_size].height, config_.pixel_format);
            esp_camera_reconfigure(&config_);
            vTaskDelay(pdMS_TO_TICKS(50));
            sensor_t *s = esp_camera_sensor_get();
            if (s) {
                s->set_hmirror(s, hmirror_enabled_ ? 1 : 0);
                s->set_vflip(s, vflip_enabled_ ? 1 : 0);
            }
        } else {
            ESP_LOGE(TAG, "Failed to reconfigure camera to QVGA JPEG: 0x%x", reconf_err);
        }
    }

    // Display captured photo on screen
    ShowCapturedPhotoOnDisplay();

    if (was_streaming) {
        // Keep streaming paused while AI explains photo and speaks.
        // It will automatically resume once AI finishes speaking (returns to idle/listening).
        resume_stream_on_idle_.store(true);
        stream_paused_.store(true);
        stream_resume_time_us_.store(0);
    } else {
        resume_stream_on_idle_.store(false);
        stream_paused_.store(false);
        stream_resume_time_us_.store(0);
    }
    return (captured_jpeg_buf_ != nullptr);
}

bool Esp32Camera::SetHMirror(bool enabled) {
    hmirror_enabled_ = enabled;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    s->set_hmirror(s, enabled ? 1 : 0);
    ESP_LOGI(TAG, "Camera H-Mirror set to: %s", enabled ? "ON (cermin)" : "OFF (normal)");
    return true;
}

bool Esp32Camera::SetVFlip(bool enabled) {
    vflip_enabled_ = enabled;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }
    s->set_vflip(s, enabled ? 1 : 0);
    ESP_LOGI(TAG, "Camera V-Flip set to: %s", enabled ? "ON" : "OFF");
    return true;
}

bool Esp32Camera::SetSwapBytes(bool enabled) {
    swap_bytes_enabled_ = enabled;
    return true;
}

std::string Esp32Camera::Explain(const std::string &question) {
    if (explain_url_.empty()) {
        throw std::runtime_error("Image explain URL or token is not set");
    }

    if (captured_jpeg_buf_ == nullptr && current_fb_ == nullptr) {
        throw std::runtime_error("No camera frame captured");
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        throw std::runtime_error("Failed to connect to explain URL");
    }

    {
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    size_t total_sent = 0;

    if (captured_jpeg_buf_ != nullptr && captured_jpeg_len_ > 0) {
        ESP_LOGI(TAG, "Streaming high-res JPEG (%dx%d, %zu bytes) directly to HTTP...",
                 captured_width_, captured_height_, captured_jpeg_len_);
        const size_t kChunkSize = 4096;
        size_t remaining = captured_jpeg_len_;
        size_t offset = 0;
        while (remaining > 0) {
            size_t to_send = std::min(remaining, kChunkSize);
            http->Write((const char *)(captured_jpeg_buf_ + offset), to_send);
            total_sent += to_send;
            offset += to_send;
            remaining -= to_send;
        }
    } else if (current_fb_ != nullptr) {
        // Fallback: encode raw frame to JPEG on the fly
        QueueHandle_t jpeg_queue = xQueueCreate(20, sizeof(JpegChunk));
        if (jpeg_queue == nullptr) {
            ESP_LOGE(TAG, "Failed to create JPEG queue");
            throw std::runtime_error("Failed to create JPEG queue");
        }

        encoder_thread_ = std::thread([this, jpeg_queue]() {
            int64_t start_time = esp_timer_get_time();
            uint16_t w = current_fb_->width;
            uint16_t h = current_fb_->height;
            v4l2_pix_fmt_t enc_fmt;
            switch (current_fb_->format) {
                case PIXFORMAT_RGB565:
                    enc_fmt = V4L2_PIX_FMT_RGB565;
                    break;
                case PIXFORMAT_YUV422:
                    enc_fmt = V4L2_PIX_FMT_YUYV;
                    break;
                case PIXFORMAT_YUV420:
                    enc_fmt = V4L2_PIX_FMT_YUV420;
                    break;
                case PIXFORMAT_GRAYSCALE:
                    enc_fmt = V4L2_PIX_FMT_GREY;
                    break;
                case PIXFORMAT_JPEG:
                    enc_fmt = V4L2_PIX_FMT_JPEG;
                    break;
                case PIXFORMAT_RGB888:
                    enc_fmt = V4L2_PIX_FMT_RGB24;
                    break;
                default:
                    ESP_LOGE(TAG, "Unsupported pixel format: %d", current_fb_->format);
                    {
                        JpegChunk chunk = {.data = nullptr, .len = 0};
                        xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
                    }
                    return;
            }

            uint8_t *jpeg_src_buf = current_fb_->buf;
            size_t jpeg_src_len = current_fb_->len;
            if (current_fb_->format == PIXFORMAT_RGB565 && encode_buf_ != nullptr) {
                jpeg_src_buf = encode_buf_;
                jpeg_src_len = encode_buf_size_;
            }

            bool ok = image_to_jpeg_cb(jpeg_src_buf, jpeg_src_len, w, h, enc_fmt, 80,
                [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                    auto jq = static_cast<QueueHandle_t>(arg);
                    JpegChunk chunk = {.data = nullptr, .len = len};
                    if (index == 0 && data != nullptr && len > 0) {
                        chunk.data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (chunk.data == nullptr) {
                            ESP_LOGE(TAG, "Failed to allocate %zu bytes for JPEG chunk", len);
                            chunk.len = 0;
                        } else {
                            memcpy(chunk.data, data, len);
                        }
                    } else {
                        chunk.len = 0;
                    }
                    xQueueSend(jq, &chunk, portMAX_DELAY);
                    return len;
                }, jpeg_queue);

            if (!ok) {
                JpegChunk chunk = {.data = nullptr, .len = 0};
                xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
            }
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "JPEG encoding time: %ld ms", int((end_time - start_time) / 1000));
        });

        bool saw_terminator = false;
        while (true) {
            JpegChunk chunk;
            if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
                ESP_LOGE(TAG, "Failed to receive JPEG chunk");
                break;
            }
            if (chunk.data == nullptr) {
                saw_terminator = true;
                break;
            }
            http->Write((const char *)chunk.data, chunk.len);
            total_sent += chunk.len;
            heap_caps_free(chunk.data);
        }
        encoder_thread_.join();
        vQueueDelete(jpeg_queue);

        if (!saw_terminator || total_sent == 0) {
            ESP_LOGE(TAG, "JPEG encoder failed or produced empty output");
            throw std::runtime_error("Failed to encode image to JPEG");
        }
    }

    if (total_sent == 0) {
        ESP_LOGE(TAG, "No image data sent to explain server");
        throw std::runtime_error("Failed to send image to server");
    }

    {
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    http->Write("", 0);

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        throw std::runtime_error("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    int img_w = (captured_jpeg_buf_ != nullptr) ? captured_width_ : (current_fb_ ? current_fb_->width : 0);
    int img_h = (captured_jpeg_buf_ != nullptr) ? captured_height_ : (current_fb_ ? current_fb_->height : 0);
    ESP_LOGI(TAG, "Explain image size=%dx%d, compressed size=%d, remain stack size=%d, question=%s\n%s",
             img_w, img_h, (int)total_sent, (int)remain_stack_size, question.c_str(), result.c_str());

    // Clean up captured JPEG buffer after upload
    if (captured_jpeg_buf_) {
        heap_caps_free(captured_jpeg_buf_);
        captured_jpeg_buf_ = nullptr;
        captured_jpeg_len_ = 0;
    }

    return result;
}

// ── Streaming Implementation ──
void Esp32Camera::SetStreamCallback(std::function<void(uint8_t* data, size_t len, uint16_t w, uint16_t h)> callback) {
    stream_callback_ = callback;
}

bool Esp32Camera::StartStreaming() {
    if (is_streaming_.load()) {
        ESP_LOGW(TAG, "Already streaming");
        return true;
    }
    
    if (!streaming_on_) {
        ESP_LOGE(TAG, "Camera not initialized");
        return false;
    }
    
    is_streaming_.store(true);
    stream_paused_.store(false);
    
    // Start streaming thread
    stream_thread_ = std::thread([this]() {
        ESP_LOGI(TAG, "Camera streaming started");
        
        while (is_streaming_.load()) {
            if (stream_paused_.load()) {
                // If stream was paused for photo capture and AI has finished speaking, auto-resume stream
                if (resume_stream_on_idle_.load()) {
                    auto dev_state = Application::GetInstance().GetDeviceState();
                    if (dev_state == kDeviceStateIdle || dev_state == kDeviceStateListening) {
                        resume_stream_on_idle_.store(false);
                        stream_paused_.store(false);
                        ESP_LOGI(TAG, "Camera stream thread resumed after AI finished speaking");
                        continue;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }

            // Do not capture or push frames while AI is actively speaking.
            // Prevents audio stutter, PSRAM bandwidth saturation, and LVGL display lock contention.
            auto dev_state = Application::GetInstance().GetDeviceState();
            if (dev_state == kDeviceStateSpeaking) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            int64_t resume_time = stream_resume_time_us_.load();
            if (resume_time > 0) {
                if (esp_timer_get_time() < resume_time) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                stream_resume_time_us_.store(0);
            }

            camera_fb_t* fb = nullptr;
            {
                std::unique_lock<std::mutex> lock(camera_mutex_, std::try_to_lock);
                if (!lock.owns_lock() || stream_paused_.load()) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                stream_frame_busy_.store(true);
                fb = esp_camera_fb_get();
            }

            if (!fb) {
                stream_frame_busy_.store(false);
                ESP_LOGW(TAG, "Stream: failed to get frame");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            // Call the callback with frame data (only if not paused)
            if (stream_callback_ && !stream_paused_.load()) {
                stream_callback_(fb->buf, fb->len, fb->width, fb->height);
            }
            
            // Return the frame buffer
            esp_camera_fb_return(fb);
            stream_frame_busy_.store(false);
            
            // ~15 FPS target
            vTaskDelay(pdMS_TO_TICKS(66));
        }
        
        ESP_LOGI(TAG, "Camera streaming stopped");
    });
    
    return true;
}

void Esp32Camera::StopStreaming() {
    if (!is_streaming_.load()) {
        return;
    }
    
    is_streaming_.store(false);
    
    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }
    
    ESP_LOGI(TAG, "Camera streaming stopped");
}

std::string Esp32Camera::GetSupportedStreamingResolutions() const {
    sensor_t *s = esp_camera_sensor_get();
    std::string sensor_name = "Kamera";
    if (s) {
        if (s->id.PID == OV2640_PID) sensor_name = "OV2640";
        else if (s->id.PID == OV3660_PID) sensor_name = "OV3660";
        else if (s->id.PID == OV5640_PID) sensor_name = "OV5640";
    }

    std::string info;
    info += "Sensor kamera terdeteksi: " + sensor_name + ".\n";
    info += "Daftar pilihan resolusi streaming yang didukung (maksimal VGA):\n";
    info += "1. QVGA (320x240 piksel) - Default streaming\n";
    info += "2. VGA (640x480 piksel) - Maksimal pilihan resolusi streaming\n";
    info += "Resolusi streaming saat ini: " + stream_res_str_ + " (" +
            std::to_string(resolution[config_.frame_size].width) + "x" +
            std::to_string(resolution[config_.frame_size].height) + ").\n";
    info += "Catatan: Resolusi pengambilan foto wajib dan terkunci di QVGA (320x240 piksel).";
    return info;
}

std::string Esp32Camera::GetStreamingResolution() const {
    return stream_res_str_ + " (" +
           std::to_string(resolution[config_.frame_size].width) + "x" +
           std::to_string(resolution[config_.frame_size].height) + ")";
}

bool Esp32Camera::SetStreamingResolution(const std::string& res_str) {
    std::string res = res_str;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
    res.erase(0, res.find_first_not_of(" \t\n\r"));
    res.erase(res.find_last_not_of(" \t\n\r") + 1);

    framesize_t target_size;
    std::string norm_name;

    if (res == "qvga" || res == "320x240" || res == "default" || res == "standar") {
        target_size = FRAMESIZE_QVGA;
        norm_name = "qvga";
    } else if (res == "vga" || res == "640x480" || res == "max" || res == "maksimal") {
        target_size = FRAMESIZE_VGA;
        norm_name = "vga";
    } else {
        ESP_LOGW(TAG, "Unsupported streaming resolution requested: %s (supported: qvga, vga)", res_str.c_str());
        return false;
    }

    if (!streaming_on_) {
        ESP_LOGE(TAG, "Camera not initialized");
        return false;
    }

    bool was_streaming = is_streaming_.load();
    if (was_streaming) {
        stream_paused_.store(true);
        for (int i = 0; i < 25 && stream_frame_busy_.load(); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    {
        std::lock_guard<std::mutex> lock(camera_mutex_);
        config_.frame_size = target_size;
        config_.pixel_format = PIXFORMAT_RGB565; // Native RGB565 for perfect colors
        config_.fb_count = 2;
        config_.fb_location = CAMERA_FB_IN_PSRAM;
        config_.grab_mode = CAMERA_GRAB_LATEST;

        esp_err_t err = esp_camera_reconfigure(&config_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_camera_reconfigure failed (0x%x) for %s", err, norm_name.c_str());
            if (was_streaming) {
                stream_paused_.store(false);
            }
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(80));

        // Flush any stale frames from previous mode
        for (int i = 0; i < 2; i++) {
            camera_fb_t *discard_fb = esp_camera_fb_get();
            if (discard_fb) {
                esp_camera_fb_return(discard_fb);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        sensor_t *s = esp_camera_sensor_get();
        if (s) {
            s->set_hmirror(s, hmirror_enabled_ ? 1 : 0);
            s->set_vflip(s, vflip_enabled_ ? 1 : 0);
        }
    }

    stream_framesize_ = target_size;
    stream_pixformat_ = PIXFORMAT_RGB565;
    stream_res_str_ = norm_name;

    // Do NOT save to NVS Settings - streaming resolution is strictly volatile in RAM.
    // When ESP32 is powered off / rebooted, it will automatically return to default QVGA.

    if (was_streaming) {
        stream_paused_.store(false);
    }

    ESP_LOGI(TAG, "Streaming resolution successfully updated to %s (%dx%d RGB565) [volatile]",
             norm_name.c_str(), resolution[target_size].width, resolution[target_size].height);
    return true;
}

void Esp32Camera::OnDeviceStateChanged(DeviceState state) {
    if (state == kDeviceStateIdle || state == kDeviceStateListening) {
        if (resume_stream_on_idle_.exchange(false)) {
            ESP_LOGI(TAG, "Device returned to %s. Resuming live camera stream after photo capture...",
                     state == kDeviceStateIdle ? "idle" : "listening");
            stream_paused_.store(false);
        }
    }
}

