#pragma once

#include "../../core/utils/DomainTypes.h"
#include "../../core/utils/TimeUtils.h"
#include "../config/StrategyConfig.h"

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace pinnacle {
namespace strategy {
namespace ml {

/**
 * @struct MarketFeatures
 * @brief Market features used for ML-based spread optimization
 */
struct MarketFeatures {
  // Price features
  double midPrice{0.0};
  double bidAskSpread{0.0};
  double priceVolatility{0.0};
  double priceMovement{0.0};
  double priceVelocity{0.0};

  // Order book features
  double orderBookImbalance{0.0};
  double bidBookDepth{0.0};
  double askBookDepth{0.0};
  double totalBookDepth{0.0};
  double weightedMidPrice{0.0};

  // Volume features
  double recentVolume{0.0};
  double volumeProfile{0.0};
  double tradeIntensity{0.0};
  double largeOrderRatio{0.0};

  // Time features
  double timeOfDay{0.0}; // Normalized 0-1
  double dayOfWeek{0.0}; // Normalized 0-1
  bool isMarketOpen{true};

  // Inventory features
  double currentPosition{0.0};
  double positionRatio{0.0}; // position / maxPosition
  double inventoryRisk{0.0};

  // Market regime features
  double trendStrength{0.0};
  double meanReversion{0.0};
  double marketStress{0.0};

  uint64_t timestamp{0};

  // Convert to feature vector for ML model
  std::vector<double> toVector() const;

  // Get feature names for model interpretability
  static std::vector<std::string> getFeatureNames();
};

/**
 * @struct SpreadPrediction
 * @brief ML model prediction for optimal spread
 */
struct SpreadPrediction {
  double optimalSpread{0.0};    // Primary prediction
  double confidence{0.0};       // Model confidence [0-1]
  double expectedPnL{0.0};      // Expected P&L for this spread
  double fillProbability{0.0};  // Probability of order fills
  double adverseSelection{0.0}; // Risk of adverse selection
  uint64_t timestamp{0};

  bool isValid() const { return optimalSpread > 0.0 && confidence > 0.0; }
};

/**
 * @struct ModelMetrics
 * @brief Performance metrics for the ML model
 */
struct ModelMetrics {
  double meanSquaredError{0.0};
  double meanAbsoluteError{0.0};
  double accuracy{0.0};
  double precision{0.0};
  double recall{0.0};
  double f1Score{0.0};

  uint64_t totalPredictions{0};
  uint64_t correctPredictions{0};
  uint64_t retrainCount{0};
  uint64_t lastRetrainTime{0};

  double avgPredictionTime{0.0}; // Microseconds
  double maxPredictionTime{0.0}; // Microseconds
};

/**
 * @class MLSpreadOptimizer
 * @brief Machine Learning based spread optimization for market making
 *
 * This class implements a sophisticated ML model that learns optimal spreads
 * based on market conditions, order book dynamics, and historical performance.
 * It uses ensemble methods with feature engineering tailored for HFT.
 */
class MLSpreadOptimizer {
public:
  /**
   * @brief Configuration for ML model
   */
  struct Config {
    // Model parameters
    size_t maxTrainingDataPoints{10000};
    size_t minTrainingDataPoints{1000};
    double learningRate{0.001};
    size_t batchSize{32};
    size_t epochs{100};

    // Feature engineering
    size_t lookbackPeriod{100};  // Number of historical points
    size_t volatilityWindow{50}; // Window for volatility calculation
    size_t volumeWindow{20};     // Window for volume analysis

    // Model update
    uint64_t retrainIntervalMs{300000}; // 5 minutes
    double performanceThreshold{0.8};   // Retrain if performance drops below
    bool enableOnlineLearning{true};

    // Risk management
    double maxSpreadMultiplier{5.0}; // Max spread vs base spread
    double minSpreadMultiplier{0.1}; // Min spread vs base spread
    double confidenceThreshold{0.5}; // Min confidence for predictions

    // Performance optimization
    bool useGPU{false};
    size_t numThreads{4};
    bool enableCache{true};
  };

  /**
   * @brief Constructor
   */
  explicit MLSpreadOptimizer(const Config& config);

  /**
   * @brief Destructor
   */
  ~MLSpreadOptimizer();

  /**
   * @brief Initialize the ML model
   */
  bool initialize();

  /**
   * @brief Add market data for feature computation and training
   */
  void addMarketData(double midPrice, double bidPrice, double askPrice,
                     double bidVolume, double askVolume, double tradeVolume,
                     double currentPosition, uint64_t timestamp);

  /**
   * @brief Predict optimal spread based on current market conditions
   */
  SpreadPrediction predictOptimalSpread(const MarketFeatures& features,
                                        const StrategyConfig& strategyConfig);

  /**
   * @brief Update model with trading outcome (online learning)
   */
  void updateWithOutcome(const MarketFeatures& features, double actualSpread,
                         double realizedPnL, double fillRate,
                         uint64_t timestamp);

  /**
   * @brief Train model with collected data
   */
  bool trainModel();

  /**
   * @brief Get current model metrics
   */
  ModelMetrics getMetrics() const;

  /**
   * @brief Get feature importance for model interpretability
   */
  std::vector<std::pair<std::string, double>> getFeatureImportance() const;

  /**
   * @brief Save model to file
   */
  bool saveModel(const std::string& filename) const;

  /**
   * @brief Load model from file
   */
  bool loadModel(const std::string& filename);

  /**
   * @brief Get model status and health
   */
  bool isModelReady() const { return m_isModelTrained.load(); }

  /**
   * @brief Force model retraining
   */
  void forceRetrain();

private:
  Config m_config;
  std::atomic<bool> m_isModelTrained{false};
  std::atomic<bool> m_needsRetraining{false};

  // Training data storage
  struct TrainingPoint {
    MarketFeatures features;
    double targetSpread;
    double actualPnL;
    double fillRate;
    uint64_t timestamp;
  };

  std::deque<TrainingPoint> m_trainingData;
  mutable std::mutex m_trainingDataMutex;

  // Market data history for feature computation
  struct MarketDataPoint {
    double midPrice;
    double bidPrice;
    double askPrice;
    double bidVolume;
    double askVolume;
    double tradeVolume;
    double currentPosition;
    uint64_t timestamp;
  };

  std::deque<MarketDataPoint> m_marketHistory;
  mutable std::mutex m_marketHistoryMutex;

  // Model state
  mutable std::mutex m_modelMutex;
  ModelMetrics m_metrics;

  // Model implementation (lightweight neural network)
  struct NeuralNetwork;
  std::unique_ptr<NeuralNetwork> m_model;

  // Feature engineering
  MarketFeatures computeFeatures(const MarketDataPoint& currentPoint) const;
  double calculateVolatility(size_t lookback = 0) const;
  double calculateVolumeProfile(size_t lookback = 0) const;
  double calculateTrendStrength(size_t lookback = 0) const;
  double calculateOrderBookImbalance(const MarketDataPoint& point) const;

  // Model training helpers
  void prepareTrainingData(std::vector<std::vector<double>>& features,
                           std::vector<double>& targets) const;
  void updateMetrics(const std::vector<double>& predictions,
                     const std::vector<double>& targets);

  // Performance optimization
  void cleanOldData();
  uint64_t m_lastCleanupTime{0};

  // Prediction cache for performance
  struct PredictionCache {
    MarketFeatures features;
    SpreadPrediction prediction;
    uint64_t timestamp;
    bool isValid() const {
      return timestamp > 0 && (utils::TimeUtils::getCurrentNanos() -
                               timestamp) < 1000000; // 1ms cache
    }
  };

  mutable PredictionCache m_cache;
  mutable std::mutex m_cacheMutex;

  // Helper methods for ML implementation
  double predictWithHeuristics(const MarketFeatures& features,
                               const StrategyConfig& strategyConfig) const;
  double calculatePredictionConfidence(const MarketFeatures& features) const;
  double estimateExpectedPnL(const MarketFeatures& features,
                             double spread) const;
  double estimateFillProbability(const MarketFeatures& features,
                                 double spread) const;
  double estimateAdverseSelection(const MarketFeatures& features,
                                  double spread) const;
};

} // namespace ml
} // namespace strategy
} // namespace pinnacle
