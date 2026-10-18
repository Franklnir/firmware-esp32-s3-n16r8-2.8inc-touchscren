#ifndef CAMERA_H
#define CAMERA_H

#include <string>
#include <functional>
#include "device_state.h"

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool IsHMirror() const { return false; }
    virtual bool IsVFlip() const { return false; }
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;
    
    // Streaming methods
    virtual bool StartStreaming() { return false; }
    virtual void StopStreaming() {}
    virtual bool IsStreaming() const { return false; }
    virtual void SetStreamCallback(std::function<void(uint8_t* data, size_t len, uint16_t w, uint16_t h)> callback) {}
    
    // Capture animation callback
    virtual void SetCaptureCallback(std::function<void()> callback) {}

    // Streaming resolution control
    virtual std::string GetSupportedStreamingResolutions() const { return ""; }
    virtual bool SetStreamingResolution(const std::string& res_str) { return false; }
    virtual std::string GetStreamingResolution() const { return ""; }
    virtual void OnDeviceStateChanged(DeviceState state) {}
};

#endif  // CAMERA_H
