#pragma once

#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace pinnacle {
namespace utils {

/**
 * @class ValidationResult
 * @brief Result of input validation
 */
struct ValidationResult {
  bool valid;
  std::string errorMessage;

  ValidationResult(bool v = true, const std::string& msg = "")
      : valid(v), errorMessage(msg) {}
};

/**
 * @class InputValidator
 * @brief Comprehensive input validation utilities
 */
class InputValidator {
public:
  // String validation
  static ValidationResult validateString(const std::string& input,
                                         size_t minLength = 0,
                                         size_t maxLength = SIZE_MAX,
                                         const std::string& allowedChars = "");

  // Numeric validation
  static ValidationResult
  validateDouble(const std::string& input,
                 double minValue = -std::numeric_limits<double>::max(),
                 double maxValue = std::numeric_limits<double>::max());

  static ValidationResult
  validateInteger(const std::string& input,
                  int minValue = std::numeric_limits<int>::min(),
                  int maxValue = std::numeric_limits<int>::max());

  // Financial validation
  static ValidationResult validateSymbol(const std::string& symbol);
  static ValidationResult validatePrice(double price);
  static ValidationResult validateQuantity(double quantity);
  static ValidationResult validateOrderId(const std::string& orderId);

  // Network validation
  static ValidationResult validateUrl(const std::string& url);
  static ValidationResult validateIPAddress(const std::string& ip);
  static ValidationResult validatePort(int port);

  // API validation
  static ValidationResult validateApiKey(const std::string& apiKey);
  static ValidationResult validateExchangeName(const std::string& exchange);

  // JSON validation
  static ValidationResult validateJson(const std::string& jsonStr);

  // File path validation
  static ValidationResult validateFilePath(const std::string& path);
  static ValidationResult validateConfigPath(const std::string& path);

  // Sanitization functions
  static std::string sanitizeString(const std::string& input);
  static std::string sanitizeFilePath(const std::string& path);
  static std::string sanitizeSymbol(const std::string& symbol);

  // Security validation
  static ValidationResult validatePassword(const std::string& password);
  static ValidationResult checkForSqlInjection(const std::string& input);
  static ValidationResult checkForXss(const std::string& input);
  static ValidationResult checkForPathTraversal(const std::string& path);

private:
  // Helper functions
  static bool containsOnlyAllowedChars(const std::string& input,
                                       const std::string& allowed);
  static bool isValidSymbolChar(char c);
  static std::unordered_set<std::string> getSupportedExchanges();

  // Security patterns
  static const std::vector<std::regex> SQL_INJECTION_PATTERNS;
  static const std::vector<std::regex> XSS_PATTERNS;
  static const std::vector<std::regex> PATH_TRAVERSAL_PATTERNS;
};

/**
 * @class ValidationException
 * @brief Exception thrown on validation failures
 */
class ValidationException : public std::invalid_argument {
public:
  explicit ValidationException(const std::string& message)
      : std::invalid_argument("Validation failed: " + message) {}
};

/**
 * @brief Macro to validate and throw on failure
 */
#define VALIDATE_OR_THROW(validation_call)                                     \
  do {                                                                         \
    auto result = validation_call;                                             \
    if (!result.valid) {                                                       \
      throw ValidationException(result.errorMessage);                          \
    }                                                                          \
  } while (0)

} // namespace utils
} // namespace pinnacle
