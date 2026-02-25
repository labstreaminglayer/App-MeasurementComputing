#pragma once
/**
 * @file Device.hpp
 * @brief MCC DAQ device interface using the uldaq library
 *
 * Implements IDevice for Measurement Computing USB DAQ devices.
 * Uses ulAInScan for continuous background acquisition.
 */

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward-declare uldaq types to avoid exposing the C header in this interface.
// The actual uldaq.h is included only in Device.cpp.
using DaqDeviceHandle = long long;

namespace mccoutlet {

struct VoltageRange {
    int id;             ///< uldaq Range enum value
    std::string label;  ///< Human-readable label, e.g. "+/-5V"
};

struct DeviceCapabilities {
    std::string input_mode_name;    ///< "Single-Ended" or "Differential"
    int max_channels = 0;
    int resolution_bits = 0;
    double min_scan_rate = 0.0;
    double max_scan_rate = 0.0;
    std::vector<VoltageRange> available_ranges;
};

struct DeviceInfo {
    std::string name;
    std::string type;
    int channel_count = 1;
    double sample_rate = 0.0;
    std::string source_id;
    int resolution_bits = 0;
    bool scaled = true;
    double range_min = 0.0;
    double range_max = 0.0;
    std::string range_label;
};

/**
 * @brief Abstract base class for device implementations
 */
class IDevice {
public:
    virtual ~IDevice() = default;
    virtual bool connect() = 0;
    virtual bool startAcquisition() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual DeviceInfo getInfo() const = 0;
    virtual DeviceCapabilities getCapabilities() const = 0;

    /**
     * @brief Retrieve all available data from the device
     * @param buffer Resized to fit all available channel-interleaved samples
     * @param timestamp Output: LSL timestamp of the most recent sample
     * @return true if data was retrieved, false on error or shutdown
     *
     * Blocks until at least one scan is available, then drains everything
     * the device has buffered. The timestamp is captured at the moment
     * data availability is detected.
     */
    virtual bool getData(std::vector<float>& buffer, double& timestamp) = 0;

    /** @brief Retrieve all available raw data as int32 (for >16-bit ADC in raw mode) */
    virtual bool getDataInt32(std::vector<int32_t>& buffer, double& timestamp) = 0;

    /** @brief Retrieve all available raw data as int16 (for <=16-bit ADC in raw mode) */
    virtual bool getDataInt16(std::vector<int16_t>& buffer, double& timestamp) = 0;
};

/**
 * @brief Description of a discovered MCC DAQ device
 */
struct DiscoveredDevice {
    int index;                  ///< Index in the inventory (for Config::device_index)
    std::string product_name;   ///< e.g. "USB-1608FS-Plus"
    std::string unique_id;      ///< Serial number or MAC address
    std::string interface_name; ///< "USB", "Bluetooth", "Ethernet"
};

/**
 * @brief MCC DAQ device using the uldaq C library
 *
 * Wraps ulAInScan for continuous analog input acquisition.
 * Usage: construct, call connect() to query capabilities, then
 * startAcquisition() to begin scanning.
 */
class MCCDevice : public IDevice {
public:
    /**
     * @brief Discover all connected MCC DAQ devices
     * @return List of discovered devices (may be empty)
     */
    static std::vector<DiscoveredDevice> discover();

    struct Config {
        std::string stream_name = "MCCDaq";
        std::string stream_type = "RawBrainSignal";
        int device_index = 0;       ///< Index of device in discovered inventory
        int low_channel = 0;
        int high_channel = 7;
        double sample_rate = 44100.0;
        int range = -1;             ///< uldaq Range enum value, -1 = auto-select first
        bool scaled = true;         ///< true = calibrated voltage (float), false = raw ADC (int)
    };

    using StatusCallback = std::function<void(const std::string& message, bool is_error)>;

    explicit MCCDevice(const Config& config, StatusCallback callback = nullptr);
    ~MCCDevice() override;

    bool connect() override;
    bool startAcquisition() override;
    void disconnect() override;
    bool isConnected() const override;
    DeviceInfo getInfo() const override;
    DeviceCapabilities getCapabilities() const override;
    bool getData(std::vector<float>& buffer, double& timestamp) override;
    bool getDataInt32(std::vector<int32_t>& buffer, double& timestamp) override;
    bool getDataInt16(std::vector<int16_t>& buffer, double& timestamp) override;

private:
    bool restartScan();

    Config config_;
    StatusCallback statusCallback_;
    DaqDeviceHandle handle_ = 0;
    bool connected_ = false;
    bool scanning_ = false;
    std::atomic<bool> disconnecting_{false};
    int overrun_count_ = 0;

    // ulAInScan circular buffer (double precision, as required by uldaq)
    std::vector<double> scan_buffer_;
    int scan_buffer_samples_per_chan_ = 0;

    // Track read position (cumulative scan count consumed)
    unsigned long long scans_read_ = 0;

    // Detected device capabilities
    int input_mode_ = 0;   // AiInputMode (stored as int to avoid uldaq.h in header)
    int range_ = 0;        // Range (stored as int)
    double actual_rate_ = 0.0;
    DeviceCapabilities capabilities_;

    // Device identification
    std::string product_name_;
    std::string unique_id_;
};

} // namespace mccoutlet
