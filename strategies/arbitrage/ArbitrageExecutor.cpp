#include "ArbitrageExecutor.h"
#include "../../core/utils/TimeUtils.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace arbitrage {

ArbitrageExecutor::ArbitrageExecutor(bool dryRun) : m_dryRun(dryRun) {}

ExecutionResult
ArbitrageExecutor::execute(const ArbitrageOpportunity& opportunity) {
  ExecutionResult result;
  result.opportunity = opportunity;

  uint64_t startTime = utils::TimeUtils::getCurrentNanos();

  m_totalExecutions.fetch_add(1, std::memory_order_relaxed);

  if (m_dryRun) {
    // Simulate execution
    result.buyFilled = true;
    result.sellFilled = true;
    result.buyFillPrice = opportunity.buyPrice;
    result.sellFillPrice = opportunity.sellPrice;
    result.fillQuantity = opportunity.maxQuantity;
    result.realizedProfit = opportunity.estimatedProfit;
    result.slippage = 0.0;

    spdlog::info("[DRY-RUN] Arbitrage: BUY {} {} @{} from {} | SELL @{} on {} "
                 "| profit=${}",
                 opportunity.maxQuantity, opportunity.symbol,
                 opportunity.buyPrice, opportunity.buyVenue,
                 opportunity.sellPrice, opportunity.sellVenue,
                 opportunity.estimatedProfit);

    m_successfulExecutions.fetch_add(1, std::memory_order_relaxed);

    // Update total profit via CAS
    double prev = m_totalProfit.load(std::memory_order_relaxed);
    while (!m_totalProfit.compare_exchange_weak(
        prev, prev + result.realizedProfit, std::memory_order_release,
        std::memory_order_relaxed)) {
    }

  } else {
    // Real execution via callbacks
    OrderSubmitCallback cb;
    {
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      cb = m_submitCallback;
    }

    if (!cb) {
      result.error = "No order submit callback registered";
      m_failedExecutions.fetch_add(1, std::memory_order_relaxed);
      return result;
    }

    // Submit buy order
    result.buyFilled =
        cb(opportunity.buyVenue, opportunity.symbol, OrderSide::BUY,
           opportunity.buyPrice, opportunity.maxQuantity);

    // Submit sell order
    result.sellFilled =
        cb(opportunity.sellVenue, opportunity.symbol, OrderSide::SELL,
           opportunity.sellPrice, opportunity.maxQuantity);

    if (result.buyFilled && result.sellFilled) {
      result.buyFillPrice = opportunity.buyPrice;
      result.sellFillPrice = opportunity.sellPrice;
      result.fillQuantity = opportunity.maxQuantity;
      result.realizedProfit = opportunity.estimatedProfit;
      m_successfulExecutions.fetch_add(1, std::memory_order_relaxed);

      double prev = m_totalProfit.load(std::memory_order_relaxed);
      while (!m_totalProfit.compare_exchange_weak(
          prev, prev + result.realizedProfit, std::memory_order_release,
          std::memory_order_relaxed)) {
      }
    } else {
      result.error = "Partial fill — ";
      if (!result.buyFilled) {
        result.error += "buy failed ";
      }
      if (!result.sellFilled) {
        result.error += "sell failed";
      }
      m_failedExecutions.fetch_add(1, std::memory_order_relaxed);
    }
  }

  result.executionTimeNs = utils::TimeUtils::getCurrentNanos() - startTime;

  // Store recent result
  {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_recentResults.push_back(result);
    if (m_recentResults.size() > 100) {
      m_recentResults.erase(m_recentResults.begin());
    }
  }

  return result;
}

void ArbitrageExecutor::setOrderSubmitCallback(OrderSubmitCallback callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_submitCallback = std::move(callback);
}

std::string ArbitrageExecutor::getStatistics() const {
  std::ostringstream oss;
  oss << "ArbitrageExecutor Statistics:\n";
  oss << "  Dry run: " << (m_dryRun ? "yes" : "no") << "\n";
  oss << "  Total executions: "
      << m_totalExecutions.load(std::memory_order_relaxed) << "\n";
  oss << "  Successful: "
      << m_successfulExecutions.load(std::memory_order_relaxed) << "\n";
  oss << "  Failed: " << m_failedExecutions.load(std::memory_order_relaxed)
      << "\n";
  oss << "  Total profit: $" << m_totalProfit.load(std::memory_order_relaxed)
      << "\n";
  return oss.str();
}

uint64_t ArbitrageExecutor::getTotalExecutions() const {
  return m_totalExecutions.load(std::memory_order_relaxed);
}

double ArbitrageExecutor::getTotalProfit() const {
  return m_totalProfit.load(std::memory_order_relaxed);
}

} // namespace arbitrage
} // namespace pinnacle
