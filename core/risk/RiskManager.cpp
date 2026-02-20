#include "RiskManager.h"
#include "../utils/AuditLogger.h"

#include <cmath>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace risk {

using pinnacle::utils::AuditLogger;

RiskManager& RiskManager::getInstance() {
  static RiskManager instance;
  return instance;
}

RiskManager::~RiskManager() {
  // Signal the hedge thread to stop and wait for it
  m_hedgeRunning.store(false, std::memory_order_release);
  if (m_hedgeThread.joinable()) {
    m_hedgeThread.join();
  }
}

void RiskManager::initialize(const RiskLimits& limits) {
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_limits = limits;
    m_dailyResetTime = utils::TimeUtils::getCurrentMillis();
  }

  // Reset all atomic state
  m_position.store(0.0, std::memory_order_relaxed);
  m_totalPnL.store(0.0, std::memory_order_relaxed);
  m_peakPnL.store(0.0, std::memory_order_relaxed);
  m_dailyPnL.store(0.0, std::memory_order_relaxed);
  m_dailyVolume.store(0.0, std::memory_order_relaxed);
  m_netExposure.store(0.0, std::memory_order_relaxed);
  m_grossExposure.store(0.0, std::memory_order_relaxed);
  m_halted.store(false, std::memory_order_relaxed);
  m_ordersThisSecond.store(0, std::memory_order_relaxed);
  m_currentSecond.store(0, std::memory_order_relaxed);

  spdlog::info("RiskManager initialized - maxPos={} maxOrderSize={} "
               "dailyLossLimit={} maxDrawdown={}%",
               limits.maxPositionSize, limits.maxOrderSize,
               limits.dailyLossLimit, limits.maxDrawdownPct);

  AUDIT_SYSTEM_EVENT("RiskManager initialized", true);

  // Start the auto-hedge thread if enabled
  if (limits.autoHedgeEnabled) {
    m_hedgeRunning.store(true, std::memory_order_release);
    m_hedgeThread = std::thread(&RiskManager::hedgeLoop, this);
    spdlog::info("Auto-hedge thread started - threshold={}% interval={}ms",
                 limits.hedgeThresholdPct, limits.hedgeIntervalMs);
  }
}

// ---------------------------------------------------------------------------
// Pre-trade check -- lock-free hot path (atomic loads only, no mutex)
// ---------------------------------------------------------------------------
RiskCheckResult RiskManager::checkOrder(OrderSide side, double price,
                                        double quantity,
                                        const std::string& symbol) {
  // 1. Halted check
  if (m_halted.load(std::memory_order_acquire)) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_halted", symbol, false);
    return RiskCheckResult::REJECTED_HALTED;
  }

  // 2. Rate limit check
  uint64_t nowSec = utils::TimeUtils::getCurrentSeconds();
  uint64_t prevSec = m_currentSecond.load(std::memory_order_relaxed);

  if (nowSec != prevSec) {
    // New second -- attempt to reset the counter.
    // If the CAS fails another thread already rolled over, which is fine.
    if (m_currentSecond.compare_exchange_strong(prevSec, nowSec,
                                                std::memory_order_relaxed)) {
      m_ordersThisSecond.store(0, std::memory_order_relaxed);
    }
  }

  // Read limits once (under no lock -- these fields are only written while
  // holding m_stateMutex, but the values are small POD and a torn read is
  // acceptable on the hot path for a best-effort rate limit).
  uint32_t maxOps = m_limits.maxOrdersPerSecond;
  uint32_t currentOps =
      m_ordersThisSecond.fetch_add(1, std::memory_order_relaxed);
  if (currentOps >= maxOps) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_rate_limit", symbol, false);
    return RiskCheckResult::REJECTED_RATE_LIMIT;
  }

  // 3. Order size / value check
  double maxOrderSize = m_limits.maxOrderSize;
  double maxOrderValue = m_limits.maxOrderValue;
  if (quantity > maxOrderSize || (price * quantity) > maxOrderValue) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_order_size", symbol, false);
    return RiskCheckResult::REJECTED_ORDER_SIZE_LIMIT;
  }

  // 4. Position limit check
  double currentPos = m_position.load(std::memory_order_relaxed);
  double projectedPos = (side == OrderSide::BUY) ? (currentPos + quantity)
                                                 : (currentPos - quantity);
  double maxPos = m_limits.maxPositionSize;
  if (std::abs(projectedPos) > maxPos) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_position_limit", symbol,
                         false);
    return RiskCheckResult::REJECTED_POSITION_LIMIT;
  }

  // 5. Daily volume check
  double currentVol = m_dailyVolume.load(std::memory_order_relaxed);
  double maxDailyVol = m_limits.maxDailyVolume;
  if ((currentVol + quantity) > maxDailyVol) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_volume_limit", symbol, false);
    return RiskCheckResult::REJECTED_VOLUME_LIMIT;
  }

  // 6. Daily loss limit check
  double dailyPnL = m_dailyPnL.load(std::memory_order_relaxed);
  double dailyLossLimit = m_limits.dailyLossLimit;
  if (dailyPnL < 0.0 && std::abs(dailyPnL) >= dailyLossLimit) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_daily_loss", symbol, false);
    return RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT;
  }

  // 7. Drawdown check
  double peakPnL = m_peakPnL.load(std::memory_order_relaxed);
  double totalPnL = m_totalPnL.load(std::memory_order_relaxed);
  double drawdownPct = 0.0;
  if (peakPnL > 0.0) {
    drawdownPct = ((peakPnL - totalPnL) / peakPnL) * 100.0;
  }
  double maxDrawdownPct = m_limits.maxDrawdownPct;
  if (drawdownPct >= maxDrawdownPct) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_drawdown", symbol, false);
    return RiskCheckResult::REJECTED_DRAWDOWN_LIMIT;
  }

  // 8. Exposure check
  double notional = price * quantity;
  double gross = m_grossExposure.load(std::memory_order_relaxed);
  double net = m_netExposure.load(std::memory_order_relaxed);
  double projectedGross = gross + notional;
  double projectedNet =
      (side == OrderSide::BUY) ? (net + notional) : (net - notional);

  double maxGross = m_limits.maxGrossExposure;
  double maxNet = m_limits.maxNetExposure;
  double maxNotional = m_limits.maxNotionalExposure;

  if (projectedGross > maxGross || std::abs(projectedNet) > maxNet ||
      notional > maxNotional) {
    AUDIT_ORDER_ACTIVITY("system", "", "rejected_exposure", symbol, false);
    return RiskCheckResult::REJECTED_EXPOSURE_LIMIT;
  }

  return RiskCheckResult::APPROVED;
}

// ---------------------------------------------------------------------------
// Post-trade state update
// ---------------------------------------------------------------------------
void RiskManager::onFill(OrderSide side, double price, double quantity,
                         const std::string& symbol) {
  double notional = price * quantity;

  // Update position atomically
  double prevPos = m_position.load(std::memory_order_relaxed);
  double newPos =
      (side == OrderSide::BUY) ? (prevPos + quantity) : (prevPos - quantity);
  m_position.store(newPos, std::memory_order_release);

  // Update daily volume atomically
  double prevVol = m_dailyVolume.load(std::memory_order_relaxed);
  m_dailyVolume.store(prevVol + quantity, std::memory_order_release);

  // Update exposure -- requires mutex for multi-field consistency
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    double gross = m_grossExposure.load(std::memory_order_relaxed);
    double net = m_netExposure.load(std::memory_order_relaxed);

    gross += notional;
    net = (side == OrderSide::BUY) ? (net + notional) : (net - notional);

    m_grossExposure.store(gross, std::memory_order_release);
    m_netExposure.store(net, std::memory_order_release);
  }

  spdlog::debug("Fill: {} {} {} @ {} | pos={} vol={} notional={}",
                (side == OrderSide::BUY) ? "BUY" : "SELL", quantity, symbol,
                price, newPos, prevVol + quantity, notional);

  // Check for daily reset while we are updating
  checkDailyReset();
}

// ---------------------------------------------------------------------------
// PnL tracking
// ---------------------------------------------------------------------------
void RiskManager::onPnLUpdate(double newPnL) {
  m_totalPnL.store(newPnL, std::memory_order_release);

  // Update peak PnL (lock-free CAS loop)
  double currentPeak = m_peakPnL.load(std::memory_order_relaxed);
  while (newPnL > currentPeak) {
    if (m_peakPnL.compare_exchange_weak(currentPeak, newPnL,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
      break;
    }
    // currentPeak is reloaded by compare_exchange_weak on failure
  }

  // Compute drawdown
  double peak = m_peakPnL.load(std::memory_order_relaxed);
  double drawdownPct = 0.0;
  if (peak > 0.0) {
    drawdownPct = ((peak - newPnL) / peak) * 100.0;
  }

  // Update daily PnL -- we store the total PnL as the daily value here;
  // callers can provide the session-relative PnL.
  m_dailyPnL.store(newPnL, std::memory_order_release);

  // Auto-halt on drawdown breach
  double maxDrawdown;
  double dailyLossLimit;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    maxDrawdown = m_limits.maxDrawdownPct;
    dailyLossLimit = m_limits.dailyLossLimit;
  }

  if (drawdownPct >= maxDrawdown && !m_halted.load(std::memory_order_relaxed)) {
    std::string reason =
        "Drawdown limit breached: " + std::to_string(drawdownPct) +
        "% >= " + std::to_string(maxDrawdown) + "%";
    halt(reason);
    spdlog::error("AUTO-HALT: {}", reason);
  }

  // Auto-halt on daily loss breach
  if (newPnL < 0.0 && std::abs(newPnL) >= dailyLossLimit &&
      !m_halted.load(std::memory_order_relaxed)) {
    std::string reason =
        "Daily loss limit breached: " + std::to_string(std::abs(newPnL)) +
        " >= " + std::to_string(dailyLossLimit);
    halt(reason);
    spdlog::error("AUTO-HALT: {}", reason);
  }
}

// ---------------------------------------------------------------------------
// Halt / Resume
// ---------------------------------------------------------------------------
void RiskManager::halt(const std::string& reason) {
  m_halted.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_haltReason = reason;
  }
  spdlog::warn("Trading HALTED: {}", reason);
  AUDIT_SYSTEM_EVENT("Trading halted: " + reason, true);
}

void RiskManager::resume() {
  m_halted.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_haltReason.clear();
  }
  spdlog::info("Trading RESUMED");
  AUDIT_SYSTEM_EVENT("Trading resumed", true);
}

bool RiskManager::isHalted() const {
  return m_halted.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// Hedging
// ---------------------------------------------------------------------------
bool RiskManager::needsHedge() const {
  double pos = std::abs(m_position.load(std::memory_order_relaxed));
  double maxPos;
  double thresholdPct;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    maxPos = m_limits.maxPositionSize;
    thresholdPct = m_limits.hedgeThresholdPct;
  }

  if (maxPos <= 0.0) {
    return false;
  }

  double utilizationPct = (pos / maxPos) * 100.0;
  return utilizationPct >= thresholdPct;
}

void RiskManager::evaluateHedge() {
  if (!needsHedge()) {
    return;
  }

  double pos = m_position.load(std::memory_order_relaxed);
  if (std::abs(pos) < 1e-12) {
    return;
  }

  HedgeCallback cb;
  {
    std::lock_guard<std::mutex> lock(m_hedgeMutex);
    cb = m_hedgeCallback;
  }

  if (!cb) {
    spdlog::warn("Hedge needed but no callback registered");
    return;
  }

  // If long, sell to reduce; if short, buy to reduce
  OrderSide hedgeSide = (pos > 0.0) ? OrderSide::SELL : OrderSide::BUY;
  double hedgeQty = std::abs(pos);

  spdlog::info("Executing hedge: {} {}",
               (hedgeSide == OrderSide::SELL) ? "SELL" : "BUY", hedgeQty);
  AUDIT_SYSTEM_EVENT(
      "Auto-hedge triggered: " +
          std::string((hedgeSide == OrderSide::SELL) ? "SELL" : "BUY") + " " +
          std::to_string(hedgeQty),
      true);

  cb(hedgeSide, hedgeQty);
}

void RiskManager::hedgeLoop() {
  spdlog::info("Hedge loop started");

  while (m_hedgeRunning.load(std::memory_order_acquire)) {
    uint64_t intervalMs;
    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      intervalMs = m_limits.hedgeIntervalMs;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

    if (!m_hedgeRunning.load(std::memory_order_acquire)) {
      break;
    }

    evaluateHedge();
  }

  spdlog::info("Hedge loop stopped");
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------
RiskState RiskManager::getState() const {
  RiskState state;
  state.currentPosition = m_position.load(std::memory_order_relaxed);
  state.totalPnL = m_totalPnL.load(std::memory_order_relaxed);
  state.peakPnL = m_peakPnL.load(std::memory_order_relaxed);
  state.dailyPnL = m_dailyPnL.load(std::memory_order_relaxed);
  state.dailyVolume = m_dailyVolume.load(std::memory_order_relaxed);
  state.netExposure = m_netExposure.load(std::memory_order_relaxed);
  state.grossExposure = m_grossExposure.load(std::memory_order_relaxed);
  state.isHalted = m_halted.load(std::memory_order_relaxed);
  state.ordersThisSecond = m_ordersThisSecond.load(std::memory_order_relaxed);
  state.currentSecond = m_currentSecond.load(std::memory_order_relaxed);
  state.lastUpdateTime = utils::TimeUtils::getCurrentNanos();

  // Compute drawdown
  if (state.peakPnL > 0.0) {
    state.currentDrawdown =
        ((state.peakPnL - state.totalPnL) / state.peakPnL) * 100.0;
  }

  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    state.haltReason = m_haltReason;
    state.dailyResetTime = m_dailyResetTime;
  }

  return state;
}

RiskLimits RiskManager::getLimits() const {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  return m_limits;
}

double RiskManager::getPosition() const {
  return m_position.load(std::memory_order_relaxed);
}

double RiskManager::getDailyPnL() const {
  return m_dailyPnL.load(std::memory_order_relaxed);
}

double RiskManager::getDrawdown() const {
  double peak = m_peakPnL.load(std::memory_order_relaxed);
  double total = m_totalPnL.load(std::memory_order_relaxed);
  if (peak > 0.0) {
    return ((peak - total) / peak) * 100.0;
  }
  return 0.0;
}

double RiskManager::getPositionUtilization() const {
  double pos = std::abs(m_position.load(std::memory_order_relaxed));
  double maxPos;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    maxPos = m_limits.maxPositionSize;
  }
  if (maxPos <= 0.0) {
    return 0.0;
  }
  return (pos / maxPos) * 100.0;
}

double RiskManager::getDailyLossUtilization() const {
  double pnl = m_dailyPnL.load(std::memory_order_relaxed);
  double limit;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    limit = m_limits.dailyLossLimit;
  }
  if (limit <= 0.0 || pnl >= 0.0) {
    return 0.0;
  }
  return (std::abs(pnl) / limit) * 100.0;
}

// ---------------------------------------------------------------------------
// Limit updates
// ---------------------------------------------------------------------------
void RiskManager::updateLimits(const RiskLimits& limits) {
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_limits = limits;
  }
  spdlog::info("Risk limits updated");
  AUDIT_SYSTEM_EVENT("Risk limits updated", true);
}

void RiskManager::setHedgeCallback(HedgeCallback callback) {
  std::lock_guard<std::mutex> lock(m_hedgeMutex);
  m_hedgeCallback = std::move(callback);
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------
nlohmann::json RiskManager::toJson() const {
  RiskState state = getState();
  return {{"current_position", state.currentPosition},
          {"total_pnl", state.totalPnL},
          {"peak_pnl", state.peakPnL},
          {"daily_pnl", state.dailyPnL},
          {"daily_volume", state.dailyVolume},
          {"current_drawdown", state.currentDrawdown},
          {"net_exposure", state.netExposure},
          {"gross_exposure", state.grossExposure},
          {"is_halted", state.isHalted},
          {"halt_reason", state.haltReason},
          {"last_update_time", state.lastUpdateTime},
          {"daily_reset_time", state.dailyResetTime},
          {"orders_this_second", state.ordersThisSecond},
          {"current_second", state.currentSecond}};
}

void RiskManager::fromJson(const nlohmann::json& j) {
  m_position.store(j.value("current_position", 0.0), std::memory_order_relaxed);
  m_totalPnL.store(j.value("total_pnl", 0.0), std::memory_order_relaxed);
  m_peakPnL.store(j.value("peak_pnl", 0.0), std::memory_order_relaxed);
  m_dailyPnL.store(j.value("daily_pnl", 0.0), std::memory_order_relaxed);
  m_dailyVolume.store(j.value("daily_volume", 0.0), std::memory_order_relaxed);
  m_netExposure.store(j.value("net_exposure", 0.0), std::memory_order_relaxed);
  m_grossExposure.store(j.value("gross_exposure", 0.0),
                        std::memory_order_relaxed);
  m_halted.store(j.value("is_halted", false), std::memory_order_relaxed);
  m_ordersThisSecond.store(j.value("orders_this_second", 0u),
                           std::memory_order_relaxed);
  m_currentSecond.store(j.value("current_second", uint64_t{0}),
                        std::memory_order_relaxed);

  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_haltReason = j.value("halt_reason", std::string{});
    m_dailyResetTime = j.value("daily_reset_time", uint64_t{0});
  }

  spdlog::info("RiskManager state restored from JSON");
}

// ---------------------------------------------------------------------------
// Daily reset
// ---------------------------------------------------------------------------
void RiskManager::resetDaily() {
  m_dailyPnL.store(0.0, std::memory_order_release);
  m_dailyVolume.store(0.0, std::memory_order_release);
  m_ordersThisSecond.store(0, std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_dailyResetTime = utils::TimeUtils::getCurrentMillis();
  }

  spdlog::info("Daily risk counters reset");
  AUDIT_SYSTEM_EVENT("Daily risk counters reset", true);
}

void RiskManager::checkDailyReset() {
  // Use system_clock so we can detect calendar-day boundaries
  auto now = std::chrono::system_clock::now();
  auto today = std::chrono::floor<std::chrono::days>(now);
  uint64_t todayMs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          today.time_since_epoch())
          .count());

  uint64_t lastReset;
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    lastReset = m_dailyResetTime;
  }

  // If the last reset was before today's midnight, do a reset
  if (lastReset < todayMs) {
    resetDaily();
  }
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
std::string RiskManager::resultToString(RiskCheckResult result) {
  switch (result) {
  case RiskCheckResult::APPROVED:
    return "APPROVED";
  case RiskCheckResult::REJECTED_POSITION_LIMIT:
    return "REJECTED_POSITION_LIMIT";
  case RiskCheckResult::REJECTED_EXPOSURE_LIMIT:
    return "REJECTED_EXPOSURE_LIMIT";
  case RiskCheckResult::REJECTED_DRAWDOWN_LIMIT:
    return "REJECTED_DRAWDOWN_LIMIT";
  case RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT:
    return "REJECTED_DAILY_LOSS_LIMIT";
  case RiskCheckResult::REJECTED_ORDER_SIZE_LIMIT:
    return "REJECTED_ORDER_SIZE_LIMIT";
  case RiskCheckResult::REJECTED_RATE_LIMIT:
    return "REJECTED_RATE_LIMIT";
  case RiskCheckResult::REJECTED_CIRCUIT_BREAKER:
    return "REJECTED_CIRCUIT_BREAKER";
  case RiskCheckResult::REJECTED_VOLUME_LIMIT:
    return "REJECTED_VOLUME_LIMIT";
  case RiskCheckResult::REJECTED_HALTED:
    return "REJECTED_HALTED";
  default:
    return "UNKNOWN";
  }
}

} // namespace risk
} // namespace pinnacle
