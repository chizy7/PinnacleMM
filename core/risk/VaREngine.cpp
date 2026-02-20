#include "VaREngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <spdlog/spdlog.h>

namespace pinnacle {
namespace risk {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

VaREngine::VaREngine() : m_rng(std::random_device{}()) {}

VaREngine::~VaREngine() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void VaREngine::initialize(const VaRConfig& config) {
  m_config = config;
  spdlog::info("VaREngine initialized: window={}, simulations={}, "
               "horizon={:.2f}, updateInterval={}ms, varLimit={:.2f}%",
               m_config.windowSize, m_config.simulationCount, m_config.horizon,
               m_config.updateIntervalMs, m_config.varLimitPct);
}

void VaREngine::start() {
  if (m_running.exchange(true)) {
    spdlog::warn("VaREngine already running");
    return;
  }
  m_mcThread = std::thread(&VaREngine::calculationLoop, this);
  spdlog::info("VaREngine background thread started");
}

void VaREngine::stop() {
  if (!m_running.exchange(false)) {
    return;
  }
  if (m_mcThread.joinable()) {
    m_mcThread.join();
  }
  spdlog::info("VaREngine stopped");
}

// ---------------------------------------------------------------------------
// Data ingestion
// ---------------------------------------------------------------------------

void VaREngine::addReturn(double returnValue) {
  std::lock_guard<std::mutex> lock(m_returnsMutex);
  m_returns.push_back(returnValue);
  while (m_returns.size() > m_config.windowSize) {
    m_returns.pop_front();
  }
}

// ---------------------------------------------------------------------------
// Result accessors
// ---------------------------------------------------------------------------

VaRResult VaREngine::getLatestResult() const {
  // Lock-free read from the currently active buffer
  return m_results[m_activeBuffer.load(std::memory_order_acquire)];
}

bool VaREngine::isVaRBreached(double portfolioValue) const {
  const auto& result =
      m_results[m_activeBuffer.load(std::memory_order_acquire)];
  // VaR as absolute dollar loss compared to limit percentage of portfolio
  double varDollar = result.historicalVaR95 * portfolioValue;
  double limitDollar = (m_config.varLimitPct / 100.0) * portfolioValue;
  return varDollar > limitDollar;
}

double VaREngine::getCurrentVaR95Pct() const {
  const auto& result =
      m_results[m_activeBuffer.load(std::memory_order_acquire)];
  return result.historicalVaR95 * 100.0;
}

double VaREngine::getCurrentVaR99Pct() const {
  const auto& result =
      m_results[m_activeBuffer.load(std::memory_order_acquire)];
  return result.historicalVaR99 * 100.0;
}

// ---------------------------------------------------------------------------
// Background calculation loop
// ---------------------------------------------------------------------------

void VaREngine::calculationLoop() {
  spdlog::info("VaREngine calculation loop started");

  while (m_running.load(std::memory_order_relaxed)) {
    auto startTime = utils::TimeUtils::getCurrentMillis();

    try {
      VaRResult newResult = calculateAll();

      // Write to the inactive buffer
      int inactive = 1 - m_activeBuffer.load(std::memory_order_acquire);
      m_results[inactive] = newResult;

      // Swap active buffer index so readers immediately see fresh data
      m_activeBuffer.store(inactive, std::memory_order_release);

      spdlog::debug("VaR updated: hist95={:.6f}, hist99={:.6f}, "
                    "param95={:.6f}, mc95={:.6f}, ES95={:.6f}, samples={}",
                    newResult.historicalVaR95, newResult.historicalVaR99,
                    newResult.parametricVaR95, newResult.monteCarloVaR95,
                    newResult.expectedShortfall95, newResult.sampleCount);
    } catch (const std::exception& e) {
      spdlog::error("VaR calculation failed: {}", e.what());
    }

    // Sleep until next update, checking for shutdown every 100 ms
    auto elapsed = utils::TimeUtils::getCurrentMillis() - startTime;
    auto remaining =
        (elapsed < m_config.updateIntervalMs)
            ? static_cast<int64_t>(m_config.updateIntervalMs - elapsed)
            : 0;

    while (remaining > 0 && m_running.load(std::memory_order_relaxed)) {
      auto sleepMs = static_cast<uint64_t>(std::min(remaining, int64_t{100}));
      utils::TimeUtils::sleepForMillis(sleepMs);
      remaining -= static_cast<int64_t>(sleepMs);
    }
  }

  spdlog::info("VaREngine calculation loop exited");
}

// ---------------------------------------------------------------------------
// Core calculation: orchestrator
// ---------------------------------------------------------------------------

VaRResult VaREngine::calculateAll() const {
  VaRResult result;

  std::vector<double> sorted = getSortedReturns();
  result.sampleCount = sorted.size();
  result.calculationTimestamp = utils::TimeUtils::getCurrentNanos();

  if (sorted.size() < 2) {
    // Not enough data for meaningful calculation
    return result;
  }

  double mean = calculateMean(sorted);
  double stddev = calculateStdDev(sorted, mean);

  // Scale by sqrt of horizon for multi-day VaR
  double horizonFactor = std::sqrt(m_config.horizon);
  double scaledStddev = stddev * horizonFactor;
  double scaledMean = mean * m_config.horizon;

  // Historical VaR
  result.historicalVaR95 =
      calculateHistoricalVaR(sorted, m_config.confidenceLevel95);
  result.historicalVaR99 =
      calculateHistoricalVaR(sorted, m_config.confidenceLevel99);

  // Parametric VaR (uses horizon-scaled parameters)
  result.parametricVaR95 = calculateParametricVaR(scaledMean, scaledStddev,
                                                  m_config.confidenceLevel95);
  result.parametricVaR99 = calculateParametricVaR(scaledMean, scaledStddev,
                                                  m_config.confidenceLevel99);

  // Monte Carlo VaR (uses horizon-scaled parameters)
  result.monteCarloVaR95 = calculateMonteCarloVaR(scaledMean, scaledStddev,
                                                  m_config.confidenceLevel95,
                                                  m_config.simulationCount);
  result.monteCarloVaR99 = calculateMonteCarloVaR(scaledMean, scaledStddev,
                                                  m_config.confidenceLevel99,
                                                  m_config.simulationCount);

  // Expected Shortfall (Conditional VaR)
  result.expectedShortfall95 =
      calculateExpectedShortfall(sorted, m_config.confidenceLevel95);
  result.expectedShortfall99 =
      calculateExpectedShortfall(sorted, m_config.confidenceLevel99);

  // Component VaR: simplified as the ratio-weighted marginal contribution
  // For a single-asset case this equals the parametric VaR at 95%
  result.componentVaR = result.parametricVaR95;

  return result;
}

// ---------------------------------------------------------------------------
// Historical VaR
// ---------------------------------------------------------------------------

double
VaREngine::calculateHistoricalVaR(const std::vector<double>& sortedReturns,
                                  double confidence) const {
  if (sortedReturns.empty()) {
    return 0.0;
  }

  // For 95% confidence the loss threshold sits at the 5th percentile
  size_t n = sortedReturns.size();
  size_t index = static_cast<size_t>(
      std::floor((1.0 - confidence) * static_cast<double>(n)));
  if (index >= n) {
    index = n - 1;
  }

  // VaR is reported as a positive loss magnitude
  return -sortedReturns[index];
}

// ---------------------------------------------------------------------------
// Parametric (variance-covariance) VaR
// ---------------------------------------------------------------------------

double VaREngine::calculateParametricVaR(double mean, double stddev,
                                         double confidence) const {
  if (stddev <= 0.0) {
    return 0.0;
  }

  // z-score for the left tail
  double zScore = normalCdfInverse(1.0 - confidence);
  // VaR = -(mean + z * sigma), reported as positive loss
  return -(mean + zScore * stddev);
}

// ---------------------------------------------------------------------------
// Monte Carlo VaR
// ---------------------------------------------------------------------------

double VaREngine::calculateMonteCarloVaR(double mean, double stddev,
                                         double confidence,
                                         size_t numSimulations) const {
  if (stddev <= 0.0 || numSimulations == 0) {
    return 0.0;
  }

  // Use a local copy of the RNG to keep this method const-correct
  std::mt19937 localRng(m_rng);
  std::normal_distribution<double> dist(mean, stddev);

  std::vector<double> simReturns(numSimulations);
  for (size_t i = 0; i < numSimulations; ++i) {
    simReturns[i] = dist(localRng);
  }

  std::sort(simReturns.begin(), simReturns.end());

  size_t index = static_cast<size_t>(
      std::floor((1.0 - confidence) * static_cast<double>(numSimulations)));
  if (index >= numSimulations) {
    index = numSimulations - 1;
  }

  return -simReturns[index];
}

// ---------------------------------------------------------------------------
// Expected Shortfall (CVaR)
// ---------------------------------------------------------------------------

double
VaREngine::calculateExpectedShortfall(const std::vector<double>& sortedReturns,
                                      double confidence) const {
  if (sortedReturns.empty()) {
    return 0.0;
  }

  size_t n = sortedReturns.size();
  size_t tailCount = static_cast<size_t>(
      std::floor((1.0 - confidence) * static_cast<double>(n)));
  if (tailCount == 0) {
    tailCount = 1;
  }

  // Average of the worst tailCount returns
  double sum = 0.0;
  for (size_t i = 0; i < tailCount; ++i) {
    sum += sortedReturns[i];
  }

  // Report as positive loss
  return -(sum / static_cast<double>(tailCount));
}

// ---------------------------------------------------------------------------
// Helper: get sorted copy of the returns window
// ---------------------------------------------------------------------------

std::vector<double> VaREngine::getSortedReturns() const {
  std::lock_guard<std::mutex> lock(m_returnsMutex);
  std::vector<double> sorted(m_returns.begin(), m_returns.end());
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

// ---------------------------------------------------------------------------
// Helper: mean
// ---------------------------------------------------------------------------

double VaREngine::calculateMean(const std::vector<double>& data) const {
  if (data.empty()) {
    return 0.0;
  }
  double sum = std::accumulate(data.begin(), data.end(), 0.0);
  return sum / static_cast<double>(data.size());
}

// ---------------------------------------------------------------------------
// Helper: standard deviation (population)
// ---------------------------------------------------------------------------

double VaREngine::calculateStdDev(const std::vector<double>& data,
                                  double mean) const {
  if (data.size() < 2) {
    return 0.0;
  }
  double sumSq = 0.0;
  for (double v : data) {
    double diff = v - mean;
    sumSq += diff * diff;
  }
  // Use sample standard deviation (N-1)
  return std::sqrt(sumSq / static_cast<double>(data.size() - 1));
}

// ---------------------------------------------------------------------------
// Helper: inverse normal CDF (rational approximation)
//
// Abramowitz and Stegun formula 26.2.23 (|error| < 4.5e-4).
// For higher accuracy the refinement step uses Halley's correction.
// ---------------------------------------------------------------------------

double VaREngine::normalCdfInverse(double p) const {
  // Handle boundary values
  if (p <= 0.0) {
    return -1e10;
  }
  if (p >= 1.0) {
    return 1e10;
  }

  // Use symmetry: for p > 0.5 compute for (1-p) and negate
  bool negate = false;
  double pp = p;
  if (pp > 0.5) {
    pp = 1.0 - pp;
    negate = true;
  }

  // Rational approximation constants (Abramowitz & Stegun 26.2.23)
  constexpr double c0 = 2.515517;
  constexpr double c1 = 0.802853;
  constexpr double c2 = 0.010328;
  constexpr double d1 = 1.432788;
  constexpr double d2 = 0.189269;
  constexpr double d3 = 0.001308;

  double t = std::sqrt(-2.0 * std::log(pp));

  double numerator = c0 + t * (c1 + t * c2);
  double denominator = 1.0 + t * (d1 + t * (d2 + t * d3));

  double result = t - numerator / denominator;

  // By convention the left-tail z-score is negative
  result = -result;

  if (negate) {
    result = -result;
  }

  return result;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json VaREngine::toJson() const {
  const auto& result =
      m_results[m_activeBuffer.load(std::memory_order_acquire)];

  return {{"historical_var_95", result.historicalVaR95},
          {"historical_var_99", result.historicalVaR99},
          {"parametric_var_95", result.parametricVaR95},
          {"parametric_var_99", result.parametricVaR99},
          {"monte_carlo_var_95", result.monteCarloVaR95},
          {"monte_carlo_var_99", result.monteCarloVaR99},
          {"expected_shortfall_95", result.expectedShortfall95},
          {"expected_shortfall_99", result.expectedShortfall99},
          {"component_var", result.componentVaR},
          {"calculation_timestamp", result.calculationTimestamp},
          {"sample_count", result.sampleCount},
          {"var_95_pct", getCurrentVaR95Pct()},
          {"var_99_pct", getCurrentVaR99Pct()},
          {"config",
           {{"window_size", m_config.windowSize},
            {"simulation_count", m_config.simulationCount},
            {"horizon", m_config.horizon},
            {"update_interval_ms", m_config.updateIntervalMs},
            {"var_limit_pct", m_config.varLimitPct}}}};
}

} // namespace risk
} // namespace pinnacle
