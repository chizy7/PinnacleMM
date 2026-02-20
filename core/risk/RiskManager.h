#pragma once

#include "../orderbook/Order.h"
#include "../utils/TimeUtils.h"
#include "RiskConfig.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace pinnacle {
namespace risk {

/**
 * @enum RiskCheckResult
 * @brief Outcome of a pre-trade risk check
 */
enum class RiskCheckResult {
  APPROVED,
  REJECTED_POSITION_LIMIT,
  REJECTED_EXPOSURE_LIMIT,
  REJECTED_DRAWDOWN_LIMIT,
  REJECTED_DAILY_LOSS_LIMIT,
  REJECTED_ORDER_SIZE_LIMIT,
  REJECTED_RATE_LIMIT,
  REJECTED_CIRCUIT_BREAKER,
  REJECTED_VOLUME_LIMIT,
  REJECTED_HALTED
};

/**
 * @struct RiskState
 * @brief Snapshot of the current risk manager state
 */
struct RiskState {
  double currentPosition{0.0};
  double totalPnL{0.0};
  double peakPnL{0.0};
  double dailyPnL{0.0};
  double dailyVolume{0.0};
  double currentDrawdown{0.0};
  double netExposure{0.0};
  double grossExposure{0.0};
  bool isHalted{false};
  std::string haltReason;
  uint64_t lastUpdateTime{0};
  uint64_t dailyResetTime{0};
  uint32_t ordersThisSecond{0};
  uint64_t currentSecond{0};
};

/**
 * @class RiskManager
 * @brief Singleton risk manager providing pre-trade checks and position
 * tracking
 *
 * The hot path (checkOrder) is fully lock-free, relying only on atomic loads.
 * State mutations (onFill, onPnLUpdate) use atomic stores and acquire the mutex
 * only when complex multi-field consistency is required.
 */
class RiskManager {
public:
  /**
   * @brief Get the singleton instance
   * @return Reference to the RiskManager singleton
   */
  static RiskManager& getInstance();

  /**
   * @brief Initialize with risk limits and optionally start the hedge thread
   * @param limits Risk limits to enforce
   */
  void initialize(const RiskLimits& limits);

  /**
   * @brief Pre-trade risk check (lock-free hot path)
   *
   * Checks are evaluated in order: halted, rate limit, order size, position
   * limit, daily volume, daily loss, drawdown, exposure.
   *
   * @param side Order side (BUY or SELL)
   * @param price Order price
   * @param quantity Order quantity
   * @param symbol Trading symbol
   * @return RiskCheckResult indicating approval or rejection reason
   */
  RiskCheckResult checkOrder(OrderSide side, double price, double quantity,
                             const std::string& symbol);

  /**
   * @brief Post-trade state update after a fill
   * @param side Fill side
   * @param price Fill price
   * @param quantity Fill quantity
   * @param symbol Trading symbol
   */
  void onFill(OrderSide side, double price, double quantity,
              const std::string& symbol);

  /**
   * @brief Update PnL tracking and evaluate drawdown / daily loss breaches
   * @param newPnL New total PnL value
   */
  void onPnLUpdate(double newPnL);

  /**
   * @brief Check if position exceeds hedge threshold
   * @return true if a hedge is needed
   */
  bool needsHedge() const;

  /**
   * @brief Evaluate and execute a hedge if needed
   */
  void evaluateHedge();

  /**
   * @brief Halt all trading activity
   * @param reason Human-readable halt reason
   */
  void halt(const std::string& reason);

  /**
   * @brief Resume trading activity after a halt
   */
  void resume();

  /**
   * @brief Check if trading is halted
   * @return true if halted
   */
  bool isHalted() const;

  // Getters
  RiskState getState() const;
  RiskLimits getLimits() const;
  double getPosition() const;
  double getDailyPnL() const;
  double getDrawdown() const;
  double getPositionUtilization() const;
  double getDailyLossUtilization() const;

  /**
   * @brief Update risk limits at runtime
   * @param limits New risk limits
   */
  void updateLimits(const RiskLimits& limits);

  /**
   * @brief Set callback invoked when a hedge is needed
   * @param callback Function receiving side and quantity for the hedge order
   */
  using HedgeCallback = std::function<void(OrderSide side, double quantity)>;
  void setHedgeCallback(HedgeCallback callback);

  /**
   * @brief Serialize current state to JSON
   * @return JSON representation
   */
  nlohmann::json toJson() const;

  /**
   * @brief Restore state from JSON
   * @param j JSON object
   */
  void fromJson(const nlohmann::json& j);

  /**
   * @brief Reset daily counters (PnL, volume, rate limit)
   */
  void resetDaily();

  /**
   * @brief Convert a RiskCheckResult to a human-readable string
   * @param result The result to convert
   * @return String representation
   */
  static std::string resultToString(RiskCheckResult result);

private:
  RiskManager() = default;
  ~RiskManager();

  RiskManager(const RiskManager&) = delete;
  RiskManager& operator=(const RiskManager&) = delete;

  // Atomic state for lock-free reads on the hot path
  std::atomic<double> m_position{0.0};
  std::atomic<double> m_totalPnL{0.0};
  std::atomic<double> m_peakPnL{0.0};
  std::atomic<double> m_dailyPnL{0.0};
  std::atomic<double> m_dailyVolume{0.0};
  std::atomic<double> m_netExposure{0.0};
  std::atomic<double> m_grossExposure{0.0};
  std::atomic<bool> m_halted{false};
  std::atomic<uint32_t> m_ordersThisSecond{0};
  std::atomic<uint64_t> m_currentSecond{0};

  // Protected state requiring mutex
  mutable std::mutex m_stateMutex;
  RiskLimits m_limits;
  std::string m_haltReason;
  uint64_t m_dailyResetTime{0};

  // Hedge state
  std::mutex m_hedgeMutex;
  HedgeCallback m_hedgeCallback;
  std::thread m_hedgeThread;
  std::atomic<bool> m_hedgeRunning{false};

  /**
   * @brief Background loop that periodically evaluates hedging
   */
  void hedgeLoop();

  /**
   * @brief Check if midnight has passed and reset daily counters if so
   */
  void checkDailyReset();
};

} // namespace risk
} // namespace pinnacle
