#include "mccoutlet/StreamThread.hpp"
#include "mccoutlet/PowerAssertion.hpp"
#include <iostream>

namespace mccoutlet {

StreamThread::StreamThread(
    std::unique_ptr<IDevice> device,
    StatusCallback callback
)
    : device_(std::move(device))
    , statusCallback_(std::move(callback))
{
}

StreamThread::~StreamThread() {
    stop();
}

bool StreamThread::start() {
    if (running_) {
        return false;
    }

    if (!device_) {
        if (statusCallback_) {
            statusCallback_("No device configured", true);
        }
        return false;
    }

    if (!device_->connect()) {
        if (statusCallback_) {
            statusCallback_("Failed to connect to device", true);
        }
        return false;
    }

    if (!device_->startAcquisition()) {
        if (statusCallback_) {
            statusCallback_("Failed to start device acquisition", true);
        }
        device_->disconnect();
        return false;
    }

    shutdown_ = false;
    running_ = true;
    thread_ = std::make_unique<std::thread>(&StreamThread::threadFunction, this);

    if (statusCallback_) {
        statusCallback_("Streaming started", false);
    }

    return true;
}

void StreamThread::stop() {
    if (!running_) {
        return;
    }

    shutdown_ = true;

    // Break any in-progress getData*() call before joining. Otherwise a
    // wedged device leaves the worker spinning inside getData*() (which only
    // checks the device's own abort flag, not shutdown_) and join() blocks
    // forever, freezing the caller (the GUI thread).
    if (device_) {
        device_->requestStop();
    }

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();

    if (device_) {
        device_->disconnect();
    }

    running_ = false;

    if (statusCallback_) {
        statusCallback_("Streaming stopped", false);
    }
}

bool StreamThread::isRunning() const {
    return running_;
}

DeviceInfo StreamThread::getDeviceInfo() const {
    if (device_) {
        return device_->getInfo();
    }
    return {};
}

void StreamThread::threadFunction() {
    // Keep the host awake for the whole streaming session. Idle system sleep
    // suspends the USB bus and wedges the DAQ's in-flight transfers (uldaq then
    // reports "still xfer pending" and the scan cannot be recovered), and App
    // Nap can throttle the libusb event thread enough to overrun the device
    // FIFO. Released automatically when this function returns (stop, error, or
    // shutdown). No-op on non-Apple platforms.
    PowerAssertion keep_awake("Streaming MCC DAQ to LSL");

    try {
        auto info = device_->getInfo();
        LSLOutlet outlet(info);

        if (statusCallback_) {
            statusCallback_("LSL outlet created: " + info.name, false);
        }

        // Reserve ~1 second of capacity; getData resizes to actual available data
        size_t reserve_elements =
            static_cast<size_t>(info.sample_rate) * info.channel_count;
        double timestamp = 0.0;

        if (info.scaled) {
            std::vector<float> buffer;
            buffer.reserve(reserve_elements);
            while (!shutdown_) {
                if (device_->getData(buffer, timestamp)) {
                    outlet.pushChunk(buffer, timestamp);
                } else if (!shutdown_) {
                    if (statusCallback_) statusCallback_("Device acquisition error", true);
                    break;
                }
            }
        } else if (info.resolution_bits <= 16) {
            std::vector<int16_t> buffer;
            buffer.reserve(reserve_elements);
            while (!shutdown_) {
                if (device_->getDataInt16(buffer, timestamp)) {
                    outlet.pushChunk(buffer, timestamp);
                } else if (!shutdown_) {
                    if (statusCallback_) statusCallback_("Device acquisition error", true);
                    break;
                }
            }
        } else {
            std::vector<int32_t> buffer;
            buffer.reserve(reserve_elements);
            while (!shutdown_) {
                if (device_->getDataInt32(buffer, timestamp)) {
                    outlet.pushChunk(buffer, timestamp);
                } else if (!shutdown_) {
                    if (statusCallback_) statusCallback_("Device acquisition error", true);
                    break;
                }
            }
        }

    } catch (const std::exception& e) {
        if (statusCallback_) {
            statusCallback_(std::string("Streaming error: ") + e.what(), true);
        }
    }

    running_ = false;
}

} // namespace mccoutlet
