#include "mccoutlet/Device.hpp"

#include <uldaq.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace mccoutlet {

namespace {

constexpr int MAX_DEV_COUNT = 100;

// Seconds of data to buffer in the circular scan buffer.
// Larger values tolerate more jitter but use more memory.
constexpr int SCAN_BUFFER_SECONDS = 10;

std::string ulErrorString(UlError err) {
    char msg[ERR_MSG_LEN]{};
    ulGetErrMsg(err, msg);
    return std::string(msg);
}

std::string rangeToString(int range_id) {
    switch (static_cast<Range>(range_id)) {
        case BIP60VOLTS:    return "+/-60V";
        case BIP30VOLTS:    return "+/-30V";
        case BIP15VOLTS:    return "+/-15V";
        case BIP20VOLTS:    return "+/-20V";
        case BIP10VOLTS:    return "+/-10V";
        case BIP5VOLTS:     return "+/-5V";
        case BIP4VOLTS:     return "+/-4V";
        case BIP2PT5VOLTS:  return "+/-2.5V";
        case BIP2VOLTS:     return "+/-2V";
        case BIP1PT25VOLTS: return "+/-1.25V";
        case BIP1VOLTS:     return "+/-1V";
        case BIPPT625VOLTS: return "+/-0.625V";
        case BIPPT5VOLTS:   return "+/-0.5V";
        case BIPPT25VOLTS:  return "+/-0.25V";
        case BIPPT125VOLTS: return "+/-0.125V";
        case BIPPT2VOLTS:   return "+/-0.2V";
        case BIPPT1VOLTS:   return "+/-0.1V";
        case BIPPT078VOLTS: return "+/-0.078V";
        case BIPPT05VOLTS:  return "+/-0.05V";
        case BIPPT01VOLTS:  return "+/-0.01V";
        case BIPPT005VOLTS: return "+/-0.005V";
        case BIP3VOLTS:     return "+/-3V";
        case BIPPT312VOLTS: return "+/-0.312V";
        case BIPPT156VOLTS: return "+/-0.156V";
        case UNI60VOLTS:    return "0-60V";
        case UNI30VOLTS:    return "0-30V";
        case UNI15VOLTS:    return "0-15V";
        case UNI20VOLTS:    return "0-20V";
        case UNI10VOLTS:    return "0-10V";
        case UNI5VOLTS:     return "0-5V";
        case UNI4VOLTS:     return "0-4V";
        case UNI2PT5VOLTS:  return "0-2.5V";
        case UNI2VOLTS:     return "0-2V";
        case UNI1PT25VOLTS: return "0-1.25V";
        case UNI1VOLTS:     return "0-1V";
        case UNIPT625VOLTS: return "0-0.625V";
        case UNIPT5VOLTS:   return "0-0.5V";
        case UNIPT25VOLTS:  return "0-0.25V";
        case UNIPT125VOLTS: return "0-0.125V";
        case UNIPT2VOLTS:   return "0-0.2V";
        case UNIPT1VOLTS:   return "0-0.1V";
        case UNIPT078VOLTS: return "0-0.078V";
        case UNIPT05VOLTS:  return "0-0.05V";
        case UNIPT01VOLTS:  return "0-0.01V";
        case UNIPT005VOLTS: return "0-0.005V";
        case MA0TO20:       return "0-20mA";
        default:            return "Range(" + std::to_string(range_id) + ")";
    }
}

std::pair<double, double> rangeToVoltage(int range_id) {
    switch (static_cast<Range>(range_id)) {
        case BIP60VOLTS:    return {-60.0, 60.0};
        case BIP30VOLTS:    return {-30.0, 30.0};
        case BIP15VOLTS:    return {-15.0, 15.0};
        case BIP20VOLTS:    return {-20.0, 20.0};
        case BIP10VOLTS:    return {-10.0, 10.0};
        case BIP5VOLTS:     return {-5.0, 5.0};
        case BIP4VOLTS:     return {-4.0, 4.0};
        case BIP2PT5VOLTS:  return {-2.5, 2.5};
        case BIP2VOLTS:     return {-2.0, 2.0};
        case BIP1PT25VOLTS: return {-1.25, 1.25};
        case BIP1VOLTS:     return {-1.0, 1.0};
        case BIPPT625VOLTS: return {-0.625, 0.625};
        case BIPPT5VOLTS:   return {-0.5, 0.5};
        case BIPPT25VOLTS:  return {-0.25, 0.25};
        case BIPPT125VOLTS: return {-0.125, 0.125};
        case BIPPT2VOLTS:   return {-0.2, 0.2};
        case BIPPT1VOLTS:   return {-0.1, 0.1};
        case BIPPT078VOLTS: return {-0.078, 0.078};
        case BIPPT05VOLTS:  return {-0.05, 0.05};
        case BIPPT01VOLTS:  return {-0.01, 0.01};
        case BIPPT005VOLTS: return {-0.005, 0.005};
        case BIP3VOLTS:     return {-3.0, 3.0};
        case BIPPT312VOLTS: return {-0.312, 0.312};
        case BIPPT156VOLTS: return {-0.156, 0.156};
        case UNI60VOLTS:    return {0.0, 60.0};
        case UNI30VOLTS:    return {0.0, 30.0};
        case UNI15VOLTS:    return {0.0, 15.0};
        case UNI20VOLTS:    return {0.0, 20.0};
        case UNI10VOLTS:    return {0.0, 10.0};
        case UNI5VOLTS:     return {0.0, 5.0};
        case UNI4VOLTS:     return {0.0, 4.0};
        case UNI2PT5VOLTS:  return {0.0, 2.5};
        case UNI2VOLTS:     return {0.0, 2.0};
        case UNI1PT25VOLTS: return {0.0, 1.25};
        case UNI1VOLTS:     return {0.0, 1.0};
        case UNIPT625VOLTS: return {0.0, 0.625};
        case UNIPT5VOLTS:   return {0.0, 0.5};
        case UNIPT25VOLTS:  return {0.0, 0.25};
        case UNIPT125VOLTS: return {0.0, 0.125};
        case UNIPT2VOLTS:   return {0.0, 0.2};
        case UNIPT1VOLTS:   return {0.0, 0.1};
        case UNIPT078VOLTS: return {0.0, 0.078};
        case UNIPT05VOLTS:  return {0.0, 0.05};
        case UNIPT01VOLTS:  return {0.0, 0.01};
        case UNIPT005VOLTS: return {0.0, 0.005};
        case MA0TO20:       return {0.0, 20.0};
        default:            return {0.0, 0.0};
    }
}

} // anonymous namespace

// ============================================================================
// Discovery
// ============================================================================

std::vector<DiscoveredDevice> MCCDevice::discover() {
    DaqDeviceDescriptor descriptors[MAX_DEV_COUNT];
    unsigned int numDevs = MAX_DEV_COUNT;

    UlError err = ulGetDaqDeviceInventory(ANY_IFC, descriptors, &numDevs);
    if (err != ERR_NO_ERROR) {
        return {};
    }

    std::vector<DiscoveredDevice> result;
    result.reserve(numDevs);

    for (unsigned int i = 0; i < numDevs; ++i) {
        std::string ifc;
        switch (descriptors[i].devInterface) {
            case USB_IFC:       ifc = "USB"; break;
            case BLUETOOTH_IFC: ifc = "Bluetooth"; break;
            case ETHERNET_IFC:  ifc = "Ethernet"; break;
            default:            ifc = "Unknown"; break;
        }
        result.push_back({
            .index = static_cast<int>(i),
            .product_name = descriptors[i].productName,
            .unique_id = descriptors[i].uniqueId,
            .interface_name = ifc
        });
    }

    return result;
}

// ============================================================================
// MCCDevice
// ============================================================================

MCCDevice::MCCDevice(const Config& config, StatusCallback callback)
    : config_(config)
    , statusCallback_(std::move(callback))
{
}

MCCDevice::~MCCDevice() {
    disconnect();
}

bool MCCDevice::connect() {
    if (connected_) return true;
    disconnecting_ = false;

    // --- Discover devices ---
    DaqDeviceDescriptor descriptors[MAX_DEV_COUNT];
    unsigned int numDevs = MAX_DEV_COUNT;

    UlError err = ulGetDaqDeviceInventory(ANY_IFC, descriptors, &numDevs);
    if (err != ERR_NO_ERROR) {
        throw std::runtime_error("ulGetDaqDeviceInventory failed: " + ulErrorString(err));
    }
    if (numDevs == 0) {
        throw std::runtime_error("No MCC DAQ devices found");
    }
    if (config_.device_index < 0 || config_.device_index >= static_cast<int>(numDevs)) {
        throw std::runtime_error(
            "Device index " + std::to_string(config_.device_index) +
            " out of range (found " + std::to_string(numDevs) + " devices)");
    }

    auto& desc = descriptors[config_.device_index];
    product_name_ = desc.productName;
    unique_id_ = desc.uniqueId;

    // --- Create and connect ---
    handle_ = ulCreateDaqDevice(desc);
    if (handle_ == 0) {
        throw std::runtime_error("Failed to create DAQ device handle");
    }

    err = ulConnectDaqDevice(handle_);
    if (err != ERR_NO_ERROR) {
        ulReleaseDaqDevice(handle_);
        handle_ = 0;
        throw std::runtime_error("ulConnectDaqDevice failed: " + ulErrorString(err));
    }

    // --- Query device capabilities ---
    long long numChans = 0;
    long long hasPacer = 0;

    // Try single-ended first, then differential
    err = ulAIGetInfo(handle_, AI_INFO_NUM_CHANS_BY_MODE, AI_SINGLE_ENDED, &numChans);
    if (err == ERR_NO_ERROR && numChans > 0) {
        input_mode_ = AI_SINGLE_ENDED;
        capabilities_.input_mode_name = "Single-Ended";
    } else {
        err = ulAIGetInfo(handle_, AI_INFO_NUM_CHANS_BY_MODE, AI_DIFFERENTIAL, &numChans);
        if (err == ERR_NO_ERROR && numChans > 0) {
            input_mode_ = AI_DIFFERENTIAL;
            capabilities_.input_mode_name = "Differential";
        } else {
            disconnect();
            throw std::runtime_error("Device has no supported analog input mode");
        }
    }

    capabilities_.max_channels = static_cast<int>(numChans);

    // Clamp high_channel to available channels
    if (config_.high_channel >= static_cast<int>(numChans)) {
        config_.high_channel = static_cast<int>(numChans) - 1;
    }

    // Verify hardware pacer
    ulAIGetInfo(handle_, AI_INFO_HAS_PACER, 0, &hasPacer);
    if (!hasPacer) {
        disconnect();
        throw std::runtime_error("Device does not support hardware-paced analog input");
    }

    // Query resolution
    long long resolution = 0;
    err = ulAIGetInfo(handle_, AI_INFO_RESOLUTION, 0, &resolution);
    if (err == ERR_NO_ERROR) {
        capabilities_.resolution_bits = static_cast<int>(resolution);
    }

    // Query scan rate limits
    double minRate = 0.0, maxRate = 0.0;
    err = ulAIGetInfoDbl(handle_, AI_INFO_MIN_SCAN_RATE, 0, &minRate);
    if (err == ERR_NO_ERROR) {
        capabilities_.min_scan_rate = minRate;
    }
    err = ulAIGetInfoDbl(handle_, AI_INFO_MAX_SCAN_RATE, 0, &maxRate);
    if (err == ERR_NO_ERROR) {
        capabilities_.max_scan_rate = maxRate;
    }

    // Query ALL supported ranges for the detected input mode
    long long numRanges = 0;
    AiInfoItem rangeCountItem = (input_mode_ == AI_SINGLE_ENDED)
        ? AI_INFO_NUM_SE_RANGES : AI_INFO_NUM_DIFF_RANGES;
    AiInfoItem rangeItem = (input_mode_ == AI_SINGLE_ENDED)
        ? AI_INFO_SE_RANGE : AI_INFO_DIFF_RANGE;

    ulAIGetInfo(handle_, rangeCountItem, 0, &numRanges);
    if (numRanges <= 0) {
        disconnect();
        throw std::runtime_error("Device reports no supported voltage ranges");
    }

    capabilities_.available_ranges.clear();
    for (long long i = 0; i < numRanges; ++i) {
        long long rangeVal = 0;
        err = ulAIGetInfo(handle_, rangeItem, static_cast<unsigned int>(i), &rangeVal);
        if (err == ERR_NO_ERROR) {
            capabilities_.available_ranges.push_back({
                .id = static_cast<int>(rangeVal),
                .label = rangeToString(static_cast<int>(rangeVal))
            });
        }
    }

    // Select range: user-specified or first available
    if (config_.range >= 0) {
        // Validate that the requested range is available
        bool found = false;
        for (const auto& r : capabilities_.available_ranges) {
            if (r.id == config_.range) {
                found = true;
                break;
            }
        }
        if (!found) {
            disconnect();
            throw std::runtime_error(
                "Requested range " + rangeToString(config_.range) +
                " is not supported by this device");
        }
        range_ = config_.range;
    } else if (!capabilities_.available_ranges.empty()) {
        range_ = capabilities_.available_ranges.front().id;
    }

    connected_ = true;
    return true;
}

bool MCCDevice::startAcquisition() {
    if (!connected_ || handle_ == 0) {
        throw std::runtime_error("Device not connected");
    }
    if (scanning_) return true;

    // Validate sample rate against device limits
    if (capabilities_.min_scan_rate > 0 && config_.sample_rate < capabilities_.min_scan_rate) {
        throw std::runtime_error(
            "Sample rate " + std::to_string(config_.sample_rate) +
            " Hz is below device minimum of " + std::to_string(capabilities_.min_scan_rate) + " Hz");
    }
    if (capabilities_.max_scan_rate > 0 && config_.sample_rate > capabilities_.max_scan_rate) {
        throw std::runtime_error(
            "Sample rate " + std::to_string(config_.sample_rate) +
            " Hz exceeds device maximum of " + std::to_string(capabilities_.max_scan_rate) + " Hz");
    }

    // Allocate scan buffer and start acquisition
    int channelCount = config_.high_channel - config_.low_channel + 1;
    scan_buffer_samples_per_chan_ = static_cast<int>(config_.sample_rate * SCAN_BUFFER_SECONDS);
    scan_buffer_.resize(
        static_cast<size_t>(channelCount) * scan_buffer_samples_per_chan_, 0.0);

    actual_rate_ = config_.sample_rate;
    ScanOption options = static_cast<ScanOption>(SO_DEFAULTIO | SO_CONTINUOUS);

    AInScanFlag scan_flags = AINSCAN_FF_DEFAULT;
    if (!config_.scaled) {
        scan_flags = static_cast<AInScanFlag>(
            AINSCAN_FF_NOSCALEDATA | AINSCAN_FF_NOCALIBRATEDATA);
    }

    UlError err = ulAInScan(
        handle_,
        config_.low_channel,
        config_.high_channel,
        static_cast<AiInputMode>(input_mode_),
        static_cast<Range>(range_),
        scan_buffer_samples_per_chan_,
        &actual_rate_,
        options,
        scan_flags,
        scan_buffer_.data()
    );

    if (err != ERR_NO_ERROR) {
        scan_buffer_.clear();
        throw std::runtime_error("ulAInScan failed: " + ulErrorString(err));
    }

    scans_read_ = 0;
    overrun_count_ = 0;
    scanning_ = true;
    return true;
}

bool MCCDevice::restartScan() {
    // Stop the current (failed) scan
    ulAInScanStop(handle_);

    // Brief pause to let USB transfers settle
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Restart the scan into the same buffer
    actual_rate_ = config_.sample_rate;
    ScanOption options = static_cast<ScanOption>(SO_DEFAULTIO | SO_CONTINUOUS);

    AInScanFlag scan_flags = AINSCAN_FF_DEFAULT;
    if (!config_.scaled) {
        scan_flags = static_cast<AInScanFlag>(
            AINSCAN_FF_NOSCALEDATA | AINSCAN_FF_NOCALIBRATEDATA);
    }

    UlError err = ulAInScan(
        handle_,
        config_.low_channel,
        config_.high_channel,
        static_cast<AiInputMode>(input_mode_),
        static_cast<Range>(range_),
        scan_buffer_samples_per_chan_,
        &actual_rate_,
        options,
        scan_flags,
        scan_buffer_.data()
    );

    if (err != ERR_NO_ERROR) {
        return false;
    }

    scans_read_ = 0;
    overrun_count_++;

    if (statusCallback_) {
        statusCallback_(
            "Device FIFO overrun detected, scan restarted (overrun #" +
            std::to_string(overrun_count_) + ")", true);
    }

    return true;
}

void MCCDevice::disconnect() {
    disconnecting_ = true;

    if (handle_ != 0) {
        if (scanning_) {
            ulAInScanStop(handle_);
            scanning_ = false;
        }
        ulDisconnectDaqDevice(handle_);
        ulReleaseDaqDevice(handle_);
        handle_ = 0;
    }

    scan_buffer_.clear();
    connected_ = false;
}

bool MCCDevice::isConnected() const {
    return connected_;
}

DeviceInfo MCCDevice::getInfo() const {
    int channelCount = config_.high_channel - config_.low_channel + 1;
    auto [rmin, rmax] = rangeToVoltage(range_);
    return {
        .name = config_.stream_name,
        .type = config_.stream_type,
        .channel_count = channelCount,
        .sample_rate = actual_rate_ > 0 ? actual_rate_ : config_.sample_rate,
        .source_id = product_name_ + "_" + unique_id_,
        .resolution_bits = capabilities_.resolution_bits,
        .scaled = config_.scaled,
        .range_min = rmin,
        .range_max = rmax,
        .range_label = rangeToString(range_)
    };
}

DeviceCapabilities MCCDevice::getCapabilities() const {
    return capabilities_;
}

bool MCCDevice::getData(std::vector<float>& buffer) {
    if (!connected_ || handle_ == 0) return false;

    const int channelCount = config_.high_channel - config_.low_channel + 1;
    const int samples_needed = static_cast<int>(buffer.size()) / channelCount;
    const size_t total_buffer_elements = scan_buffer_.size();

    while (!disconnecting_) {
        ScanStatus status{};
        TransferStatus xfer{};

        UlError err = ulAInScanStatus(handle_, &status, &xfer);
        if (err != ERR_NO_ERROR || status != SS_RUNNING) {
            if (disconnecting_) return false;
            // Overrun or other scan failure — attempt restart
            if (!restartScan()) return false;
            continue;
        }

        // currentScanCount = per-channel samples transferred since scan start
        long long available = static_cast<long long>(xfer.currentScanCount) -
                              static_cast<long long>(scans_read_);

        if (available >= samples_needed) {
            // Calculate read position in the circular buffer
            size_t read_offset =
                (static_cast<size_t>(scans_read_) * channelCount) % total_buffer_elements;

            // Copy data, converting double -> float, handling wrap-around
            for (size_t i = 0; i < buffer.size(); ++i) {
                buffer[i] = static_cast<float>(
                    scan_buffer_[(read_offset + i) % total_buffer_elements]);
            }

            scans_read_ += samples_needed;
            return true;
        }

        // Sleep briefly before polling again
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

bool MCCDevice::getDataInt32(std::vector<int32_t>& buffer) {
    if (!connected_ || handle_ == 0) return false;

    const int channelCount = config_.high_channel - config_.low_channel + 1;
    const int samples_needed = static_cast<int>(buffer.size()) / channelCount;
    const size_t total_buffer_elements = scan_buffer_.size();

    while (!disconnecting_) {
        ScanStatus status{};
        TransferStatus xfer{};

        UlError err = ulAInScanStatus(handle_, &status, &xfer);
        if (err != ERR_NO_ERROR || status != SS_RUNNING) {
            if (disconnecting_) return false;
            if (!restartScan()) return false;
            continue;
        }

        long long available = static_cast<long long>(xfer.currentScanCount) -
                              static_cast<long long>(scans_read_);

        if (available >= samples_needed) {
            size_t read_offset =
                (static_cast<size_t>(scans_read_) * channelCount) % total_buffer_elements;

            for (size_t i = 0; i < buffer.size(); ++i) {
                buffer[i] = static_cast<int32_t>(
                    scan_buffer_[(read_offset + i) % total_buffer_elements]);
            }

            scans_read_ += samples_needed;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

bool MCCDevice::getDataInt16(std::vector<int16_t>& buffer) {
    if (!connected_ || handle_ == 0) return false;

    const int channelCount = config_.high_channel - config_.low_channel + 1;
    const int samples_needed = static_cast<int>(buffer.size()) / channelCount;
    const size_t total_buffer_elements = scan_buffer_.size();

    while (!disconnecting_) {
        ScanStatus status{};
        TransferStatus xfer{};

        UlError err = ulAInScanStatus(handle_, &status, &xfer);
        if (err != ERR_NO_ERROR || status != SS_RUNNING) {
            if (disconnecting_) return false;
            if (!restartScan()) return false;
            continue;
        }

        long long available = static_cast<long long>(xfer.currentScanCount) -
                              static_cast<long long>(scans_read_);

        if (available >= samples_needed) {
            size_t read_offset =
                (static_cast<size_t>(scans_read_) * channelCount) % total_buffer_elements;

            for (size_t i = 0; i < buffer.size(); ++i) {
                buffer[i] = static_cast<int16_t>(
                    scan_buffer_[(read_offset + i) % total_buffer_elements]);
            }

            scans_read_ += samples_needed;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

} // namespace mccoutlet
