#pragma once
/**
 * @file LSLOutlet.hpp
 * @brief LSL stream outlet wrapper
 */

#include "Device.hpp"
#include <lsl_cpp.h>
#include <memory>
#include <string>
#include <vector>

namespace mccoutlet {

class LSLOutlet {
public:
    explicit LSLOutlet(const DeviceInfo& info);
    ~LSLOutlet();

    LSLOutlet(const LSLOutlet&) = delete;
    LSLOutlet& operator=(const LSLOutlet&) = delete;
    LSLOutlet(LSLOutlet&&) noexcept = default;
    LSLOutlet& operator=(LSLOutlet&&) noexcept = default;

    void pushChunk(const std::vector<float>& data, double timestamp = 0.0);
    void pushChunk(const std::vector<int32_t>& data, double timestamp = 0.0);
    void pushChunk(const std::vector<int16_t>& data, double timestamp = 0.0);
    void pushSample(const std::vector<float>& sample);
    std::string getStreamName() const;
    bool hasConsumers() const;

private:
    std::unique_ptr<lsl::stream_outlet> outlet_;
    DeviceInfo info_;
};

} // namespace mccoutlet
