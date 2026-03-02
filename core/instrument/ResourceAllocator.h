#pragma once

#include "../utils/ThreadAffinity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace instrument {

/**
 * @struct CoreAssignment
 * @brief CPU core assignment for an instrument
 */
struct CoreAssignment {
  std::string symbol;
  int strategyCore{-1};  // Core for the strategy thread
  int simulatorCore{-1}; // Core for the simulator thread
  int priority{0};       // Thread priority hint (0 = normal)
};

/**
 * @class ResourceAllocator
 * @brief Assigns CPU cores and priorities to instruments based on count and
 * available hardware
 *
 * Used by InstrumentManager on startup to distribute instruments across
 * available cores for optimal performance.
 */
class ResourceAllocator {
public:
  ResourceAllocator() = default;

  /**
   * @brief Allocate cores for a set of instruments
   * @param symbols List of instrument symbols
   * @return Map of symbol -> CoreAssignment
   */
  std::unordered_map<std::string, CoreAssignment>
  allocate(const std::vector<std::string>& symbols) const;

  /**
   * @brief Get available core count
   * @return Number of hardware threads available
   */
  int getAvailableCores() const;

  /**
   * @brief Apply a core assignment to the calling thread
   * @param assignment The assignment to apply
   * @param isStrategy true for strategy thread, false for simulator
   * @return true if affinity was set successfully
   */
  static bool applyAssignment(const CoreAssignment& assignment,
                              bool isStrategy);
};

} // namespace instrument
} // namespace pinnacle
