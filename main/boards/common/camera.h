#ifndef CAMERA_H
#define CAMERA_H

#include <expected>
#include <functional>
#include <string>

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual bool StartStreaming() { return false; }
    virtual void StopStreaming() {}
    virtual bool IsStreaming() const { return false; }
    virtual void SetStreamCallback(
        std::function<void(uint8_t* data, size_t len, uint16_t width, uint16_t height)> callback) {
        (void)callback;
    }
    virtual void SetCaptureCallback(std::function<void()> callback) { (void)callback; }
    virtual std::string GetSupportedStreamingResolutions() const { return {}; }
    virtual bool SetStreamingResolution(const std::string& resolution) {
        (void)resolution;
        return false;
    }
    virtual std::string GetStreamingResolution() const { return {}; }
    virtual std::expected<std::string, std::string> Explain(const std::string& question) = 0;
    virtual std::expected<std::string, std::string> Explain(const std::string& question,
                                                            const std::string& url,
                                                            const std::string& token) = 0;
};

#endif  // CAMERA_H
