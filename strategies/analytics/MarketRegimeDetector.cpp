#include "MarketRegimeDetector.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

namespace pinnacle {
namespace analytics {

// HiddenMarkovModel implementation
HiddenMarkovModel::HiddenMarkovModel(size_t numStates, size_t numObservations)
    : numStates_(numStates), numObservations_(numObservations) {
  // Initialize matrices
  transitionMatrix_.resize(numStates_, std::vector<double>(numStates_, 0.0));
  emissionMatrix_.resize(numStates_,
                         std::vector<double>(numObservations_, 0.0));
  initialProbs_.resize(numStates_, 0.0);
  initializeRandomly();
}

void HiddenMarkovModel::initializeRandomly() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  // Initialize transition matrix
  for (size_t i = 0; i < numStates_; ++i) {
    double sum = 0.0;
    for (size_t j = 0; j < numStates_; ++j) {
      transitionMatrix_[i][j] = dist(gen);
      sum += transitionMatrix_[i][j];
    }
    // Normalize
    for (size_t j = 0; j < numStates_; ++j) {
      transitionMatrix_[i][j] /= sum;
    }
  }

  // Initialize emission matrix
  for (size_t i = 0; i < numStates_; ++i) {
    double sum = 0.0;
    for (size_t j = 0; j < numObservations_; ++j) {
      emissionMatrix_[i][j] = dist(gen);
      sum += emissionMatrix_[i][j];
    }
    // Normalize
    for (size_t j = 0; j < numObservations_; ++j) {
      emissionMatrix_[i][j] /= sum;
    }
  }

  // Initialize initial probabilities
  double sum = 0.0;
  for (size_t i = 0; i < numStates_; ++i) {
    initialProbs_[i] = dist(gen);
    sum += initialProbs_[i];
  }
  // Normalize
  for (size_t i = 0; i < numStates_; ++i) {
    initialProbs_[i] /= sum;
  }
}

bool HiddenMarkovModel::train(
    const std::vector<std::vector<double>>& observations) {
  if (observations.empty())
    return false;
  return baumWelch(observations);
}

bool HiddenMarkovModel::baumWelch(
    const std::vector<std::vector<double>>& observations) {
  // Simplified Baum-Welch implementation
  // In practice, this would be more sophisticated
  const size_t maxIterations = 50;
  const double convergenceThreshold = 1e-6;

  for (size_t iter = 0; iter < maxIterations; ++iter) {
    double logLikelihood = 0.0;

    // E-step: Calculate forward/backward probabilities
    for (const auto& obs : observations) {
      std::vector<std::vector<double>> alpha, beta;
      forward(obs, alpha);
      backward(obs, beta);

      // Add to log likelihood
      double seqLikelihood = 0.0;
      for (size_t i = 0; i < numStates_; ++i) {
        seqLikelihood += alpha.back()[i];
      }
      logLikelihood += std::log(seqLikelihood);
    }

    // M-step: Update parameters (simplified)
    // In practice, this would update transition and emission matrices

    // Check convergence (simplified)
    if (iter > 0 && std::abs(logLikelihood) < convergenceThreshold) {
      return true;
    }
  }

  return true; // Assume convergence for simplicity
}

void HiddenMarkovModel::forward(const std::vector<double>& observations,
                                std::vector<std::vector<double>>& alpha) const {
  size_t T = observations.size();
  alpha.resize(T, std::vector<double>(numStates_, 0.0));

  // Initialize
  for (size_t i = 0; i < numStates_; ++i) {
    size_t obsIndex =
        std::min(static_cast<size_t>(observations[0]), numObservations_ - 1);
    alpha[0][i] = initialProbs_[i] * emissionMatrix_[i][obsIndex];
  }

  // Forward pass
  for (size_t t = 1; t < T; ++t) {
    for (size_t j = 0; j < numStates_; ++j) {
      alpha[t][j] = 0.0;
      for (size_t i = 0; i < numStates_; ++i) {
        alpha[t][j] += alpha[t - 1][i] * transitionMatrix_[i][j];
      }
      size_t obsIndex =
          std::min(static_cast<size_t>(observations[t]), numObservations_ - 1);
      alpha[t][j] *= emissionMatrix_[j][obsIndex];
    }
  }
}

void HiddenMarkovModel::backward(const std::vector<double>& observations,
                                 std::vector<std::vector<double>>& beta) const {
  size_t T = observations.size();
  beta.resize(T, std::vector<double>(numStates_, 0.0));

  // Initialize
  for (size_t i = 0; i < numStates_; ++i) {
    beta[T - 1][i] = 1.0;
  }

  // Backward pass
  for (int t = T - 2; t >= 0; --t) {
    for (size_t i = 0; i < numStates_; ++i) {
      beta[t][i] = 0.0;
      for (size_t j = 0; j < numStates_; ++j) {
        size_t obsIndex = std::min(static_cast<size_t>(observations[t + 1]),
                                   numObservations_ - 1);
        beta[t][i] += transitionMatrix_[i][j] * emissionMatrix_[j][obsIndex] *
                      beta[t + 1][j];
      }
    }
  }
}

std::vector<size_t>
HiddenMarkovModel::viterbi(const std::vector<double>& observations) const {
  size_t T = observations.size();
  std::vector<std::vector<double>> delta(T, std::vector<double>(numStates_));
  std::vector<std::vector<size_t>> psi(T, std::vector<size_t>(numStates_));

  // Initialize
  for (size_t i = 0; i < numStates_; ++i) {
    size_t obsIndex =
        std::min(static_cast<size_t>(observations[0]), numObservations_ - 1);
    delta[0][i] =
        std::log(initialProbs_[i]) + std::log(emissionMatrix_[i][obsIndex]);
    psi[0][i] = 0;
  }

  // Forward pass
  for (size_t t = 1; t < T; ++t) {
    for (size_t j = 0; j < numStates_; ++j) {
      double maxVal = -std::numeric_limits<double>::infinity();
      size_t maxIdx = 0;

      for (size_t i = 0; i < numStates_; ++i) {
        double val = delta[t - 1][i] + std::log(transitionMatrix_[i][j]);
        if (val > maxVal) {
          maxVal = val;
          maxIdx = i;
        }
      }

      size_t obsIndex =
          std::min(static_cast<size_t>(observations[t]), numObservations_ - 1);
      delta[t][j] = maxVal + std::log(emissionMatrix_[j][obsIndex]);
      psi[t][j] = maxIdx;
    }
  }

  // Backtrack
  std::vector<size_t> path(T);
  double maxVal = -std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < numStates_; ++i) {
    if (delta[T - 1][i] > maxVal) {
      maxVal = delta[T - 1][i];
      path[T - 1] = i;
    }
  }

  for (int t = T - 2; t >= 0; --t) {
    path[t] = psi[t + 1][path[t + 1]];
  }

  return path;
}

// MarketRegimeDetector implementation
MarketRegimeDetector::MarketRegimeDetector(const RegimeConfiguration& config)
    : config_(config) {
  // Initialize HMM model
  hmmModel_ = std::make_unique<HiddenMarkovModel>(config_.hmmStates,
                                                  10); // 10 observation bins
}

bool MarketRegimeDetector::initialize() {
  std::lock_guard<std::mutex> lock(dataMutex_);

  // Clear any existing data
  marketData_.clear();
  returns_.clear();
  volatilities_.clear();
  hmmObservations_.clear();

  // Reset state
  currentRegime_.store(MarketRegime::UNKNOWN);
  regimeConfidence_.store(0.0);

  // Reset statistics
  totalUpdates_ = 0;
  regimeTransitions_ = 0;
  avgRegimeConfidence_ = 0.0;
  lastUpdateTime_ = 0;
  lastRegimeChange_ = 0;

  return true;
}

void MarketRegimeDetector::updateMarketData(const MarketDataPoint& dataPoint) {
  std::lock_guard<std::mutex> dataLock(dataMutex_);

  // Add new data point
  marketData_.push_back(dataPoint);

  // Calculate return if we have previous data
  if (marketData_.size() > 1) {
    double prevPrice = marketData_[marketData_.size() - 2].price;
    if (prevPrice > 0) {
      double return_val = (dataPoint.price - prevPrice) / prevPrice;
      returns_.push_back(return_val);

      // Keep returns within configured window
      while (returns_.size() > config_.longWindow) {
        returns_.pop_front();
      }
    }
  }

  // Keep market data within window
  while (marketData_.size() > config_.longWindow + 1) {
    marketData_.pop_front();
  }

  totalUpdates_++;
  lastUpdateTime_ = dataPoint.timestamp;

  // Update regime if we have enough data
  if (marketData_.size() >= config_.minDataPoints) {
    // Calculate new metrics
    RegimeMetrics newMetrics = calculateRegimeMetrics();

    {
      std::lock_guard<std::mutex> metricsLock(metricsMutex_);
      currentMetrics_ = newMetrics;
    }

    // Detect regime
    MarketRegime newRegime = detectRegimeFromMetrics(newMetrics);
    double confidence = getRegimeConfidence();

    // Check for regime transition
    MarketRegime oldRegime = currentRegime_.load();
    (void)oldRegime; // Suppress unused variable warning
    if (shouldTransitionRegime(newRegime, confidence)) {
      recordRegimeTransition(newRegime, confidence);
      currentRegime_.store(newRegime);
    }

    regimeConfidence_.store(confidence);
    avgRegimeConfidence_ = (avgRegimeConfidence_ * 0.95) + (confidence * 0.05);

    // Update HMM periodically
    if (totalUpdates_ % 100 == 0) {
      updateHMMModel();
    }
  }
}

MarketRegime MarketRegimeDetector::getCurrentRegime() const {
  return currentRegime_.load();
}

RegimeMetrics MarketRegimeDetector::getCurrentMetrics() const {
  std::lock_guard<std::mutex> lock(metricsMutex_);
  return currentMetrics_;
}

double MarketRegimeDetector::getRegimeConfidence() const {
  return regimeConfidence_.load();
}

RegimeMetrics MarketRegimeDetector::calculateRegimeMetrics() const {
  RegimeMetrics metrics;
  metrics.timestamp = lastUpdateTime_;

  if (marketData_.size() < config_.minDataPoints) {
    return metrics;
  }

  // Calculate all metrics
  metrics.trendStrength = calculateTrendStrength();
  metrics.volatility = calculateVolatility();
  metrics.meanReversion = calculateMeanReversion();
  metrics.momentum = calculateMomentum();
  metrics.liquidity = calculateLiquidity();
  metrics.stress = calculateMarketStress();
  metrics.autocorrelation = calculateAutocorrelation();
  metrics.varianceRatio = calculateVarianceRatio();

  return metrics;
}

double MarketRegimeDetector::calculateTrendStrength() const {
  if (marketData_.size() < config_.shortWindow)
    return 0.0;

  auto recentPrices = getRecentPrices(config_.shortWindow);

  // Calculate linear regression slope
  size_t n = recentPrices.size();
  double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;

  for (size_t i = 0; i < n; ++i) {
    double x = static_cast<double>(i);
    double y = recentPrices[i];
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }

  double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
  double avgPrice = sumY / n;

  // Normalize slope by average price to get percentage trend
  double trendStrength = slope / avgPrice;

  // Clamp to [-1, 1]
  return std::max(-1.0, std::min(1.0, trendStrength * 100.0));
}

double MarketRegimeDetector::calculateVolatility() const {
  if (returns_.size() < config_.shortWindow)
    return 0.0;

  auto recentReturns = getRecentReturns(config_.shortWindow);
  return calculateStandardDeviation(recentReturns);
}

double MarketRegimeDetector::calculateMeanReversion() const {
  if (returns_.size() < config_.mediumWindow)
    return 0.0;

  // Use variance ratio test for mean reversion
  double varianceRatio = calculateVarianceRatio();

  // Convert variance ratio to mean reversion probability
  // VR < 1 suggests mean reversion, VR > 1 suggests momentum
  if (varianceRatio < 1.0) {
    return std::min(1.0, (1.0 - varianceRatio) * 2.0);
  } else {
    return std::max(0.0, 1.0 - (varianceRatio - 1.0));
  }
}

double MarketRegimeDetector::calculateMomentum() const {
  if (marketData_.size() < 2)
    return 0.0;

  double currentPrice = marketData_.back().price;
  double previousPrice = marketData_[marketData_.size() - 2].price;

  if (previousPrice == 0.0)
    return 0.0;

  return (currentPrice - previousPrice) / previousPrice;
}

double MarketRegimeDetector::calculateLiquidity() const {
  if (marketData_.empty())
    return 0.0;

  const auto& latest = marketData_.back();

  // Simple liquidity measure: inverse of bid-ask spread
  double spread = latest.ask - latest.bid;
  double midPrice = (latest.ask + latest.bid) / 2.0;

  if (spread <= 0.0 || midPrice <= 0.0)
    return 0.0;

  double relativeSpread = spread / midPrice;
  return 1.0 / (1.0 + relativeSpread * 100.0); // Normalize to [0, 1]
}

double MarketRegimeDetector::calculateMarketStress() const {
  if (returns_.size() < config_.shortWindow)
    return 0.0;

  auto recentReturns = getRecentReturns(config_.shortWindow);
  double volatility = calculateStandardDeviation(recentReturns);

  // Calculate drawdown
  auto recentPrices = getRecentPrices(config_.shortWindow);
  double maxPrice = *std::max_element(recentPrices.begin(), recentPrices.end());
  double currentPrice = recentPrices.back();
  double drawdown = (maxPrice - currentPrice) / maxPrice;

  // Combine volatility and drawdown for stress measure
  double stressFromVolatility =
      std::min(1.0, volatility / config_.highVolatilityThreshold);
  double stressFromDrawdown =
      std::min(1.0, drawdown / config_.crisisDrawdownThreshold);

  return std::max(stressFromVolatility, stressFromDrawdown);
}

double MarketRegimeDetector::calculateAutocorrelation() const {
  if (returns_.size() < config_.shortWindow)
    return 0.0;

  auto recentReturns = getRecentReturns(config_.shortWindow);

  // Calculate lag-1 autocorrelation
  if (recentReturns.size() < 2)
    return 0.0;

  std::vector<double> x(recentReturns.begin(), recentReturns.end() - 1);
  std::vector<double> y(recentReturns.begin() + 1, recentReturns.end());

  return calculateCorrelation(x, y);
}

double MarketRegimeDetector::calculateVarianceRatio() const {
  if (returns_.size() < config_.mediumWindow)
    return 1.0;

  auto recentReturns = getRecentReturns(config_.mediumWindow);
  size_t n = recentReturns.size();

  if (n < 4)
    return 1.0;

  // Calculate variance of 1-period returns
  double var1 = calculateStandardDeviation(recentReturns);
  var1 = var1 * var1; // Convert to variance

  // Calculate variance of 2-period returns
  std::vector<double> returns2;
  for (size_t i = 1; i < n; i += 2) {
    if (i + 1 < n) {
      returns2.push_back(recentReturns[i] + recentReturns[i + 1]);
    }
  }

  if (returns2.empty())
    return 1.0;

  double var2 = calculateStandardDeviation(returns2);
  var2 = var2 * var2; // Convert to variance

  // Variance ratio
  if (var1 == 0.0)
    return 1.0;
  return var2 / (2.0 * var1);
}

MarketRegime MarketRegimeDetector::detectRegimeFromMetrics(
    const RegimeMetrics& metrics) const {
  // Crisis detection (highest priority)
  if (isCrisisRegime(metrics)) {
    return MarketRegime::CRISIS;
  }

  // Volatility regimes
  if (isVolatilityRegime(metrics)) {
    if (metrics.volatility > config_.highVolatilityThreshold) {
      return MarketRegime::HIGH_VOLATILITY;
    } else {
      return MarketRegime::LOW_VOLATILITY;
    }
  }

  // Trending regimes
  if (isTrendingRegime(metrics)) {
    if (metrics.trendStrength > config_.trendStrengthThreshold) {
      return MarketRegime::TRENDING_UP;
    } else if (metrics.trendStrength < -config_.trendStrengthThreshold) {
      return MarketRegime::TRENDING_DOWN;
    }
  }

  // Mean reversion
  if (metrics.meanReversion > config_.meanReversionThreshold) {
    return MarketRegime::MEAN_REVERTING;
  }

  // Consolidation
  if (isConsolidationRegime(metrics)) {
    return MarketRegime::CONSOLIDATION;
  }

  return MarketRegime::UNKNOWN;
}

bool MarketRegimeDetector::isTrendingRegime(
    const RegimeMetrics& metrics) const {
  return std::abs(metrics.trendStrength) > config_.trendStrengthThreshold &&
         metrics.autocorrelation > config_.autocorrelationThreshold;
}

bool MarketRegimeDetector::isVolatilityRegime(
    const RegimeMetrics& metrics) const {
  return metrics.volatility > config_.highVolatilityThreshold ||
         metrics.volatility < config_.lowVolatilityThreshold;
}

bool MarketRegimeDetector::isCrisisRegime(const RegimeMetrics& metrics) const {
  return metrics.stress > 0.7 || // High stress threshold
         metrics.volatility > config_.highVolatilityThreshold *
                                  config_.crisisVolatilityMultiplier;
}

bool MarketRegimeDetector::isConsolidationRegime(
    const RegimeMetrics& metrics) const {
  return std::abs(metrics.trendStrength) < config_.consolidationThreshold &&
         metrics.volatility < config_.highVolatilityThreshold &&
         metrics.meanReversion < config_.meanReversionThreshold;
}

bool MarketRegimeDetector::shouldTransitionRegime(MarketRegime newRegime,
                                                  double confidence) const {
  MarketRegime currentRegime = currentRegime_.load();

  // Always transition from UNKNOWN
  if (currentRegime == MarketRegime::UNKNOWN) {
    return confidence > 0.5;
  }

  // Don't transition to the same regime
  if (newRegime == currentRegime) {
    return false;
  }

  // Require higher confidence for regime transitions
  double transitionThreshold = 0.7;

  // Crisis regime can be entered with lower threshold
  if (newRegime == MarketRegime::CRISIS) {
    transitionThreshold = 0.6;
  }

  return confidence > transitionThreshold;
}

void MarketRegimeDetector::recordRegimeTransition(MarketRegime newRegime,
                                                  double confidence) {
  std::lock_guard<std::mutex> lock(historyMutex_);

  MarketRegime oldRegime = currentRegime_.load();

  RegimeTransition transition;
  transition.fromRegime = oldRegime;
  transition.toRegime = newRegime;
  transition.confidence = confidence;
  transition.timestamp = lastUpdateTime_;
  transition.metrics = currentMetrics_;

  regimeHistory_.push_back(transition);

  // Keep history within limits
  while (regimeHistory_.size() > 1000) {
    regimeHistory_.pop_front();
  }

  // Update statistics
  if (oldRegime != MarketRegime::UNKNOWN) {
    uint64_t duration = lastUpdateTime_ - lastRegimeChange_;
    regimeDurations_[oldRegime] += duration;
    regimeCounts_[oldRegime]++;
  }

  lastRegimeChange_ = lastUpdateTime_;
  regimeTransitions_++;
}

std::vector<double> MarketRegimeDetector::getRecentReturns(size_t count) const {
  std::vector<double> result;
  size_t startIdx = (returns_.size() > count) ? returns_.size() - count : 0;

  for (size_t i = startIdx; i < returns_.size(); ++i) {
    result.push_back(returns_[i]);
  }

  return result;
}

std::vector<double> MarketRegimeDetector::getRecentPrices(size_t count) const {
  std::vector<double> result;
  size_t startIdx =
      (marketData_.size() > count) ? marketData_.size() - count : 0;

  for (size_t i = startIdx; i < marketData_.size(); ++i) {
    result.push_back(marketData_[i].price);
  }

  return result;
}

std::vector<double> MarketRegimeDetector::getRecentVolumes(size_t count) const {
  std::vector<double> result;
  size_t startIdx =
      (marketData_.size() > count) ? marketData_.size() - count : 0;

  for (size_t i = startIdx; i < marketData_.size(); ++i) {
    result.push_back(marketData_[i].volume);
  }

  return result;
}

double MarketRegimeDetector::calculateStandardDeviation(
    const std::vector<double>& data) const {
  if (data.empty())
    return 0.0;

  double mean = calculateMean(data);
  double sumSquaredDiff = 0.0;

  for (double value : data) {
    double diff = value - mean;
    sumSquaredDiff += diff * diff;
  }

  return std::sqrt(sumSquaredDiff / data.size());
}

double
MarketRegimeDetector::calculateMean(const std::vector<double>& data) const {
  if (data.empty())
    return 0.0;

  double sum = std::accumulate(data.begin(), data.end(), 0.0);
  return sum / data.size();
}

double
MarketRegimeDetector::calculateCorrelation(const std::vector<double>& x,
                                           const std::vector<double>& y) const {
  if (x.size() != y.size() || x.empty())
    return 0.0;

  double meanX = calculateMean(x);
  double meanY = calculateMean(y);

  double numerator = 0.0;
  double sumSquaredDiffX = 0.0;
  double sumSquaredDiffY = 0.0;

  for (size_t i = 0; i < x.size(); ++i) {
    double diffX = x[i] - meanX;
    double diffY = y[i] - meanY;

    numerator += diffX * diffY;
    sumSquaredDiffX += diffX * diffX;
    sumSquaredDiffY += diffY * diffY;
  }

  double denominator = std::sqrt(sumSquaredDiffX * sumSquaredDiffY);

  if (denominator == 0.0)
    return 0.0;

  return numerator / denominator;
}

void MarketRegimeDetector::updateHMMModel() {
  std::lock_guard<std::mutex> lock(modelMutex_);

  if (returns_.size() < config_.mediumWindow)
    return;

  // Prepare observations for HMM
  auto observations = prepareHMMObservations();
  if (observations.empty())
    return;

  // Train HMM (simplified implementation)
  std::vector<std::vector<double>> obsSeq = {observations};
  hmmModel_->train(obsSeq);
}

std::vector<double> MarketRegimeDetector::prepareHMMObservations() const {
  std::vector<double> observations;

  for (double return_val : returns_) {
    // Discretize returns into bins
    int bin = static_cast<int>((return_val + 0.1) * 50); // Scale and shift
    bin = std::max(0, std::min(9, bin));                 // Clamp to [0, 9]
    observations.push_back(static_cast<double>(bin));
  }

  return observations;
}

std::vector<RegimeTransition>
MarketRegimeDetector::getRecentTransitions(size_t count) const {
  std::lock_guard<std::mutex> lock(historyMutex_);

  std::vector<RegimeTransition> result;
  size_t startIdx =
      (regimeHistory_.size() > count) ? regimeHistory_.size() - count : 0;

  for (size_t i = startIdx; i < regimeHistory_.size(); ++i) {
    result.push_back(regimeHistory_[i]);
  }

  return result;
}

std::string MarketRegimeDetector::getRegimeStatistics() const {
  std::ostringstream oss;

  oss << "=== Market Regime Detection Statistics ===\n";
  oss << "Current Regime: " << regimeToString(getCurrentRegime()) << "\n";
  oss << "Regime Confidence: " << (getRegimeConfidence() * 100.0) << "%\n";
  oss << "Total Updates: " << totalUpdates_ << "\n";
  oss << "Regime Transitions: " << regimeTransitions_ << "\n";
  oss << "Avg Confidence: " << (avgRegimeConfidence_ * 100.0) << "%\n";

  auto metrics = getCurrentMetrics();
  oss << "\n=== Current Regime Metrics ===\n";
  oss << "Trend Strength: " << metrics.trendStrength << "\n";
  oss << "Volatility: " << (metrics.volatility * 100.0) << "%\n";
  oss << "Mean Reversion: " << (metrics.meanReversion * 100.0) << "%\n";
  oss << "Momentum: " << (metrics.momentum * 100.0) << "%\n";
  oss << "Market Stress: " << (metrics.stress * 100.0) << "%\n";
  oss << "Autocorrelation: " << metrics.autocorrelation << "\n";
  oss << "Variance Ratio: " << metrics.varianceRatio << "\n";

  return oss.str();
}

void MarketRegimeDetector::updateConfiguration(
    const RegimeConfiguration& config) {
  std::lock_guard<std::mutex> lock(configMutex_);
  config_ = config;
}

RegimeConfiguration MarketRegimeDetector::getConfiguration() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return config_;
}

// Utility functions
std::string regimeToString(MarketRegime regime) {
  switch (regime) {
  case MarketRegime::TRENDING_UP:
    return "TRENDING_UP";
  case MarketRegime::TRENDING_DOWN:
    return "TRENDING_DOWN";
  case MarketRegime::MEAN_REVERTING:
    return "MEAN_REVERTING";
  case MarketRegime::HIGH_VOLATILITY:
    return "HIGH_VOLATILITY";
  case MarketRegime::LOW_VOLATILITY:
    return "LOW_VOLATILITY";
  case MarketRegime::CRISIS:
    return "CRISIS";
  case MarketRegime::CONSOLIDATION:
    return "CONSOLIDATION";
  case MarketRegime::UNKNOWN:
    return "UNKNOWN";
  default:
    return "UNKNOWN";
  }
}

MarketRegime stringToRegime(const std::string& regimeStr) {
  if (regimeStr == "TRENDING_UP")
    return MarketRegime::TRENDING_UP;
  if (regimeStr == "TRENDING_DOWN")
    return MarketRegime::TRENDING_DOWN;
  if (regimeStr == "MEAN_REVERTING")
    return MarketRegime::MEAN_REVERTING;
  if (regimeStr == "HIGH_VOLATILITY")
    return MarketRegime::HIGH_VOLATILITY;
  if (regimeStr == "LOW_VOLATILITY")
    return MarketRegime::LOW_VOLATILITY;
  if (regimeStr == "CRISIS")
    return MarketRegime::CRISIS;
  if (regimeStr == "CONSOLIDATION")
    return MarketRegime::CONSOLIDATION;
  return MarketRegime::UNKNOWN;
}

std::string regimeMetricsToString(const RegimeMetrics& metrics) {
  std::ostringstream oss;
  oss << "Trend: " << metrics.trendStrength << ", Vol: " << metrics.volatility
      << ", MeanRev: " << metrics.meanReversion
      << ", Momentum: " << metrics.momentum << ", Stress: " << metrics.stress;
  return oss.str();
}

bool MarketRegimeDetector::saveModel(const std::string& filename) const {
  try {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }

    // Save configuration
    file.write(reinterpret_cast<const char*>(&config_), sizeof(config_));

    // Save current metrics
    std::lock_guard<std::mutex> lock(metricsMutex_);
    file.write(reinterpret_cast<const char*>(&currentMetrics_),
               sizeof(currentMetrics_));

    // Save regime history size and data
    std::lock_guard<std::mutex> dataLock(dataMutex_);
    size_t historySize = regimeHistory_.size();
    file.write(reinterpret_cast<const char*>(&historySize),
               sizeof(historySize));

    for (const auto& transition : regimeHistory_) {
      file.write(reinterpret_cast<const char*>(&transition),
                 sizeof(transition));
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool MarketRegimeDetector::loadModel(const std::string& filename) {
  try {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }

    // Load configuration
    file.read(reinterpret_cast<char*>(&config_), sizeof(config_));

    // Load current metrics
    std::lock_guard<std::mutex> lock(metricsMutex_);
    file.read(reinterpret_cast<char*>(&currentMetrics_),
              sizeof(currentMetrics_));

    // Load regime history
    std::lock_guard<std::mutex> dataLock(dataMutex_);
    size_t historySize;
    file.read(reinterpret_cast<char*>(&historySize), sizeof(historySize));

    regimeHistory_.clear();
    for (size_t i = 0; i < historySize; ++i) {
      RegimeTransition transition;
      file.read(reinterpret_cast<char*>(&transition), sizeof(transition));
      regimeHistory_.push_back(transition);
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace analytics
} // namespace pinnacle
