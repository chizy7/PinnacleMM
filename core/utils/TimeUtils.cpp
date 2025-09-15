#include "TimeUtils.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace pinnacle {
namespace utils {

std::string TimeUtils::nanosToTimestamp(uint64_t nanos) {
  using namespace std::chrono;

  // Convert nanoseconds to time_point
  time_t seconds = nanos / 1000000000;
  auto timePoint = system_clock::from_time_t(seconds);

  // Convert to time_t for date/time formatting
  auto timeT = system_clock::to_time_t(timePoint);

  // Get remaining nanoseconds
  auto remainingNanos = nanos % 1000000000;

  // Format the time with nanosecond precision
  std::stringstream ss;
  ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(9) << remainingNanos;

  return ss.str();
}

std::string TimeUtils::getCurrentISOTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto timeT = std::chrono::system_clock::to_time_t(now);

  // Get milliseconds
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';

  return ss.str();
}

bool TimeUtils::isNanosecondPrecisionAvailable() {
  static std::atomic<int> cachedResult =
      -1; // -1 = not computed, 0 = false, 1 = true

  // Return cached result if available
  int cached = cachedResult.load(std::memory_order_relaxed);
  if (cached >= 0) {
    return cached == 1;
  }
  // Check if the current platform supports nanosecond precision
  // by measuring the resolution of the steady_clock

  using clock = std::chrono::steady_clock;

  // Measure the minimum time difference that can be detected
  constexpr int samplesCount = 100;
  uint64_t minDiff = UINT64_MAX;

  for (int i = 0; i < samplesCount; ++i) {
    auto start = clock::now();
    auto end = clock::now();

    auto diff = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count());

    // If the difference is 0, the clock doesn't have enough precision
    if (diff > 0 && diff < minDiff) {
      minDiff = diff;
    }
  }

  // If the minimum difference is less than 1000 nanoseconds (1 microsecond),
  // we consider the platform to support nanosecond precision
  bool result = minDiff < 1000;
  cachedResult.store(result ? 1 : 0, std::memory_order_relaxed);
  return result;
}

} // namespace utils
} // namespace pinnacle
