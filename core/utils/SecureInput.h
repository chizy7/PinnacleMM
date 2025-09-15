#pragma once

#include <string>

namespace pinnacle {
namespace utils {

/**
 * @class SecureInput
 * @brief Utilities for secure input handling
 */
class SecureInput {
public:
  /**
   * @brief Read a password securely without echoing to terminal
   *
   * @param prompt The prompt to display
   * @return The password entered by user
   */
  static std::string readPassword(const std::string &prompt);

  /**
   * @brief Clear sensitive data from string memory
   *
   * @param sensitive String containing sensitive data
   */
  static void clearSensitiveString(std::string &sensitive);

private:
  /**
   * @brief Platform-specific secure password reading
   */
#ifdef _WIN32
  static std::string readPasswordWindows(const std::string &prompt);
#else
  static std::string readPasswordUnix(const std::string &prompt);
#endif
};

} // namespace utils
} // namespace pinnacle