#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace pinnacle {
namespace utils {

/**
 * @class Factor
 * @brief Ensures value is constrained to the [0.0, 1.0] range
 *
 * A utility class for representing factor values that must lie between 0 and 1.
 * Provides automatic clamping and type safety.
 */
class Factor {
private:
  double value;

public:
  /**
   * @brief Construct a new Factor from a double value
   * @param v Value to be clamped to [0.0, 1.0] range
   */
  explicit Factor(double v) : value(std::clamp(v, 0.0, 1.0)) {}

  /**
   * @brief Implicit conversion to double
   * @return The clamped value
   */
  operator double() const { return value; }

  /**
   * @brief Get the value directly
   * @return The clamped value
   */
  double getValue() const { return value; }

  /**
   * @brief Check if the value is at the minimum of the range
   * @return true if value is 0.0
   */
  bool isMin() const { return std::abs(value) < 1e-10; }

  /**
   * @brief Check if the value is at the maximum of the range
   * @return true if value is 1.0
   */
  bool isMax() const { return std::abs(value - 1.0) < 1e-10; }

  /**
   * @brief Convert the factor to a percentage string
   * @return String representation as percentage
   */
  std::string toPercentString() const {
    return std::to_string(static_cast<int>(value * 100)) + "%";
  }
};

/**
 * @class Percentage
 * @brief Ensures value is constrained to the [0.0, 100.0] range
 *
 * A utility class for representing percentage values.
 * Provides automatic clamping and type safety.
 */
class Percentage {
private:
  double value;

public:
  /**
   * @brief Construct a new Percentage from a double value
   * @param v Value to be clamped to [0.0, 100.0] range
   */
  explicit Percentage(double v) : value(std::clamp(v, 0.0, 100.0)) {}

  /**
   * @brief Implicit conversion to double
   * @return The clamped value
   */
  operator double() const { return value; }

  /**
   * @brief Get the value directly
   * @return The clamped value
   */
  double getValue() const { return value; }

  /**
   * @brief Convert to Factor (divides by 100)
   * @return Factor object with equivalent value
   */
  Factor toFactor() const { return Factor(value / 100.0); }

  /**
   * @brief Convert the percentage to a string with % sign
   * @return String representation with % sign
   */
  std::string toString() const { return std::to_string(value) + "%"; }
};

} // namespace utils
} // namespace pinnacle
