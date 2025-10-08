#include "MLSpreadOptimizer.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

namespace pinnacle {
namespace strategy {
namespace ml {

// Simple neural network implementation optimized for low latency
struct MLSpreadOptimizer::NeuralNetwork {
  static constexpr size_t INPUT_SIZE = 20;  // Number of features
  static constexpr size_t HIDDEN_SIZE = 32; // Hidden layer size
  static constexpr size_t OUTPUT_SIZE = 1;  // Single output (spread)

  // Network weights and biases
  std::array<std::array<double, INPUT_SIZE>, HIDDEN_SIZE> weightsInputHidden;
  std::array<double, HIDDEN_SIZE> biasHidden;
  std::array<std::array<double, HIDDEN_SIZE>, OUTPUT_SIZE> weightsHiddenOutput;
  std::array<double, OUTPUT_SIZE> biasOutput;

  // Activation function (ReLU for hidden, linear for output)
  static double relu(double x) { return std::max(0.0, x); }
  static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

  // Forward pass
  double predict(const std::vector<double>& input) const {
    if (input.size() != INPUT_SIZE) {
      return 0.0; // Invalid input
    }

    // Input to hidden layer
    std::array<double, HIDDEN_SIZE> hidden{};
    for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
      double sum = biasHidden[i];
      for (size_t j = 0; j < INPUT_SIZE; ++j) {
        sum += weightsInputHidden[i][j] * input[j];
      }
      hidden[i] = relu(sum);
    }

    // Hidden to output layer
    double output = biasOutput[0];
    for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
      output += weightsHiddenOutput[0][i] * hidden[i];
    }

    return output; // Linear activation for regression
  }

  // Initialize with random weights
  void initialize() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(0.0, 0.1);

    // Xavier initialization for input->hidden weights
    double stddev = std::sqrt(2.0 / INPUT_SIZE);
    std::normal_distribution<double> inputDist(0.0, stddev);

    for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
      for (size_t j = 0; j < INPUT_SIZE; ++j) {
        weightsInputHidden[i][j] = inputDist(gen);
      }
      biasHidden[i] = 0.0;
    }

    // Xavier initialization for hidden->output weights
    stddev = std::sqrt(2.0 / HIDDEN_SIZE);
    std::normal_distribution<double> hiddenDist(0.0, stddev);

    for (size_t i = 0; i < OUTPUT_SIZE; ++i) {
      for (size_t j = 0; j < HIDDEN_SIZE; ++j) {
        weightsHiddenOutput[i][j] = hiddenDist(gen);
      }
      biasOutput[i] = 0.0;
    }
  }

  // Simple gradient descent training
  void train(const std::vector<std::vector<double>>& inputs,
             const std::vector<double>& targets, double learningRate,
             size_t epochs) {

    if (inputs.empty() || inputs.size() != targets.size()) {
      return;
    }

    for (size_t epoch = 0; epoch < epochs; ++epoch) {
      double totalLoss = 0.0;

      for (size_t sample = 0; sample < inputs.size(); ++sample) {
        const auto& input = inputs[sample];
        double target = targets[sample];

        if (input.size() != INPUT_SIZE) {
          continue;
        }

        // Forward pass
        std::array<double, HIDDEN_SIZE> hidden{};
        for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
          double sum = biasHidden[i];
          for (size_t j = 0; j < INPUT_SIZE; ++j) {
            sum += weightsInputHidden[i][j] * input[j];
          }
          hidden[i] = relu(sum);
        }

        double output = biasOutput[0];
        for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
          output += weightsHiddenOutput[0][i] * hidden[i];
        }

        // Compute loss and gradients
        double loss = (output - target) * (output - target);
        totalLoss += loss;

        double outputGradient = 2.0 * (output - target);

        // Backpropagation - output layer
        for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
          weightsHiddenOutput[0][i] -=
              learningRate * outputGradient * hidden[i];
        }
        biasOutput[0] -= learningRate * outputGradient;

        // Backpropagation - hidden layer
        for (size_t i = 0; i < HIDDEN_SIZE; ++i) {
          if (hidden[i] > 0) { // ReLU derivative
            double hiddenGradient = outputGradient * weightsHiddenOutput[0][i];
            for (size_t j = 0; j < INPUT_SIZE; ++j) {
              weightsInputHidden[i][j] -=
                  learningRate * hiddenGradient * input[j];
            }
            biasHidden[i] -= learningRate * hiddenGradient;
          }
        }
      }

      // Early stopping based on loss convergence
      if (epoch > 10 && totalLoss < 1e-6) {
        break;
      }
    }
  }
};

MLSpreadOptimizer::MLSpreadOptimizer(const Config& config)
    : m_config(config), m_model(std::make_unique<NeuralNetwork>()) {}

MLSpreadOptimizer::~MLSpreadOptimizer() = default;

bool MLSpreadOptimizer::initialize() {
  std::lock_guard<std::mutex> lock(m_modelMutex);

  m_model->initialize();
  m_metrics = ModelMetrics{}; // Reset metrics

  // Initialize with some default data if available
  return true;
}

void MLSpreadOptimizer::addMarketData(double midPrice, double bidPrice,
                                      double askPrice, double bidVolume,
                                      double askVolume, double tradeVolume,
                                      double currentPosition,
                                      uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(m_marketHistoryMutex);

  MarketDataPoint point;
  point.midPrice = midPrice;
  point.bidPrice = bidPrice;
  point.askPrice = askPrice;
  point.bidVolume = bidVolume;
  point.askVolume = askVolume;
  point.tradeVolume = tradeVolume;
  point.currentPosition = currentPosition;
  point.timestamp = timestamp;

  m_marketHistory.push_back(point);

  // Keep only recent data for feature computation
  while (m_marketHistory.size() > m_config.lookbackPeriod * 2) {
    m_marketHistory.pop_front();
  }

  // Trigger cleanup periodically
  if (timestamp - m_lastCleanupTime > 60000000000ULL) { // 60 seconds
    cleanOldData();
    m_lastCleanupTime = timestamp;
  }
}

SpreadPrediction
MLSpreadOptimizer::predictOptimalSpread(const MarketFeatures& features,
                                        const StrategyConfig& strategyConfig) {
  auto startTime = utils::TimeUtils::getCurrentNanos();

  // Check cache first
  {
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    if (m_cache.isValid()) {
      // Simple feature similarity check (could be more sophisticated)
      double featureDiff =
          std::abs(m_cache.features.midPrice - features.midPrice) /
          features.midPrice;
      if (featureDiff < 0.001) { // 0.1% difference
        return m_cache.prediction;
      }
    }
  }

  SpreadPrediction prediction;
  prediction.timestamp = utils::TimeUtils::getCurrentNanos();

  // If model is not ready, use fallback heuristics
  if (!m_isModelTrained.load()) {
    prediction.optimalSpread = predictWithHeuristics(features, strategyConfig);
    prediction.confidence = 0.3; // Low confidence for heuristic predictions
    prediction.expectedPnL = 0.0;
    prediction.fillProbability = 0.5;
    prediction.adverseSelection = 0.0;
  } else {
    std::lock_guard<std::mutex> modelLock(m_modelMutex);

    // Convert features to input vector
    auto inputVector = features.toVector();

    // Get model prediction
    double rawPrediction = m_model->predict(inputVector);

    // Post-process prediction
    double baseSpread =
        strategyConfig.baseSpreadBps * 0.0001 * features.midPrice;
    prediction.optimalSpread = std::max(
        baseSpread * m_config.minSpreadMultiplier,
        std::min(baseSpread * m_config.maxSpreadMultiplier, rawPrediction));

    // Calculate confidence based on feature similarity to training data
    prediction.confidence = calculatePredictionConfidence(features);

    // Estimate expected outcomes
    prediction.expectedPnL =
        estimateExpectedPnL(features, prediction.optimalSpread);
    prediction.fillProbability =
        estimateFillProbability(features, prediction.optimalSpread);
    prediction.adverseSelection =
        estimateAdverseSelection(features, prediction.optimalSpread);
  }

  // Update prediction timing metrics
  auto endTime = utils::TimeUtils::getCurrentNanos();
  double predictionTime =
      (endTime - startTime) / 1000.0; // Convert to microseconds

  {
    std::lock_guard<std::mutex> metricsLock(m_modelMutex);
    m_metrics.avgPredictionTime =
        (m_metrics.avgPredictionTime * 0.95) + (predictionTime * 0.05);
    m_metrics.maxPredictionTime =
        std::max(m_metrics.maxPredictionTime, predictionTime);
    m_metrics.totalPredictions++;
  }

  // Cache the prediction
  {
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    m_cache.features = features;
    m_cache.prediction = prediction;
    m_cache.timestamp = prediction.timestamp;
  }

  return prediction;
}

void MLSpreadOptimizer::updateWithOutcome(const MarketFeatures& features,
                                          double actualSpread,
                                          double realizedPnL, double fillRate,
                                          uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(m_trainingDataMutex);

  TrainingPoint point;
  point.features = features;
  point.targetSpread = actualSpread;
  point.actualPnL = realizedPnL;
  point.fillRate = fillRate;
  point.timestamp = timestamp;

  m_trainingData.push_back(point);

  // Keep training data within limits
  while (m_trainingData.size() > m_config.maxTrainingDataPoints) {
    m_trainingData.pop_front();
  }

  // Check if we need retraining
  if (m_config.enableOnlineLearning &&
      m_trainingData.size() >= m_config.minTrainingDataPoints &&
      (timestamp - m_metrics.lastRetrainTime) >
          (m_config.retrainIntervalMs * 1000000ULL)) {
    m_needsRetraining.store(true);
  }
}

bool MLSpreadOptimizer::trainModel() {
  std::lock_guard<std::mutex> trainingLock(m_trainingDataMutex);
  std::lock_guard<std::mutex> modelLock(m_modelMutex);

  if (m_trainingData.size() < m_config.minTrainingDataPoints) {
    return false;
  }

  // Prepare training data
  std::vector<std::vector<double>> features;
  std::vector<double> targets;
  prepareTrainingData(features, targets);

  if (features.empty()) {
    return false;
  }

  // Train the model
  m_model->train(features, targets, m_config.learningRate, m_config.epochs);

  // Update metrics
  std::vector<double> predictions;
  predictions.reserve(features.size());
  for (const auto& feature : features) {
    predictions.push_back(m_model->predict(feature));
  }
  updateMetrics(predictions, targets);

  m_isModelTrained.store(true);
  m_needsRetraining.store(false);
  m_metrics.retrainCount++;
  m_metrics.lastRetrainTime = utils::TimeUtils::getCurrentNanos();

  return true;
}

ModelMetrics MLSpreadOptimizer::getMetrics() const {
  std::lock_guard<std::mutex> lock(m_modelMutex);
  return m_metrics;
}

std::vector<std::pair<std::string, double>>
MLSpreadOptimizer::getFeatureImportance() const {
  // For the simple neural network, I can approximate feature importance
  // by the magnitude of input weights
  std::lock_guard<std::mutex> lock(m_modelMutex);

  auto featureNames = MarketFeatures::getFeatureNames();
  std::vector<std::pair<std::string, double>> importance;

  // Check if model is initialized and has valid weights
  if (!m_model) {
    // Return zero importance if model not initialized
    for (const auto& name : featureNames) {
      importance.emplace_back(name, 0.0);
    }
    return importance;
  }

  for (size_t i = 0; i < featureNames.size() && i < NeuralNetwork::INPUT_SIZE;
       ++i) {
    double totalWeight = 0.0;
    for (size_t j = 0; j < NeuralNetwork::HIDDEN_SIZE; ++j) {
      double weight = m_model->weightsInputHidden[j][i];
      // Check for NaN/inf and replace with 0
      if (std::isfinite(weight)) {
        totalWeight += std::abs(weight);
      }
    }
    importance.emplace_back(featureNames[i], totalWeight);
  }

  // Sort by importance
  std::sort(importance.begin(), importance.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  return importance;
}

MarketFeatures
MLSpreadOptimizer::computeFeatures(const MarketDataPoint& currentPoint) const {
  MarketFeatures features;
  features.timestamp = currentPoint.timestamp;

  // Basic price features
  features.midPrice = currentPoint.midPrice;
  features.bidAskSpread = currentPoint.askPrice - currentPoint.bidPrice;

  // Calculate features requiring historical data
  if (m_marketHistory.size() >= 2) {
    // Price movement and velocity
    const auto& prevPoint = m_marketHistory[m_marketHistory.size() - 2];
    features.priceMovement =
        (currentPoint.midPrice - prevPoint.midPrice) / prevPoint.midPrice;

    double timeDiff =
        (currentPoint.timestamp - prevPoint.timestamp) / 1e9; // seconds
    if (timeDiff > 0) {
      features.priceVelocity = features.priceMovement / timeDiff;
    }
  }

  // Volatility
  features.priceVolatility = calculateVolatility(m_config.volatilityWindow);

  // Order book features
  features.orderBookImbalance = calculateOrderBookImbalance(currentPoint);
  features.bidBookDepth = currentPoint.bidVolume;
  features.askBookDepth = currentPoint.askVolume;
  features.totalBookDepth = currentPoint.bidVolume + currentPoint.askVolume;

  // Weighted mid price (volume-weighted)
  if (features.totalBookDepth > 0) {
    features.weightedMidPrice =
        (currentPoint.bidPrice * currentPoint.askVolume +
         currentPoint.askPrice * currentPoint.bidVolume) /
        features.totalBookDepth;
  } else {
    features.weightedMidPrice = features.midPrice;
  }

  // Volume features
  features.recentVolume = currentPoint.tradeVolume;
  features.volumeProfile = calculateVolumeProfile(m_config.volumeWindow);

  // Time features
  auto now = std::chrono::system_clock::now();
  auto timeT = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&timeT);

  features.timeOfDay =
      (tm.tm_hour * 60 + tm.tm_min) / (24.0 * 60.0); // Normalize to [0,1]
  features.dayOfWeek = tm.tm_wday / 7.0;             // Normalize to [0,1]
  features.isMarketOpen = true; // Simplified - could check actual market hours

  // Position features
  features.currentPosition = currentPoint.currentPosition;
  // positionRatio and inventoryRisk will be set by the caller with strategy
  // config

  // Market regime features
  features.trendStrength = calculateTrendStrength(50);
  features.meanReversion = 1.0 - std::abs(features.trendStrength); // Simplified
  features.marketStress =
      std::min(1.0, features.priceVolatility * 10.0); // Simplified

  return features;
}

// Implementation of helper methods

double MLSpreadOptimizer::calculateVolatility(size_t lookback) const {
  if (m_marketHistory.size() < 2 || lookback == 0) {
    return 0.0;
  }

  size_t windowSize = std::min(lookback, m_marketHistory.size() - 1);
  size_t startIdx = m_marketHistory.size() - windowSize - 1;

  std::vector<double> returns;
  returns.reserve(windowSize);

  for (size_t i = startIdx; i < m_marketHistory.size() - 1; ++i) {
    double ret =
        std::log(m_marketHistory[i + 1].midPrice / m_marketHistory[i].midPrice);
    returns.push_back(ret);
  }

  if (returns.empty())
    return 0.0;

  double mean =
      std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
  double variance = 0.0;
  for (double ret : returns) {
    variance += (ret - mean) * (ret - mean);
  }
  variance /= returns.size();

  return std::sqrt(variance * 252 * 24 * 60 * 60); // Annualized volatility
}

double MLSpreadOptimizer::calculateVolumeProfile(size_t lookback) const {
  if (m_marketHistory.size() < 2 || lookback == 0) {
    return 0.0;
  }

  size_t windowSize = std::min(lookback, m_marketHistory.size());
  size_t startIdx = m_marketHistory.size() - windowSize;

  double totalVolume = 0.0;
  for (size_t i = startIdx; i < m_marketHistory.size(); ++i) {
    totalVolume += m_marketHistory[i].tradeVolume;
  }

  return totalVolume / windowSize;
}

double MLSpreadOptimizer::calculateTrendStrength(size_t lookback) const {
  if (m_marketHistory.size() < lookback + 1) {
    return 0.0;
  }

  size_t startIdx = m_marketHistory.size() - lookback - 1;
  double startPrice = m_marketHistory[startIdx].midPrice;
  double endPrice = m_marketHistory.back().midPrice;

  return (endPrice - startPrice) / startPrice; // Simple trend measure
}

double MLSpreadOptimizer::calculateOrderBookImbalance(
    const MarketDataPoint& point) const {
  double totalVolume = point.bidVolume + point.askVolume;
  if (totalVolume <= 0)
    return 0.0;

  return (point.bidVolume - point.askVolume) / totalVolume;
}

void MLSpreadOptimizer::prepareTrainingData(
    std::vector<std::vector<double>>& features,
    std::vector<double>& targets) const {
  features.clear();
  targets.clear();

  for (const auto& point : m_trainingData) {
    auto featureVector = point.features.toVector();
    if (featureVector.size() == NeuralNetwork::INPUT_SIZE) {
      features.push_back(featureVector);
      targets.push_back(point.targetSpread);
    }
  }
}

void MLSpreadOptimizer::updateMetrics(const std::vector<double>& predictions,
                                      const std::vector<double>& targets) {
  if (predictions.size() != targets.size() || predictions.empty()) {
    return;
  }

  double mse = 0.0, mae = 0.0;
  for (size_t i = 0; i < predictions.size(); ++i) {
    double error = predictions[i] - targets[i];
    mse += error * error;
    mae += std::abs(error);
  }

  mse /= predictions.size();
  mae /= predictions.size();

  m_metrics.meanSquaredError = mse;
  m_metrics.meanAbsoluteError = mae;

  // Calculate accuracy within 10% tolerance
  size_t correct = 0;
  for (size_t i = 0; i < predictions.size(); ++i) {
    double relativeError = std::abs(predictions[i] - targets[i]) / targets[i];
    if (relativeError < 0.1) {
      correct++;
    }
  }
  m_metrics.accuracy = static_cast<double>(correct) / predictions.size();
  m_metrics.correctPredictions = correct;
}

void MLSpreadOptimizer::cleanOldData() {
  uint64_t cutoffTime = utils::TimeUtils::getCurrentNanos() -
                        (24ULL * 60 * 60 * 1000000000); // 24 hours

  {
    std::lock_guard<std::mutex> lock(m_trainingDataMutex);
    auto it = std::remove_if(m_trainingData.begin(), m_trainingData.end(),
                             [cutoffTime](const TrainingPoint& point) {
                               return point.timestamp < cutoffTime;
                             });
    m_trainingData.erase(it, m_trainingData.end());
  }
}

// Placeholder implementations for complex methods
double MLSpreadOptimizer::predictWithHeuristics(
    const MarketFeatures& features,
    const StrategyConfig& strategyConfig) const {
  // Simple heuristic: base spread adjusted for volatility and imbalance
  double baseSpread = strategyConfig.baseSpreadBps * 0.0001 * features.midPrice;

  // Handle edge case where midPrice is zero
  if (baseSpread <= 0.0) {
    baseSpread =
        strategyConfig.baseSpreadBps * 0.0001; // Default to 1.0 price unit
  }

  // Widen spread during high volatility
  double volatilityMultiplier = 1.0 + features.priceVolatility;

  // Widen spread during high imbalance
  double imbalanceMultiplier = 1.0 + std::abs(features.orderBookImbalance);

  return baseSpread * volatilityMultiplier * imbalanceMultiplier;
}

double MLSpreadOptimizer::calculatePredictionConfidence(
    const MarketFeatures& features) const {
  // Simplified confidence based on data availability and market stability
  double confidence = 0.5;

  if (m_trainingData.size() > m_config.minTrainingDataPoints) {
    confidence += 0.2;
  }

  if (features.priceVolatility < 0.1) { // Low volatility
    confidence += 0.2;
  }

  if (std::abs(features.orderBookImbalance) < 0.3) { // Balanced book
    confidence += 0.1;
  }

  return std::min(1.0, confidence);
}

double MLSpreadOptimizer::estimateExpectedPnL(const MarketFeatures& features,
                                              double spread) const {
  // Simplified P&L estimation based on spread and market activity
  return spread * features.recentVolume * 0.01; // 1% of volume * spread
}

double
MLSpreadOptimizer::estimateFillProbability(const MarketFeatures& features,
                                           double spread) const {
  // Higher probability for narrower spreads
  double baseSpread = features.bidAskSpread;
  if (baseSpread <= 0)
    return 0.5;

  return std::max(0.1, std::min(0.9, baseSpread / spread));
}

double
MLSpreadOptimizer::estimateAdverseSelection(const MarketFeatures& features,
                                            double /* spread */) const {
  // Higher adverse selection risk during high volatility and trend
  return features.priceVolatility * std::abs(features.trendStrength);
}

void MLSpreadOptimizer::forceRetrain() { m_needsRetraining.store(true); }

bool MLSpreadOptimizer::saveModel(const std::string& filename) const {
  std::lock_guard<std::mutex> lock(m_modelMutex);

  std::ofstream file(filename, std::ios::binary);
  if (!file)
    return false;

  // Save model weights (simplified binary format)
  file.write(reinterpret_cast<const char*>(&m_model->weightsInputHidden),
             sizeof(m_model->weightsInputHidden));
  file.write(reinterpret_cast<const char*>(&m_model->biasHidden),
             sizeof(m_model->biasHidden));
  file.write(reinterpret_cast<const char*>(&m_model->weightsHiddenOutput),
             sizeof(m_model->weightsHiddenOutput));
  file.write(reinterpret_cast<const char*>(&m_model->biasOutput),
             sizeof(m_model->biasOutput));

  return file.good();
}

bool MLSpreadOptimizer::loadModel(const std::string& filename) {
  std::lock_guard<std::mutex> lock(m_modelMutex);

  std::ifstream file(filename, std::ios::binary);
  if (!file)
    return false;

  // Load model weights
  file.read(reinterpret_cast<char*>(&m_model->weightsInputHidden),
            sizeof(m_model->weightsInputHidden));
  file.read(reinterpret_cast<char*>(&m_model->biasHidden),
            sizeof(m_model->biasHidden));
  file.read(reinterpret_cast<char*>(&m_model->weightsHiddenOutput),
            sizeof(m_model->weightsHiddenOutput));
  file.read(reinterpret_cast<char*>(&m_model->biasOutput),
            sizeof(m_model->biasOutput));

  if (file.good()) {
    m_isModelTrained.store(true);
    return true;
  }

  return false;
}

// Feature vector implementation
std::vector<double> MarketFeatures::toVector() const {
  return {midPrice,
          bidAskSpread,
          priceVolatility,
          priceMovement,
          priceVelocity,
          orderBookImbalance,
          bidBookDepth,
          askBookDepth,
          totalBookDepth,
          weightedMidPrice,
          recentVolume,
          volumeProfile,
          tradeIntensity,
          largeOrderRatio,
          timeOfDay,
          dayOfWeek,
          isMarketOpen ? 1.0 : 0.0,
          currentPosition,
          positionRatio,
          inventoryRisk};
}

std::vector<std::string> MarketFeatures::getFeatureNames() {
  return {"midPrice",         "bidAskSpread",    "priceVolatility",
          "priceMovement",    "priceVelocity",   "orderBookImbalance",
          "bidBookDepth",     "askBookDepth",    "totalBookDepth",
          "weightedMidPrice", "recentVolume",    "volumeProfile",
          "tradeIntensity",   "largeOrderRatio", "timeOfDay",
          "dayOfWeek",        "isMarketOpen",    "currentPosition",
          "positionRatio",    "inventoryRisk"};
}

} // namespace ml
} // namespace strategy
} // namespace pinnacle
