#pragma once

#include "../../core/utils/TimeUtils.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace analytics {

/**
 * @enum MarketRegime
 * @brief Different market regimes that can be detected
 */
enum class MarketRegime {
  TRENDING_UP,     // Strong upward trend
  TRENDING_DOWN,   // Strong downward trend
  MEAN_REVERTING,  // Price oscillating around mean
  HIGH_VOLATILITY, // Elevated volatility period
  LOW_VOLATILITY,  // Quiet, stable period
  CRISIS,          // Extreme market stress
  CONSOLIDATION,   // Sideways movement
  UNKNOWN          // Cannot classify or insufficient data
};

/**
 * @struct RegimeMetrics
 * @brief Metrics describing current market regime characteristics
 */
struct RegimeMetrics {
  double trendStrength{0.0};   // [-1, 1] where -1=strong down, 1=strong up
  double volatility{0.0};      // Current volatility level
  double meanReversion{0.0};   // [0, 1] likelihood of mean reversion
  double momentum{0.0};        // Rate of price change
  double liquidity{0.0};       // Market liquidity indicator
  double stress{0.0};          // Market stress level [0, 1]
  double autocorrelation{0.0}; // Price return autocorrelation
  double varianceRatio{0.0};   // Variance ratio test statistic
  uint64_t timestamp{0};
};

/**
 * @struct RegimeTransition
 * @brief Information about regime transitions
 */
struct RegimeTransition {
  MarketRegime fromRegime{MarketRegime::UNKNOWN};
  MarketRegime toRegime{MarketRegime::UNKNOWN};
  double confidence{0.0}; // Confidence in the transition [0, 1]
  uint64_t timestamp{0};
  RegimeMetrics metrics;
};

/**
 * @struct RegimeConfiguration
 * @brief Configuration parameters for regime detection
 */
struct RegimeConfiguration {
  // Window sizes for different analyses
  size_t shortWindow{20};  // Short-term analysis window
  size_t mediumWindow{50}; // Medium-term analysis window
  size_t longWindow{200};  // Long-term analysis window

  // Volatility thresholds
  double highVolatilityThreshold{0.02}; // 2% volatility threshold
  double lowVolatilityThreshold{0.005}; // 0.5% volatility threshold

  // Trend detection parameters
  double trendStrengthThreshold{0.3}; // Minimum trend strength
  double consolidationThreshold{0.1}; // Maximum movement for consolidation

  // Mean reversion parameters
  double meanReversionThreshold{0.6};   // Minimum mean reversion confidence
  double autocorrelationThreshold{0.2}; // Autocorrelation threshold

  // Crisis detection parameters
  double crisisVolatilityMultiplier{3.0}; // Multiple of normal volatility
  double crisisDrawdownThreshold{0.05};   // 5% drawdown threshold

  // Update intervals
  uint64_t updateIntervalMs{1000}; // Regime update frequency
  uint64_t minDataPoints{30};      // Minimum data for classification

  // HMM parameters
  size_t hmmStates{4};                  // Number of HMM states
  double hmmConvergenceThreshold{1e-6}; // HMM training convergence
  size_t maxHmmIterations{100};         // Maximum HMM training iterations
};

/**
 * @struct MarketDataPoint
 * @brief Market data point for regime analysis
 */
struct MarketDataPoint {
  double price{0.0};
  double volume{0.0};
  double bid{0.0};
  double ask{0.0};
  double spread{0.0};
  uint64_t timestamp{0};
};

/**
 * @class HiddenMarkovModel
 * @brief Hidden Markov Model for regime detection
 */
class HiddenMarkovModel {
public:
  HiddenMarkovModel(size_t numStates, size_t numObservations);

  // Training and inference
  bool train(const std::vector<std::vector<double>>& observations);
  std::vector<size_t> viterbi(const std::vector<double>& observations) const;
  double getStateProb(size_t state,
                      const std::vector<double>& observation) const;

  // Model parameters
  const std::vector<std::vector<double>>& getTransitionMatrix() const {
    return transitionMatrix_;
  }
  const std::vector<std::vector<double>>& getEmissionMatrix() const {
    return emissionMatrix_;
  }
  const std::vector<double>& getInitialProbs() const { return initialProbs_; }

private:
  size_t numStates_;
  size_t numObservations_;
  std::vector<std::vector<double>> transitionMatrix_;
  std::vector<std::vector<double>> emissionMatrix_;
  std::vector<double> initialProbs_;

  // Helper methods
  void initializeRandomly();
  bool baumWelch(const std::vector<std::vector<double>>& observations);
  void forward(const std::vector<double>& observations,
               std::vector<std::vector<double>>& alpha) const;
  void backward(const std::vector<double>& observations,
                std::vector<std::vector<double>>& beta) const;
};

/**
 * @class MarketRegimeDetector
 * @brief Advanced market regime detection and classification system
 */
class MarketRegimeDetector {
public:
  explicit MarketRegimeDetector(
      const RegimeConfiguration& config = RegimeConfiguration{});
  ~MarketRegimeDetector() = default;

  // Core functionality
  bool initialize();
  void updateMarketData(const MarketDataPoint& dataPoint);
  MarketRegime getCurrentRegime() const;
  RegimeMetrics getCurrentMetrics() const;
  double getRegimeConfidence() const;

  // Regime analysis
  std::vector<RegimeTransition> getRecentTransitions(size_t count = 10) const;
  std::unordered_map<MarketRegime, double> getRegimeProbabilities() const;
  double getRegimePersistence(MarketRegime regime) const;

  // Historical analysis
  std::vector<std::pair<MarketRegime, uint64_t>> getRegimeHistory() const;
  double getAverageRegimeDuration(MarketRegime regime) const;

  // Configuration
  void updateConfiguration(const RegimeConfiguration& config);
  RegimeConfiguration getConfiguration() const;

  // Statistics and monitoring
  std::string getRegimeStatistics() const;
  void resetStatistics();

  // Model management
  bool saveModel(const std::string& filename) const;
  bool loadModel(const std::string& filename);

private:
  // Configuration
  RegimeConfiguration config_;
  mutable std::mutex configMutex_;

  // Market data storage
  std::deque<MarketDataPoint> marketData_;
  std::deque<double> returns_;
  std::deque<double> volatilities_;
  mutable std::mutex dataMutex_;

  // Current state
  std::atomic<MarketRegime> currentRegime_{MarketRegime::UNKNOWN};
  std::atomic<double> regimeConfidence_{0.0};
  RegimeMetrics currentMetrics_;
  mutable std::mutex metricsMutex_;

  // Regime tracking
  std::deque<RegimeTransition> regimeHistory_;
  std::unordered_map<MarketRegime, uint64_t> regimeDurations_;
  std::unordered_map<MarketRegime, size_t> regimeCounts_;
  uint64_t lastRegimeChange_{0};
  mutable std::mutex historyMutex_;

  // Models
  std::unique_ptr<HiddenMarkovModel> hmmModel_;
  std::vector<double> hmmObservations_;
  mutable std::mutex modelMutex_;

  // Statistics
  size_t totalUpdates_{0};
  size_t regimeTransitions_{0};
  double avgRegimeConfidence_{0.0};
  uint64_t lastUpdateTime_{0};

  // Core detection algorithms
  MarketRegime detectRegimeFromMetrics(const RegimeMetrics& metrics) const;
  RegimeMetrics calculateRegimeMetrics() const;

  // Individual detection methods
  double calculateTrendStrength() const;
  double calculateVolatility() const;
  double calculateMeanReversion() const;
  double calculateMomentum() const;
  double calculateLiquidity() const;
  double calculateMarketStress() const;
  double calculateAutocorrelation() const;
  double calculateVarianceRatio() const;

  // HMM-based detection
  MarketRegime detectRegimeHMM() const;
  void updateHMMModel();
  std::vector<double> prepareHMMObservations() const;

  // Utility methods
  std::vector<double> getRecentReturns(size_t count) const;
  std::vector<double> getRecentPrices(size_t count) const;
  std::vector<double> getRecentVolumes(size_t count) const;
  double calculateStandardDeviation(const std::vector<double>& data) const;
  double calculateMean(const std::vector<double>& data) const;
  double calculateCorrelation(const std::vector<double>& x,
                              const std::vector<double>& y) const;

  // Regime classification helpers
  bool isTrendingRegime(const RegimeMetrics& metrics) const;
  bool isVolatilityRegime(const RegimeMetrics& metrics) const;
  bool isCrisisRegime(const RegimeMetrics& metrics) const;
  bool isConsolidationRegime(const RegimeMetrics& metrics) const;

  // Transition detection
  bool shouldTransitionRegime(MarketRegime newRegime, double confidence) const;
  void recordRegimeTransition(MarketRegime newRegime, double confidence);
};

// Utility functions
std::string regimeToString(MarketRegime regime);
MarketRegime stringToRegime(const std::string& regimeStr);
std::string regimeMetricsToString(const RegimeMetrics& metrics);

} // namespace analytics
} // namespace pinnacle
