#pragma once

#include "ArbitrageDetector.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pinnacle {
namespace arbitrage {

/**
 * @struct ExecutionResult
 * @brief Result of an arbitrage execution attempt
 */
struct ExecutionResult {
  ArbitrageOpportunity opportunity;
  bool buyFilled{false};
  bool sellFilled{false};
  double buyFillPrice{0.0};
  double sellFillPrice{0.0};
  double fillQuantity{0.0};
  double realizedProfit{0.0};
  double slippage{0.0};
  uint64_t executionTimeNs{0};
  std::string error;
};

/**
 * @class ArbitrageExecutor
 * @brief Executes arbitrage opportunities by submitting simultaneous buy/sell
 * orders
 */
class ArbitrageExecutor {
public:
  using OrderSubmitCallback = std::function<bool(
      const std::string& venue, const std::string& symbol,
      pinnacle::OrderSide side, double price, double quantity)>;

  explicit ArbitrageExecutor(bool dryRun = true);
  ~ArbitrageExecutor() = default;

  ArbitrageExecutor(const ArbitrageExecutor&) = delete;
  ArbitrageExecutor& operator=(const ArbitrageExecutor&) = delete;

  /**
   * @brief Execute an arbitrage opportunity
   * @param opportunity The detected opportunity
   * @return Execution result
   */
  ExecutionResult execute(const ArbitrageOpportunity& opportunity);

  /**
   * @brief Set the order submission callback (used to route to OrderRouter)
   */
  void setOrderSubmitCallback(OrderSubmitCallback callback);

  /**
   * @brief Get execution statistics
   */
  std::string getStatistics() const;

  /**
   * @brief Get total number of executions
   */
  uint64_t getTotalExecutions() const;

  /**
   * @brief Get total realized profit
   */
  double getTotalProfit() const;

private:
  bool m_dryRun;
  OrderSubmitCallback m_submitCallback;
  std::mutex m_callbackMutex;

  // Statistics
  std::atomic<uint64_t> m_totalExecutions{0};
  std::atomic<uint64_t> m_successfulExecutions{0};
  std::atomic<uint64_t> m_failedExecutions{0};
  std::atomic<double> m_totalProfit{0.0};

  mutable std::mutex m_resultsMutex;
  std::vector<ExecutionResult> m_recentResults;
};

} // namespace arbitrage
} // namespace pinnacle
