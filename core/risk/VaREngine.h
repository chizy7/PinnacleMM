#pragma once

#include "../utils/TimeUtils.h"
#include "RiskConfig.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace pinnacle {
namespace risk {

struct VaRResult {
  double historicalVaR95{0.0};
  double historicalVaR99{0.0};
  double parametricVaR95{0.0};
  double parametricVaR99{0.0};
  double monteCarloVaR95{0.0};
  double monteCarloVaR99{0.0};
  double expectedShortfall95{0.0};
  double expectedShortfall99{0.0};
  double componentVaR{0.0};
  uint64_t calculationTimestamp{0};
  size_t sampleCount{0};
};

class VaREngine {
public:
  VaREngine();
  ~VaREngine();

  void initialize(const VaRConfig& config);
  void start();
  void stop();

  // Feed returns data
  void addReturn(double returnValue);

  // Get latest result (lock-free read from double buffer)
  VaRResult getLatestResult() const;

  // Check if VaR exceeds limit
  bool isVaRBreached(double portfolioValue) const;

  // Get current VaR as percentage
  double getCurrentVaR95Pct() const;
  double getCurrentVaR99Pct() const;

  // Serialization
  nlohmann::json toJson() const;

private:
  VaRConfig m_config;

  // Returns window
  std::deque<double> m_returns;
  mutable std::mutex m_returnsMutex;

  // Double-buffered results for lock-free reads
  std::atomic<int> m_activeBuffer{0};
  VaRResult m_results[2];
  mutable std::mutex m_resultsMutex;

  // Background Monte Carlo thread
  std::thread m_mcThread;
  std::atomic<bool> m_running{false};

  // Random number generator
  std::mt19937 m_rng;

  // Calculation methods
  VaRResult calculateAll() const;
  double calculateHistoricalVaR(const std::vector<double>& sortedReturns,
                                double confidence) const;
  double calculateParametricVaR(double mean, double stddev,
                                double confidence) const;
  double calculateMonteCarloVaR(double mean, double stddev, double confidence,
                                size_t numSimulations) const;
  double calculateExpectedShortfall(const std::vector<double>& sortedReturns,
                                    double confidence) const;

  // Helper methods
  std::vector<double> getSortedReturns() const;
  double calculateMean(const std::vector<double>& data) const;
  double calculateStdDev(const std::vector<double>& data, double mean) const;
  double normalCdfInverse(double p) const;

  // Background calculation loop
  void calculationLoop();
};

} // namespace risk
} // namespace pinnacle
