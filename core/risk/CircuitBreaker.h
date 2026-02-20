#pragma once

#include "../utils/TimeUtils.h"
#include "RiskConfig.h"

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace pinnacle {

// Forward declare to avoid header dependency
namespace analytics {
enum class MarketRegime;
}

namespace risk {

enum class CircuitBreakerState {
  CLOSED,   // Normal trading
  OPEN,     // Trading halted
  HALF_OPEN // Testing recovery
};

enum class CircuitBreakerTrigger {
  NONE,
  RAPID_PRICE_MOVE_1MIN,
  RAPID_PRICE_MOVE_5MIN,
  SPREAD_WIDENING,
  VOLUME_SPIKE,
  MARKET_CRISIS,
  LATENCY_DEGRADATION,
  CONNECTIVITY_LOSS,
  MANUAL
};

struct CircuitBreakerStatus {
  CircuitBreakerState state{CircuitBreakerState::CLOSED};
  CircuitBreakerTrigger lastTrigger{CircuitBreakerTrigger::NONE};
  uint64_t stateChangeTime{0};
  uint64_t cooldownEndTime{0};
  double lastPriceMove1min{0.0};
  double lastPriceMove5min{0.0};
  double currentSpreadRatio{0.0};
  double currentVolumeRatio{0.0};
  size_t tripCount{0};
};

class CircuitBreaker {
public:
  static CircuitBreaker& getInstance();

  void initialize(const CircuitBreakerConfig& config);

  // Hot path - single atomic load
  bool isTradingAllowed() const;

  // State queries
  CircuitBreakerState getState() const;
  CircuitBreakerStatus getStatus() const;

  // Market data feeds
  void onPrice(double price, uint64_t timestamp);
  void onSpread(double spread, uint64_t timestamp);
  void onVolume(double volume, uint64_t timestamp);
  void onLatency(uint64_t latencyUs);

  // External triggers
  void onRegimeChange(int regime); // accepts int for MarketRegime
  void onConnectivityLoss();
  void onConnectivityRestored();

  // Manual control
  void trip(const std::string& reason);
  void reset();

  // State change callback
  using StateCallback = std::function<void(CircuitBreakerState oldState,
                                           CircuitBreakerState newState,
                                           CircuitBreakerTrigger trigger)>;
  void setStateCallback(StateCallback callback);

  // Serialization
  nlohmann::json toJson() const;

  static std::string stateToString(CircuitBreakerState state);
  static std::string triggerToString(CircuitBreakerTrigger trigger);

private:
  CircuitBreaker() = default;
  ~CircuitBreaker() = default;

  CircuitBreaker(const CircuitBreaker&) = delete;
  CircuitBreaker& operator=(const CircuitBreaker&) = delete;

  // Atomic state for lock-free isTradingAllowed()
  std::atomic<CircuitBreakerState> m_state{CircuitBreakerState::CLOSED};

  // Config
  CircuitBreakerConfig m_config;
  mutable std::mutex m_configMutex;

  // Lock-free ring buffer for price history
  static constexpr size_t MAX_PRICE_HISTORY = 512;
  struct PriceEntry {
    double price{0.0};
    uint64_t timestamp{0};
  };
  std::array<PriceEntry, MAX_PRICE_HISTORY> m_priceHistory;
  std::atomic<size_t> m_priceHead{0};
  std::atomic<size_t> m_priceCount{0};

  // Spread and volume tracking
  double m_baselineSpread{0.0};
  double m_baselineVolume{0.0};
  bool m_baselineInitialized{false};
  std::mutex m_baselineMutex;

  // State tracking
  mutable std::mutex m_statusMutex;
  CircuitBreakerTrigger m_lastTrigger{CircuitBreakerTrigger::NONE};
  uint64_t m_stateChangeTime{0};
  uint64_t m_cooldownEndTime{0};
  size_t m_tripCount{0};
  double m_lastPriceMove1min{0.0};
  double m_lastPriceMove5min{0.0};
  double m_currentSpreadRatio{0.0};
  double m_currentVolumeRatio{0.0};

  // Callback
  StateCallback m_stateCallback;
  std::mutex m_callbackMutex;

  // Internal methods
  void transitionTo(CircuitBreakerState newState,
                    CircuitBreakerTrigger trigger);
  void checkPriceMove(double price, uint64_t timestamp);
  double calculatePriceMove(uint64_t windowMs, uint64_t currentTime) const;
  void checkCooldown();
};

} // namespace risk
} // namespace pinnacle
