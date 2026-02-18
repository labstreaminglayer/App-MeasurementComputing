#pragma once
/**
 * @file StreamThread.hpp
 * @brief Background thread for LSL streaming
 */

#include "Device.hpp"
#include "LSLOutlet.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace mccoutlet {

using StatusCallback = std::function<void(const std::string& message, bool is_error)>;

class StreamThread {
public:
    explicit StreamThread(
        std::unique_ptr<IDevice> device,
        StatusCallback callback = nullptr
    );

    ~StreamThread();

    StreamThread(const StreamThread&) = delete;
    StreamThread& operator=(const StreamThread&) = delete;

    bool start();
    void stop();
    bool isRunning() const;
    DeviceInfo getDeviceInfo() const;

private:
    void threadFunction();

    std::unique_ptr<IDevice> device_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_{false};
    StatusCallback statusCallback_;
};

} // namespace mccoutlet
