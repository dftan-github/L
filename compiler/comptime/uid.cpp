#include "comptime/uid.hpp"

namespace l::comptime {
namespace {

std::uint64_t g_uid_counter = 0;

}  // namespace

void resetUidCounter() { g_uid_counter = 0; }

std::uint64_t peekUidCounter() { return g_uid_counter; }

std::int64_t takeUidCounter() {
  const std::uint64_t cur = g_uid_counter;
  ++g_uid_counter;
  return static_cast<std::int64_t>(cur);
}

}  // namespace l::comptime
