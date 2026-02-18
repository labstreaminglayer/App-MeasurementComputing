#include "mccoutlet/Config.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <linux/limits.h>
#endif
#endif

namespace mccoutlet {

namespace {

std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(),
        [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(str.rbegin(), str.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

std::filesystem::path getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        return std::filesystem::path(buffer).parent_path();
    }
    return {};
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
    return {};
#endif
}

std::filesystem::path getConfigDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buffer))) {
        return std::filesystem::path(buffer);
    }
    return {};
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / "Library" / "Preferences";
    }
    return {};
#else
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::filesystem::path(xdg_config);
    }
    const char* home = getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config";
    }
    return {};
#endif
}

} // anonymous namespace

std::optional<AppConfig> ConfigManager::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    AppConfig config;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            continue;  // Section headers are informational only
        }

        auto eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(line.substr(0, eq_pos));
            std::string value = trim(line.substr(eq_pos + 1));

            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }

            if (key == "name" || key == "stream_name") {
                config.stream_name = value;
            } else if (key == "type" || key == "stream_type") {
                config.stream_type = value;
            } else if (key == "device_index" || key == "device") {
                config.device_index = std::stoi(value);
            } else if (key == "low_channel" || key == "low_chan") {
                config.low_channel = std::stoi(value);
            } else if (key == "high_channel" || key == "high_chan") {
                config.high_channel = std::stoi(value);
            } else if (key == "sample_rate" || key == "srate") {
                config.sample_rate = std::stod(value);
            } else if (key == "range" || key == "voltage_range") {
                config.range = std::stoi(value);
            } else if (key == "scaled") {
                config.scaled = (value != "0" && value != "false");
            }
        }
    }

    return config;
}

bool ConfigManager::save(const AppConfig& config, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "# MCCOutlet Configuration\n";
    file << "# Measurement Computing DAQ to Lab Streaming Layer\n\n";
    file << "[Stream]\n";
    file << "name=" << config.stream_name << "\n";
    file << "type=" << config.stream_type << "\n";
    file << "\n";
    file << "[Device]\n";
    file << "device_index=" << config.device_index << "\n";
    file << "low_channel=" << config.low_channel << "\n";
    file << "high_channel=" << config.high_channel << "\n";
    file << "sample_rate=" << config.sample_rate << "\n";
    if (config.range >= 0) {
        file << "range=" << config.range << "\n";
    }
    file << "scaled=" << (config.scaled ? "1" : "0") << "\n";

    return file.good();
}

std::filesystem::path ConfigManager::findConfigFile(
    const std::string& filename,
    const std::optional<std::filesystem::path>& hint
) {
    if (hint && std::filesystem::exists(*hint)) {
        return *hint;
    }

    std::vector<std::filesystem::path> search_paths;
    search_paths.push_back(std::filesystem::current_path());

    auto exe_path = getExecutablePath();
    if (!exe_path.empty()) {
        search_paths.push_back(exe_path);
    }

    auto config_dir = getConfigDirectory();
    if (!config_dir.empty()) {
        search_paths.push_back(config_dir);
    }

    for (const auto& dir : search_paths) {
        auto full_path = dir / filename;
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }
    }

    return {};
}

} // namespace mccoutlet
