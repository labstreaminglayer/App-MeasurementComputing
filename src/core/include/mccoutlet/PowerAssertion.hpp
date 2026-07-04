#pragma once
/**
 * @file PowerAssertion.hpp
 * @brief RAII guard that keeps the host awake during acquisition.
 */

#include <string>

namespace mccoutlet {

/**
 * @brief Keeps the system awake for as long as the object is alive.
 *
 * On macOS, idle system sleep suspends the USB bus, which wedges the DAQ's
 * in-flight transfers (uldaq then logs "##### error still xfer pending" and
 * acquisition cannot be recovered). App Nap can likewise throttle the libusb
 * event thread enough to overrun the device's hardware FIFO. This guard opts
 * the process out of both via @c -[NSProcessInfo beginActivityWithOptions:reason:]
 * for the duration of streaming, releasing the activity on destruction.
 *
 * On non-Apple platforms it is a no-op (idle sleep does not suspend USB the
 * same way; add a platform implementation here if that changes).
 */
class PowerAssertion {
public:
    /// @param reason Human-readable reason shown in power diagnostics.
    explicit PowerAssertion(const std::string& reason);
    ~PowerAssertion();

    PowerAssertion(const PowerAssertion&) = delete;
    PowerAssertion& operator=(const PowerAssertion&) = delete;

private:
    void* token_ = nullptr;  ///< retained activity token (id<NSObject> on macOS)
};

} // namespace mccoutlet
