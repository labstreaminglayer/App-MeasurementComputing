/**
 * @file main.cpp
 * @brief CLI entry point for MCCOutlet
 *
 * Headless version for servers, embedded systems, or automated use.
 */

#include <mccoutlet/Config.hpp>
#include <mccoutlet/Device.hpp>
#include <mccoutlet/StreamThread.hpp>

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {

std::atomic<bool> g_shutdown{false};

void signalHandler(int /*signum*/) {
    std::cout << "\nShutdown requested..." << std::endl;
    g_shutdown = true;
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help              Show this help message\n"
              << "  -l, --list-devices      List connected MCC devices and exit\n"
              << "  --list-ranges           Show device capabilities (ranges, resolution, rate limits)\n"
              << "  -c, --config FILE       Load configuration from FILE\n"
              << "  -n, --name NAME         Stream name (default: MCCDaq)\n"
              << "  -t, --type TYPE         Stream type (default: RawBrainSignal)\n"
              << "  -d, --device INDEX      Device index (default: 0)\n"
              << "  --device-name NAME      Select device by product name (substring match)\n"
              << "  --low-chan N             Low channel (default: 0)\n"
              << "  --high-chan N            High channel (default: 7)\n"
              << "  -r, --rate RATE         Sample rate in Hz (default: 44100)\n"
              << "  --range VALUE           Voltage range (uldaq Range enum value, default: auto)\n"
              << "  --raw                   Output raw integer ADC integers instead of scaled voltage\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " --list-devices\n"
              << "  " << program_name << " --list-ranges -d 0\n"
              << "  " << program_name << " --device-name USB-1608FS --rate 44100\n"
              << "  " << program_name << " -d 0 --low-chan 0 --high-chan 7 --range 6\n"
              << std::endl;
}

void statusCallback(const std::string& message, bool is_error) {
    if (is_error) {
        std::cerr << "[ERROR] " << message << std::endl;
    } else {
        std::cout << "[INFO] " << message << std::endl;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    mccoutlet::AppConfig config;
    std::string config_file;
    std::string device_name;
    bool list_devices = false;
    bool list_ranges = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-l" || arg == "--list-devices") {
            list_devices = true;
        } else if (arg == "--list-ranges") {
            list_ranges = true;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        } else if ((arg == "-n" || arg == "--name") && i + 1 < argc) {
            config.stream_name = argv[++i];
        } else if ((arg == "-t" || arg == "--type") && i + 1 < argc) {
            config.stream_type = argv[++i];
        } else if ((arg == "-d" || arg == "--device") && i + 1 < argc) {
            config.device_index = std::stoi(argv[++i]);
        } else if (arg == "--device-name" && i + 1 < argc) {
            device_name = argv[++i];
        } else if (arg == "--low-chan" && i + 1 < argc) {
            config.low_channel = std::stoi(argv[++i]);
        } else if (arg == "--high-chan" && i + 1 < argc) {
            config.high_channel = std::stoi(argv[++i]);
        } else if ((arg == "-r" || arg == "--rate") && i + 1 < argc) {
            config.sample_rate = std::stod(argv[++i]);
        } else if (arg == "--range" && i + 1 < argc) {
            config.range = std::stoi(argv[++i]);
        } else if (arg == "--raw") {
            config.scaled = false;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Handle --list-devices
    if (list_devices) {
        auto devices = mccoutlet::MCCDevice::discover();
        if (devices.empty()) {
            std::cout << "No MCC DAQ devices found." << std::endl;
        } else {
            std::cout << "Found " << devices.size() << " device(s):" << std::endl;
            for (const auto& dev : devices) {
                std::cout << "  [" << dev.index << "] "
                          << dev.product_name << " (" << dev.unique_id << ") "
                          << "[" << dev.interface_name << "]" << std::endl;
            }
        }
        return 0;
    }

    if (!config_file.empty()) {
        auto loaded = mccoutlet::ConfigManager::load(config_file);
        if (loaded) {
            config = *loaded;
            std::cout << "Loaded configuration from: " << config_file << std::endl;
        } else {
            std::cerr << "Failed to load config file: " << config_file << std::endl;
            return 1;
        }
    }

    // Resolve --device-name to a device index
    if (!device_name.empty()) {
        auto devices = mccoutlet::MCCDevice::discover();
        bool found = false;
        for (const auto& dev : devices) {
            if (dev.product_name.find(device_name) != std::string::npos) {
                config.device_index = dev.index;
                std::cout << "Matched device: " << dev.product_name
                          << " (" << dev.unique_id << ") at index " << dev.index << std::endl;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "No device matching '" << device_name << "' found." << std::endl;
            std::cerr << "Use --list-devices to see available devices." << std::endl;
            return 1;
        }
    }

    // Handle --list-ranges
    if (list_ranges) {
        try {
            mccoutlet::MCCDevice::Config device_cfg;
            device_cfg.device_index = config.device_index;

            mccoutlet::MCCDevice device(device_cfg);
            device.connect();

            auto caps = device.getCapabilities();

            std::cout << "Device capabilities (index " << config.device_index << "):" << std::endl;
            std::cout << "  Input mode:   " << caps.input_mode_name << std::endl;
            std::cout << "  Max channels: " << caps.max_channels << std::endl;
            std::cout << "  Resolution:   " << caps.resolution_bits << " bits" << std::endl;
            std::cout << "  Scan rate:    " << caps.min_scan_rate
                      << " - " << caps.max_scan_rate << " Hz" << std::endl;
            std::cout << "  Voltage ranges:" << std::endl;
            for (const auto& r : caps.available_ranges) {
                std::cout << "    [" << r.id << "] " << r.label << std::endl;
            }

            device.disconnect();
        } catch (const std::exception& e) {
            std::cerr << "Error querying device: " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int channelCount = config.high_channel - config.low_channel + 1;
    std::cout << "MCCOutlet CLI" << std::endl;
    std::cout << "Stream: " << config.stream_name << " (" << config.stream_type << ")" << std::endl;
    std::cout << "Device index: " << config.device_index << std::endl;
    std::cout << "Channels: " << config.low_channel << "-" << config.high_channel
              << " (" << channelCount << " ch) @ " << config.sample_rate << " Hz" << std::endl;
    std::cout << "Data format: " << (config.scaled ? "Scaled (Voltage)" : "Raw (Integer Counts)") << std::endl;

    mccoutlet::MCCDevice::Config device_config{
        .stream_name = config.stream_name,
        .stream_type = config.stream_type,
        .device_index = config.device_index,
        .low_channel = config.low_channel,
        .high_channel = config.high_channel,
        .sample_rate = config.sample_rate,
        .range = config.range,
        .scaled = config.scaled
    };
    auto device = std::make_unique<mccoutlet::MCCDevice>(device_config, statusCallback);

    mccoutlet::StreamThread stream(std::move(device), statusCallback);

    if (!stream.start()) {
        std::cerr << "Failed to start streaming" << std::endl;
        return 1;
    }

    // Display resolution info after successful connection
    auto info = stream.getDeviceInfo();
    if (info.resolution_bits > 0) {
        std::cout << "ADC Resolution: " << info.resolution_bits << " bits" << std::endl;
    }

    std::cout << "Press Ctrl+C to stop..." << std::endl;

    while (!g_shutdown && stream.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stream.stop();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
