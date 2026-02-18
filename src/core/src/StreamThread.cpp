#include "mccoutlet/StreamThread.hpp"
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
    try {
        auto info = device_->getInfo();
        LSLOutlet outlet(info);

        if (statusCallback_) {
            statusCallback_("LSL outlet created: " + info.name, false);
        }

        // Buffer ~100ms of data per chunk, minimum 1 sample
        size_t samples_per_chunk = std::max(
            1,
            static_cast<int>(info.sample_rate * 0.1)
        );
        size_t chunk_elements = samples_per_chunk * info.channel_count;

        if (info.scaled) {
            // Scaled mode: calibrated voltage as float
            std::vector<float> buffer(chunk_elements);
            while (!shutdown_) {
                if (device_->getData(buffer)) {
                    outlet.pushChunk(buffer);
                } else if (!shutdown_) {
                    if (statusCallback_) statusCallback_("Device acquisition error", true);
                    break;
                }
            }
        } else if (info.resolution_bits <= 16) {
            // Raw mode, <=16-bit ADC: integer counts as int16
            std::vector<int16_t> buffer(chunk_elements);
            while (!shutdown_) {
                if (device_->getDataInt16(buffer)) {
                    outlet.pushChunk(buffer);
                } else if (!shutdown_) {
                    if (statusCallback_) statusCallback_("Device acquisition error", true);
                    break;
                }
            }
        } else {
            // Raw mode, >16-bit ADC: integer counts as int32
            std::vector<int32_t> buffer(chunk_elements);
            while (!shutdown_) {
                if (device_->getDataInt32(buffer)) {
                    outlet.pushChunk(buffer);
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
