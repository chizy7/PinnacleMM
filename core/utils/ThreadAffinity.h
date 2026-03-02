#pragma once

#include <string>
#include <thread>

namespace pinnacle {
namespace utils {

/**
 * @class ThreadAffinity
 * @brief Platform-specific CPU pinning and thread naming utilities
 */
class ThreadAffinity {
public:
  /**
   * @brief Pin the calling thread to a specific CPU core
   * @param coreId The CPU core to pin to (0-indexed)
   * @return true if pinning succeeded
   */
  static bool pinToCore(int coreId);

  /**
   * @brief Set the name of the calling thread (for debugging/profiling)
   * @param name Thread name (will be truncated to platform limit)
   */
  static void setThreadName(const std::string& name);

  /**
   * @brief Get the number of available CPU cores
   * @return Number of hardware threads
   */
  static int getNumCores();

  /**
   * @brief Pin a given std::thread to a specific core
   * @param thread Thread to pin
   * @param coreId CPU core to pin to
   * @return true if pinning succeeded
   */
  static bool pinThreadToCore(std::thread& thread, int coreId);
};

} // namespace utils
} // namespace pinnacle
