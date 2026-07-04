// Non-Apple no-op implementation of PowerAssertion.
// macOS uses PowerAssertion.mm instead (selected in src/core/CMakeLists.txt).
#include "mccoutlet/PowerAssertion.hpp"

namespace mccoutlet {

PowerAssertion::PowerAssertion(const std::string& /*reason*/) {}
PowerAssertion::~PowerAssertion() = default;

} // namespace mccoutlet
