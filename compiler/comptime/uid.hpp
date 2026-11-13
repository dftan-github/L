#pragma once

#include <cstdint>

namespace l::comptime {

// Rule 93–94: process-wide monotonic counter for compile-time uid_counter().
// Reset at the start of each LCMD expansion / compilation unit.
void resetUidCounter();
std::uint64_t peekUidCounter();
// Returns current value, then increments. Never lowered to LLVM.
std::int64_t takeUidCounter();

}  // namespace l::comptime
