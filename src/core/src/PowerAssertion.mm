// macOS implementation of PowerAssertion. Compiled with ARC (-fobjc-arc);
// see src/core/CMakeLists.txt.
#include "mccoutlet/PowerAssertion.hpp"

#import <Foundation/Foundation.h>

namespace mccoutlet {

PowerAssertion::PowerAssertion(const std::string& reason) {
    // NSActivityUserInitiated -> disables App Nap and idle system sleep.
    // NSActivityLatencyCritical -> also disables timer throttling so the
    // libusb event thread keeps servicing high-rate transfers promptly.
    NSActivityOptions options =
        NSActivityUserInitiated | NSActivityLatencyCritical;

    NSString* why = [NSString stringWithUTF8String:reason.c_str()];
    if (why == nil) why = @"DAQ acquisition";

    id token = [[NSProcessInfo processInfo] beginActivityWithOptions:options
                                                              reason:why];
    // Hand the token to the C++ object; balanced by __bridge_transfer below.
    token_ = (__bridge_retained void*)token;
}

PowerAssertion::~PowerAssertion() {
    if (token_) {
        id token = (__bridge_transfer id)token_;
        [[NSProcessInfo processInfo] endActivity:token];
        token_ = nullptr;
    }
}

} // namespace mccoutlet
