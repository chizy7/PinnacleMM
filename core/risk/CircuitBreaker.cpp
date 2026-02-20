#include "CircuitBreaker.h"
#include "../utils/AuditLogger.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace risk {

using pinnacle::utils::AuditLogger;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

CircuitBreaker& CircuitBreaker::getInstance() {
  static CircuitBreaker instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void CircuitBreaker::initialize(const CircuitBreakerConfig& config) {
  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_config = config;
  }

  // Reset state
  m_state.store(CircuitBreakerState::CLOSED, std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_lastTrigger = CircuitBreakerTrigger::NONE;
    m_stateChangeTime = utils::TimeUtils::getCurrentMillis();
    m_cooldownEndTime = 0;
    m_tripCount = 0;
    m_lastPriceMove1min = 0.0;
    m_lastPriceMove5min = 0.0;
    m_currentSpreadRatio = 0.0;
    m_currentVolumeRatio = 0.0;
  }

  // Reset price ring buffer
  m_priceHead.store(0, std::memory_order_release);
  m_priceCount.store(0, std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(m_baselineMutex);
    m_baselineSpread = 0.0;
    m_baselineVolume = 0.0;
    m_baselineInitialized = false;
  }

  spdlog::info("[CircuitBreaker] Initialized - priceMove1min={:.2f}% "
               "priceMove5min={:.2f}% spreadWiden={:.1f}x volumeSpike={:.1f}x "
               "cooldown={}ms maxLatency={}us",
               config.priceMove1minPct, config.priceMove5minPct,
               config.spreadWidenMultiplier, config.volumeSpikeMultiplier,
               config.cooldownPeriodMs, config.maxLatencyUs);

  AUDIT_SYSTEM_EVENT("CircuitBreaker initialized", true);
}

// ---------------------------------------------------------------------------
// Hot path
// ---------------------------------------------------------------------------

bool CircuitBreaker::isTradingAllowed() const {
  return m_state.load(std::memory_order_acquire) == CircuitBreakerState::CLOSED;
}

CircuitBreakerState CircuitBreaker::getState() const {
  return m_state.load(std::memory_order_acquire);
}

CircuitBreakerStatus CircuitBreaker::getStatus() const {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  CircuitBreakerStatus status;
  status.state = m_state.load(std::memory_order_acquire);
  status.lastTrigger = m_lastTrigger;
  status.stateChangeTime = m_stateChangeTime;
  status.cooldownEndTime = m_cooldownEndTime;
  status.lastPriceMove1min = m_lastPriceMove1min;
  status.lastPriceMove5min = m_lastPriceMove5min;
  status.currentSpreadRatio = m_currentSpreadRatio;
  status.currentVolumeRatio = m_currentVolumeRatio;
  status.tripCount = m_tripCount;
  return status;
}

// ---------------------------------------------------------------------------
// Market data feeds
// ---------------------------------------------------------------------------

void CircuitBreaker::onPrice(double price, uint64_t timestamp) {
  // Store in ring buffer (lock-free for single-producer)
  size_t head = m_priceHead.load(std::memory_order_relaxed);
  size_t idx = head % MAX_PRICE_HISTORY;
  m_priceHistory[idx].price = price;
  m_priceHistory[idx].timestamp = timestamp;
  m_priceHead.store(head + 1, std::memory_order_release);

  size_t count = m_priceCount.load(std::memory_order_relaxed);
  if (count < MAX_PRICE_HISTORY) {
    m_priceCount.store(count + 1, std::memory_order_release);
  }

  // Check price moves and cooldown
  checkPriceMove(price, timestamp);
  checkCooldown();
}

void CircuitBreaker::onSpread(double spread, uint64_t timestamp) {
  static_cast<void>(timestamp);

  std::lock_guard<std::mutex> lock(m_baselineMutex);

  // Initialize baseline using exponential moving average of first samples
  if (!m_baselineInitialized) {
    if (m_baselineSpread == 0.0) {
      m_baselineSpread = spread;
    } else {
      // EMA with alpha = 0.1 for the first ~20 samples
      constexpr double alpha = 0.1;
      m_baselineSpread = alpha * spread + (1.0 - alpha) * m_baselineSpread;

      // Consider baseline initialized after enough smoothing
      static size_t spreadSamples = 0;
      ++spreadSamples;
      if (spreadSamples >= 20) {
        m_baselineInitialized = true;
        spdlog::info("[CircuitBreaker] Spread baseline initialized at {:.6f}",
                     m_baselineSpread);
      }
    }
    return;
  }

  // Guard against division by zero
  if (m_baselineSpread <= 0.0) {
    return;
  }

  double ratio = spread / m_baselineSpread;

  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_currentSpreadRatio = ratio;
  }

  double threshold;
  {
    std::lock_guard<std::mutex> cfgLock(m_configMutex);
    threshold = m_config.spreadWidenMultiplier;
  }

  if (ratio >= threshold) {
    auto currentState = m_state.load(std::memory_order_acquire);
    if (currentState == CircuitBreakerState::CLOSED) {
      spdlog::warn("[CircuitBreaker] Spread widening detected: ratio={:.2f}x "
                   "(threshold={:.1f}x)",
                   ratio, threshold);
      transitionTo(CircuitBreakerState::OPEN,
                   CircuitBreakerTrigger::SPREAD_WIDENING);
    }
  }

  // Slowly adapt baseline (very low alpha to track long-term drift)
  constexpr double adaptAlpha = 0.001;
  m_baselineSpread =
      adaptAlpha * spread + (1.0 - adaptAlpha) * m_baselineSpread;
}

void CircuitBreaker::onVolume(double volume, uint64_t timestamp) {
  static_cast<void>(timestamp);

  std::lock_guard<std::mutex> lock(m_baselineMutex);

  // Initialize volume baseline
  if (m_baselineVolume == 0.0) {
    m_baselineVolume = volume;
    return;
  }

  // Update baseline with slow EMA
  constexpr double adaptAlpha = 0.005;
  m_baselineVolume =
      adaptAlpha * volume + (1.0 - adaptAlpha) * m_baselineVolume;

  // Guard against division by zero
  if (m_baselineVolume <= 0.0) {
    return;
  }

  double ratio = volume / m_baselineVolume;

  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_currentVolumeRatio = ratio;
  }

  double threshold;
  {
    std::lock_guard<std::mutex> cfgLock(m_configMutex);
    threshold = m_config.volumeSpikeMultiplier;
  }

  if (ratio >= threshold) {
    auto currentState = m_state.load(std::memory_order_acquire);
    if (currentState == CircuitBreakerState::CLOSED) {
      spdlog::warn("[CircuitBreaker] Volume spike detected: ratio={:.2f}x "
                   "(threshold={:.1f}x)",
                   ratio, threshold);
      transitionTo(CircuitBreakerState::OPEN,
                   CircuitBreakerTrigger::VOLUME_SPIKE);
    }
  }
}

void CircuitBreaker::onLatency(uint64_t latencyUs) {
  uint64_t threshold;
  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    threshold = m_config.maxLatencyUs;
  }

  if (latencyUs > threshold) {
    auto currentState = m_state.load(std::memory_order_acquire);
    if (currentState == CircuitBreakerState::CLOSED) {
      spdlog::warn("[CircuitBreaker] Latency degradation: {}us > {}us limit",
                   latencyUs, threshold);
      transitionTo(CircuitBreakerState::OPEN,
                   CircuitBreakerTrigger::LATENCY_DEGRADATION);
    }
  }
}

// ---------------------------------------------------------------------------
// External triggers
// ---------------------------------------------------------------------------

void CircuitBreaker::onRegimeChange(int regime) {
  // MarketRegime::CRISIS = 5 (from analytics::MarketRegime enum)
  constexpr int CRISIS_VALUE = 5;

  if (regime == CRISIS_VALUE) {
    auto currentState = m_state.load(std::memory_order_acquire);
    if (currentState == CircuitBreakerState::CLOSED) {
      spdlog::warn("[CircuitBreaker] Market crisis regime detected");
      transitionTo(CircuitBreakerState::OPEN,
                   CircuitBreakerTrigger::MARKET_CRISIS);
    }
  }
}

void CircuitBreaker::onConnectivityLoss() {
  auto currentState = m_state.load(std::memory_order_acquire);
  if (currentState == CircuitBreakerState::CLOSED) {
    spdlog::error("[CircuitBreaker] Connectivity loss detected");
    transitionTo(CircuitBreakerState::OPEN,
                 CircuitBreakerTrigger::CONNECTIVITY_LOSS);
  }
}

void CircuitBreaker::onConnectivityRestored() {
  auto currentState = m_state.load(std::memory_order_acquire);
  if (currentState == CircuitBreakerState::OPEN) {
    // Only auto-recover if the trip was caused by connectivity loss
    CircuitBreakerTrigger trigger;
    {
      std::lock_guard<std::mutex> lock(m_statusMutex);
      trigger = m_lastTrigger;
    }

    if (trigger == CircuitBreakerTrigger::CONNECTIVITY_LOSS) {
      spdlog::info(
          "[CircuitBreaker] Connectivity restored, entering HALF_OPEN");
      transitionTo(CircuitBreakerState::HALF_OPEN,
                   CircuitBreakerTrigger::CONNECTIVITY_LOSS);
    }
  }
}

// ---------------------------------------------------------------------------
// Manual control
// ---------------------------------------------------------------------------

void CircuitBreaker::trip(const std::string& reason) {
  spdlog::warn("[CircuitBreaker] Manual trip: {}", reason);
  transitionTo(CircuitBreakerState::OPEN, CircuitBreakerTrigger::MANUAL);
}

void CircuitBreaker::reset() {
  spdlog::info("[CircuitBreaker] Manual reset");
  transitionTo(CircuitBreakerState::CLOSED, CircuitBreakerTrigger::NONE);
}

// ---------------------------------------------------------------------------
// Callback
// ---------------------------------------------------------------------------

void CircuitBreaker::setStateCallback(StateCallback callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_stateCallback = std::move(callback);
}

// ---------------------------------------------------------------------------
// State transition
// ---------------------------------------------------------------------------

void CircuitBreaker::transitionTo(CircuitBreakerState newState,
                                  CircuitBreakerTrigger trigger) {
  auto oldState = m_state.load(std::memory_order_acquire);
  if (oldState == newState) {
    return;
  }

  uint64_t now = utils::TimeUtils::getCurrentMillis();

  // Atomically update the state (hot-path visible immediately)
  m_state.store(newState, std::memory_order_release);

  // Update status fields under mutex
  {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_lastTrigger = trigger;
    m_stateChangeTime = now;

    if (newState == CircuitBreakerState::OPEN) {
      uint64_t cooldown;
      {
        std::lock_guard<std::mutex> cfgLock(m_configMutex);
        cooldown = m_config.cooldownPeriodMs;
      }
      m_cooldownEndTime = now + cooldown;
      ++m_tripCount;
    } else if (newState == CircuitBreakerState::HALF_OPEN) {
      uint64_t testDuration;
      {
        std::lock_guard<std::mutex> cfgLock(m_configMutex);
        testDuration = m_config.halfOpenTestDurationMs;
      }
      // Use cooldownEndTime to track the half-open test window expiry
      m_cooldownEndTime = now + testDuration;
    } else {
      // CLOSED - clear cooldown
      m_cooldownEndTime = 0;
    }
  }

  spdlog::info("[CircuitBreaker] {} -> {} (trigger={})",
               stateToString(oldState), stateToString(newState),
               triggerToString(trigger));

  AUDIT_SYSTEM_EVENT("CircuitBreaker transition: " + stateToString(oldState) +
                         " -> " + stateToString(newState) +
                         " trigger=" + triggerToString(trigger),
                     true);

  // Fire callback outside of locks
  StateCallback cb;
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    cb = m_stateCallback;
  }

  if (cb) {
    try {
      cb(oldState, newState, trigger);
    } catch (const std::exception& e) {
      spdlog::error("[CircuitBreaker] State callback exception: {}", e.what());
    } catch (...) {
      spdlog::error("[CircuitBreaker] State callback unknown exception");
    }
  }
}

// ---------------------------------------------------------------------------
// Price move detection
// ---------------------------------------------------------------------------

void CircuitBreaker::checkPriceMove(double price, uint64_t timestamp) {
  static_cast<void>(price);

  size_t count = m_priceCount.load(std::memory_order_acquire);
  if (count < 2) {
    return; // Need at least 2 data points
  }

  auto currentState = m_state.load(std::memory_order_acquire);
  if (currentState != CircuitBreakerState::CLOSED) {
    return; // Only trip from CLOSED state
  }

  double move1min = calculatePriceMove(60000, timestamp);  // 1 minute in ms
  double move5min = calculatePriceMove(300000, timestamp); // 5 minutes in ms

  {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_lastPriceMove1min = move1min;
    m_lastPriceMove5min = move5min;
  }

  double threshold1min;
  double threshold5min;
  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    threshold1min = m_config.priceMove1minPct;
    threshold5min = m_config.priceMove5minPct;
  }

  if (move1min >= threshold1min) {
    spdlog::warn(
        "[CircuitBreaker] Rapid 1-min price move: {:.4f}% (threshold={:.2f}%)",
        move1min, threshold1min);
    transitionTo(CircuitBreakerState::OPEN,
                 CircuitBreakerTrigger::RAPID_PRICE_MOVE_1MIN);
  } else if (move5min >= threshold5min) {
    spdlog::warn(
        "[CircuitBreaker] Rapid 5-min price move: {:.4f}% (threshold={:.2f}%)",
        move5min, threshold5min);
    transitionTo(CircuitBreakerState::OPEN,
                 CircuitBreakerTrigger::RAPID_PRICE_MOVE_5MIN);
  }
}

double CircuitBreaker::calculatePriceMove(uint64_t windowMs,
                                          uint64_t currentTime) const {
  size_t count = m_priceCount.load(std::memory_order_acquire);
  if (count < 2) {
    return 0.0;
  }

  size_t head = m_priceHead.load(std::memory_order_acquire);

  // Most recent entry
  size_t newestIdx = (head - 1) % MAX_PRICE_HISTORY;
  double newestPrice = m_priceHistory[newestIdx].price;

  if (newestPrice <= 0.0) {
    return 0.0;
  }

  // Walk backwards through the ring buffer to find the oldest entry within
  // the time window (convert windowMs to nanoseconds)
  uint64_t windowNs = windowMs * 1000000ULL;
  uint64_t windowStart =
      (currentTime > windowNs) ? (currentTime - windowNs) : 0;
  double oldestPriceInWindow = newestPrice;
  size_t entriesToScan = std::min(count, MAX_PRICE_HISTORY);

  for (size_t i = 1; i < entriesToScan; ++i) {
    size_t idx = (head - 1 - i) % MAX_PRICE_HISTORY;
    const auto& entry = m_priceHistory[idx];

    if (entry.timestamp < windowStart) {
      break; // Beyond our time window
    }

    oldestPriceInWindow = entry.price;
  }

  if (oldestPriceInWindow <= 0.0) {
    return 0.0;
  }

  // Return absolute percentage move
  return std::abs((newestPrice - oldestPriceInWindow) / oldestPriceInWindow) *
         100.0;
}

// ---------------------------------------------------------------------------
// Cooldown management
// ---------------------------------------------------------------------------

void CircuitBreaker::checkCooldown() {
  auto currentState = m_state.load(std::memory_order_acquire);

  if (currentState == CircuitBreakerState::CLOSED) {
    return; // Nothing to do
  }

  uint64_t now = utils::TimeUtils::getCurrentMillis();
  uint64_t cooldownEnd;

  {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    cooldownEnd = m_cooldownEndTime;
  }

  if (cooldownEnd == 0 || now < cooldownEnd) {
    return; // Cooldown not yet expired
  }

  if (currentState == CircuitBreakerState::OPEN) {
    // Cooldown expired: move to HALF_OPEN for testing
    spdlog::info("[CircuitBreaker] Cooldown expired, entering HALF_OPEN");
    transitionTo(CircuitBreakerState::HALF_OPEN, CircuitBreakerTrigger::NONE);
  } else if (currentState == CircuitBreakerState::HALF_OPEN) {
    // Half-open test duration expired without re-trip: fully recover
    spdlog::info(
        "[CircuitBreaker] HALF_OPEN test passed, recovering to CLOSED");
    transitionTo(CircuitBreakerState::CLOSED, CircuitBreakerTrigger::NONE);
  }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json CircuitBreaker::toJson() const {
  CircuitBreakerStatus status = getStatus();

  return {{"state", stateToString(status.state)},
          {"last_trigger", triggerToString(status.lastTrigger)},
          {"state_change_time", status.stateChangeTime},
          {"cooldown_end_time", status.cooldownEndTime},
          {"last_price_move_1min_pct", status.lastPriceMove1min},
          {"last_price_move_5min_pct", status.lastPriceMove5min},
          {"current_spread_ratio", status.currentSpreadRatio},
          {"current_volume_ratio", status.currentVolumeRatio},
          {"trip_count", status.tripCount},
          {"trading_allowed", isTradingAllowed()}};
}

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

std::string CircuitBreaker::stateToString(CircuitBreakerState state) {
  switch (state) {
  case CircuitBreakerState::CLOSED:
    return "CLOSED";
  case CircuitBreakerState::OPEN:
    return "OPEN";
  case CircuitBreakerState::HALF_OPEN:
    return "HALF_OPEN";
  }
  return "UNKNOWN";
}

std::string CircuitBreaker::triggerToString(CircuitBreakerTrigger trigger) {
  switch (trigger) {
  case CircuitBreakerTrigger::NONE:
    return "NONE";
  case CircuitBreakerTrigger::RAPID_PRICE_MOVE_1MIN:
    return "RAPID_PRICE_MOVE_1MIN";
  case CircuitBreakerTrigger::RAPID_PRICE_MOVE_5MIN:
    return "RAPID_PRICE_MOVE_5MIN";
  case CircuitBreakerTrigger::SPREAD_WIDENING:
    return "SPREAD_WIDENING";
  case CircuitBreakerTrigger::VOLUME_SPIKE:
    return "VOLUME_SPIKE";
  case CircuitBreakerTrigger::MARKET_CRISIS:
    return "MARKET_CRISIS";
  case CircuitBreakerTrigger::LATENCY_DEGRADATION:
    return "LATENCY_DEGRADATION";
  case CircuitBreakerTrigger::CONNECTIVITY_LOSS:
    return "CONNECTIVITY_LOSS";
  case CircuitBreakerTrigger::MANUAL:
    return "MANUAL";
  }
  return "UNKNOWN";
}

} // namespace risk
} // namespace pinnacle
