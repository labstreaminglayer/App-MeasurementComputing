#pragma once
/**
 * @file Config.hpp
 * @brief Configuration management for MCCOutlet
 */

#include <filesystem>
#include <optional>
#include <string>

namespace mccoutlet {

struct AppConfig {
    // Stream settings
    std::string stream_name = "MCCDaq";
    std::string stream_type = "RawBrainSignal";

    // Device settings
    int device_index = 0;
    int low_channel = 0;
    int high_channel = 5;
    double sample_rate = 16384.0;
    int range = -1;  ///< uldaq Range enum value, -1 = auto-select first
    bool scaled = true;  ///< true = calibrated voltage, false = raw ADC counts
};

class ConfigManager {
public:
    static std::optional<AppConfig> load(const std::filesystem::path& path);
    static bool save(const AppConfig& config, const std::filesystem::path& path);
    static std::filesystem::path findConfigFile(
        const std::string& filename,
        const std::optional<std::filesystem::path>& hint = std::nullopt
    );
};

} // namespace mccoutlet
