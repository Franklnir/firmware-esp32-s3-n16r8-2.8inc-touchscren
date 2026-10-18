#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <mutex>

#include "camera.h"
#include "esp_camera.h"
#include "jpg/image_to_jpeg.h"

struct JpegChunk
{
    uint8_t *data;
    size_t len;
};

class Esp32Camera : public Camera
{
private:
    camera_config_t config_{};
    framesize_t photo_framesize_ = FRAMESIZE_QVGA;
    pixformat_t photo_pixformat_ = PIXFORMAT_JPEG;
    framesize_t stream_framesize_ = FRAMESIZE_QVGA;
    pixformat_t stream_pixformat_ = PIXFORMAT_RGB565;
    std::string stream_res_str_ = "qvga";
    mutable std::mutex camera_mutex_;
    std::atomic<bool> stream_paused_{false};

    bool streaming_on_ = false;
    bool swap_bytes_enabled_ = true;  // Swap pixel byte order for RGB565, enabled by default
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    camera_fb_t *current_fb_ = nullptr;
    uint8_t *encode_buf_ = nullptr;  // Buffer for JPEG encoding (with optional byte swap)
    size_t encode_buf_size_ = 0;

    // Captured high-resolution photo buffer (Full HD JPEG)
    uint8_t *captured_jpeg_buf_ = nullptr;
    size_t captured_jpeg_len_ = 0;
    uint16_t captured_width_ = 0;
    uint16_t captured_height_ = 0;
    
    // Streaming
    std::atomic<bool> is_streaming_{false};
    std::atomic<bool> stream_frame_busy_{false};
    std::atomic<int64_t> stream_resume_time_us_{0};
    std::atomic<bool> resume_stream_on_idle_{false};
    std::thread stream_thread_;
    std::function<void(uint8_t* data, size_t len, uint16_t w, uint16_t h)> stream_callback_;
    
    // Capture animation
    std::function<void()> capture_callback_;

    bool hmirror_enabled_ = true;  // Default: Mirror ON (selfie mirror view)
    bool vflip_enabled_ = false;

    void ShowCapturedPhotoOnDisplay();

public:
    Esp32Camera(const camera_config_t &config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string &url, const std::string &token) override;
    virtual bool Capture() override;
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool IsHMirror() const override { return hmirror_enabled_; }
    virtual bool IsVFlip() const override { return vflip_enabled_; }
    virtual bool SetSwapBytes(bool enabled) override;
    virtual std::string Explain(const std::string &question) override;
    
    // Photo resolution control
    void SetPhotoFrameSize(framesize_t size) { photo_framesize_ = size; }
    framesize_t GetPhotoFrameSize() const { return photo_framesize_; }

    // Streaming methods
    virtual bool StartStreaming() override;
    virtual void StopStreaming() override;
    virtual bool IsStreaming() const override { return is_streaming_.load(); }
    virtual void SetStreamCallback(std::function<void(uint8_t* data, size_t len, uint16_t w, uint16_t h)> callback) override;
    
    // Capture animation callback
    virtual void SetCaptureCallback(std::function<void()> callback) override { capture_callback_ = callback; }

    // Streaming resolution control
    virtual std::string GetSupportedStreamingResolutions() const override;
    virtual bool SetStreamingResolution(const std::string& res_str) override;
    virtual std::string GetStreamingResolution() const override;
    virtual void OnDeviceStateChanged(DeviceState state) override;
};
