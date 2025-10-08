#include "../../core/utils/TimeUtils.h"
#include "../../strategies/config/StrategyConfig.h"
#include "../../strategies/ml/MLSpreadOptimizer.h"

#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <thread>

using namespace pinnacle::strategy;
using namespace pinnacle::strategy::ml;

class MLSpreadOptimizerTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_ = MLSpreadOptimizer::Config{};
    config_.maxTrainingDataPoints = 1000;
    config_.minTrainingDataPoints = 10;
    config_.learningRate = 0.01;
    config_.batchSize = 16;
    config_.epochs = 50;
    config_.lookbackPeriod = 50;

    optimizer_ = std::make_unique<MLSpreadOptimizer>(config_);
    strategyConfig_ = StrategyConfig{};
    strategyConfig_.baseSpreadBps = 10.0;
    strategyConfig_.minSpreadBps = 5.0;
    strategyConfig_.maxSpreadBps = 50.0;
  }

  void TearDown() override { optimizer_.reset(); }

  // Helper methods
  MarketFeatures createTestFeatures(double midPrice = 50000.0,
                                    double volatility = 0.02,
                                    double imbalance = 0.1) {
    MarketFeatures features;
    features.midPrice = midPrice;
    features.bidAskSpread = midPrice * 0.0001; // 1 bps
    features.priceVolatility = volatility;
    features.orderBookImbalance = imbalance;
    features.bidBookDepth = 1000.0;
    features.askBookDepth = 1200.0;
    features.totalBookDepth = 2200.0;
    features.weightedMidPrice = midPrice;
    features.recentVolume = 50.0;
    features.timeOfDay = 0.5; // Noon
    features.dayOfWeek = 0.5; // Wednesday
    features.isMarketOpen = true;
    features.currentPosition = 0.0;
    features.positionRatio = 0.0;
    features.inventoryRisk = 0.0;
    features.timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();
    return features;
  }

  void addTrainingData(size_t count) {
    for (size_t i = 0; i < count; ++i) {
      double price = 50000.0 + (i % 100 - 50) * 10; // Varying prices
      double volatility = 0.01 + (i % 10) * 0.001;  // Varying volatility
      double imbalance = -0.2 + (i % 20) * 0.02;    // Varying imbalance

      auto features = createTestFeatures(price, volatility, imbalance);

      // Simulate optimal spread based on features
      double optimalSpread = strategyConfig_.baseSpreadBps * 0.0001 * price *
                             (1.0 + volatility) * (1.0 + std::abs(imbalance));

      optimizer_->updateWithOutcome(features, optimalSpread,
                                    optimalSpread * 0.1, // Simulated P&L
                                    0.8,                 // Simulated fill rate
                                    features.timestamp + i * 1000000);
    }
  }

  MLSpreadOptimizer::Config config_;
  std::unique_ptr<MLSpreadOptimizer> optimizer_;
  StrategyConfig strategyConfig_;
};

TEST_F(MLSpreadOptimizerTest, InitializationTest) {
  EXPECT_TRUE(optimizer_->initialize());
  EXPECT_FALSE(optimizer_->isModelReady()); // Should not be ready until trained

  auto metrics = optimizer_->getMetrics();
  EXPECT_EQ(metrics.totalPredictions, 0);
  EXPECT_EQ(metrics.retrainCount, 0);
}

TEST_F(MLSpreadOptimizerTest, MarketDataIngestionTest) {
  EXPECT_TRUE(optimizer_->initialize());

  // Add market data
  uint64_t timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();
  for (int i = 0; i < 100; ++i) {
    optimizer_->addMarketData(50000.0 + i, // midPrice
                              49995.0 + i, // bidPrice
                              50005.0 + i, // askPrice
                              1000.0,      // bidVolume
                              1200.0,      // askVolume
                              50.0,        // tradeVolume
                              i * 0.01,    // currentPosition
                              timestamp + i * 1000000);
  }

  // Should be able to predict (even with heuristics)
  auto features = createTestFeatures();
  auto prediction = optimizer_->predictOptimalSpread(features, strategyConfig_);

  EXPECT_TRUE(prediction.isValid());
  EXPECT_GT(prediction.optimalSpread, 0.0);
  EXPECT_GE(prediction.confidence, 0.0);
  EXPECT_LE(prediction.confidence, 1.0);
}

TEST_F(MLSpreadOptimizerTest, HeuristicPredictionTest) {
  EXPECT_TRUE(optimizer_->initialize());

  // Without training data, should use heuristics
  auto features =
      createTestFeatures(50000.0, 0.05, 0.2); // High volatility and imbalance
  auto prediction = optimizer_->predictOptimalSpread(features, strategyConfig_);

  EXPECT_TRUE(prediction.isValid());
  EXPECT_GT(prediction.optimalSpread, 0.0);
  EXPECT_LT(prediction.confidence,
            0.5); // Should have low confidence for heuristics

  // Spread should be reasonable
  double baseSpread =
      strategyConfig_.baseSpreadBps * 0.0001 * features.midPrice;
  EXPECT_GT(prediction.optimalSpread,
            baseSpread * 0.5); // At least half of base
  EXPECT_LT(prediction.optimalSpread,
            baseSpread * 10.0); // Not more than 10x base
}

TEST_F(MLSpreadOptimizerTest, TrainingTest) {
  EXPECT_TRUE(optimizer_->initialize());

  // Add training data
  addTrainingData(50);

  // Should still not be ready (below minimum threshold)
  EXPECT_FALSE(optimizer_->isModelReady());

  // Add more data to meet minimum threshold
  addTrainingData(50);

  // Train the model
  EXPECT_TRUE(optimizer_->trainModel());
  EXPECT_TRUE(optimizer_->isModelReady());

  auto metrics = optimizer_->getMetrics();
  EXPECT_GT(metrics.retrainCount, 0);
  EXPECT_GE(metrics.meanSquaredError, 0.0);
  EXPECT_GE(metrics.meanAbsoluteError, 0.0);
}

TEST_F(MLSpreadOptimizerTest, MLPredictionTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(100);
  EXPECT_TRUE(optimizer_->trainModel());

  // Test ML prediction
  auto features = createTestFeatures(50000.0, 0.02, 0.1);
  auto prediction = optimizer_->predictOptimalSpread(features, strategyConfig_);

  EXPECT_TRUE(prediction.isValid());
  EXPECT_GT(prediction.optimalSpread, 0.0);
  EXPECT_GT(prediction.confidence,
            0.3); // Should have higher confidence with ML

  // Test different market conditions
  auto highVolFeatures = createTestFeatures(50000.0, 0.1, 0.3);
  auto highVolPrediction =
      optimizer_->predictOptimalSpread(highVolFeatures, strategyConfig_);

  EXPECT_TRUE(highVolPrediction.isValid());
  // High volatility should generally lead to wider spreads
  EXPECT_GE(highVolPrediction.optimalSpread, prediction.optimalSpread);
}

TEST_F(MLSpreadOptimizerTest, OnlineLearningTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(100);
  EXPECT_TRUE(optimizer_->trainModel());

  auto initialMetrics = optimizer_->getMetrics();

  // Make predictions and add outcomes to trigger online learning
  auto features = createTestFeatures();
  for (int i = 0; i < 20; ++i) {
    // First make a prediction (this increments totalPredictions)
    auto prediction =
        optimizer_->predictOptimalSpread(features, strategyConfig_);
    EXPECT_TRUE(prediction.isValid());

    // Then update with the outcome
    optimizer_->updateWithOutcome(features, 5.0 + i * 0.1, 1.0, 0.8,
                                  features.timestamp + i * 1000000);
  }

  // Metrics should be updated
  auto updatedMetrics = optimizer_->getMetrics();
  EXPECT_GT(updatedMetrics.totalPredictions, initialMetrics.totalPredictions);
}

TEST_F(MLSpreadOptimizerTest, FeatureEngineeringTest) {
  MarketFeatures features;

  // Test feature vector conversion
  auto featureVector = features.toVector();
  EXPECT_EQ(featureVector.size(),
            20); // Should match INPUT_SIZE in neural network

  // Test feature names
  auto featureNames = MarketFeatures::getFeatureNames();
  EXPECT_EQ(featureNames.size(), 20);
  EXPECT_EQ(featureNames[0], "midPrice");
  EXPECT_EQ(featureNames[1], "bidAskSpread");
}

TEST_F(MLSpreadOptimizerTest, PerformanceTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(200);
  EXPECT_TRUE(optimizer_->trainModel());

  auto features = createTestFeatures();

  // Measure prediction time
  auto startTime = std::chrono::high_resolution_clock::now();

  const int numPredictions = 1000;
  for (int i = 0; i < numPredictions; ++i) {
    auto prediction =
        optimizer_->predictOptimalSpread(features, strategyConfig_);
    EXPECT_TRUE(prediction.isValid());
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);

  double avgTimePerPrediction =
      static_cast<double>(duration.count()) / numPredictions;

  // Should be very fast (< 100 microseconds per prediction)
  EXPECT_LT(avgTimePerPrediction, 100.0);

  auto metrics = optimizer_->getMetrics();
  EXPECT_GT(metrics.avgPredictionTime, 0.0);
  EXPECT_LT(metrics.avgPredictionTime, 100.0); // Should be under 100 μs
}

TEST_F(MLSpreadOptimizerTest, ModelPersistenceTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(100);
  EXPECT_TRUE(optimizer_->trainModel());

  // Test model saving
  std::string filename = "/tmp/test_ml_model.bin";
  EXPECT_TRUE(optimizer_->saveModel(filename));

  // Create new optimizer and load model
  auto newOptimizer = std::make_unique<MLSpreadOptimizer>(config_);
  EXPECT_TRUE(newOptimizer->initialize());
  EXPECT_TRUE(newOptimizer->loadModel(filename));
  EXPECT_TRUE(newOptimizer->isModelReady());

  // Test that predictions are consistent
  auto features = createTestFeatures();
  auto prediction1 =
      optimizer_->predictOptimalSpread(features, strategyConfig_);
  auto prediction2 =
      newOptimizer->predictOptimalSpread(features, strategyConfig_);

  EXPECT_NEAR(prediction1.optimalSpread, prediction2.optimalSpread, 0.01);

  // Cleanup
  std::remove(filename.c_str());
}

TEST_F(MLSpreadOptimizerTest, FeatureImportanceTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(200);
  EXPECT_TRUE(optimizer_->trainModel());

  auto importance = optimizer_->getFeatureImportance();
  EXPECT_FALSE(importance.empty());

  // Check that importance values are reasonable
  for (const auto& [feature, value] : importance) {
    EXPECT_FALSE(feature.empty());
    EXPECT_GE(value, 0.0);
  }

  // Should be sorted by importance (descending)
  for (size_t i = 1; i < importance.size(); ++i) {
    EXPECT_GE(importance[i - 1].second, importance[i].second);
  }
}

TEST_F(MLSpreadOptimizerTest, EdgeCasesTest) {
  EXPECT_TRUE(optimizer_->initialize());

  // Test with extreme market conditions
  MarketFeatures extremeFeatures;
  extremeFeatures.midPrice = 100000.0;      // Very high price
  extremeFeatures.priceVolatility = 1.0;    // 100% volatility
  extremeFeatures.orderBookImbalance = 1.0; // Complete imbalance
  extremeFeatures.timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();

  auto prediction =
      optimizer_->predictOptimalSpread(extremeFeatures, strategyConfig_);
  EXPECT_TRUE(prediction.isValid());
  EXPECT_GT(prediction.optimalSpread, 0.0);

  // Test with zero values
  MarketFeatures zeroFeatures;
  zeroFeatures.timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();

  auto zeroPrediction =
      optimizer_->predictOptimalSpread(zeroFeatures, strategyConfig_);
  EXPECT_TRUE(zeroPrediction.isValid());
  EXPECT_GT(zeroPrediction.optimalSpread, 0.0);
}

TEST_F(MLSpreadOptimizerTest, ThreadSafetyTest) {
  EXPECT_TRUE(optimizer_->initialize());
  addTrainingData(100);
  EXPECT_TRUE(optimizer_->trainModel());

  std::vector<std::thread> threads;
  std::atomic<int> successCount{0};
  std::atomic<int> totalPredictions{0};

  // Launch multiple threads making predictions
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t]() {
      auto features = createTestFeatures(50000.0 + t * 100);

      for (int i = 0; i < 100; ++i) {
        auto prediction =
            optimizer_->predictOptimalSpread(features, strategyConfig_);
        totalPredictions++;

        if (prediction.isValid() && prediction.optimalSpread > 0) {
          successCount++;
        }

        // Also test updates
        optimizer_->updateWithOutcome(features, 5.0 + i * 0.01, 1.0, 0.8,
                                      features.timestamp + i * 1000000);

        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // All predictions should be successful
  EXPECT_EQ(successCount.load(), totalPredictions.load());
}

// Test suite for configuration edge cases
TEST_F(MLSpreadOptimizerTest, ConfigurationValidationTest) {
  // Test with invalid configuration
  MLSpreadOptimizer::Config invalidConfig;
  invalidConfig.maxTrainingDataPoints = 10;
  invalidConfig.minTrainingDataPoints = 20; // Invalid: min > max

  auto invalidOptimizer = std::make_unique<MLSpreadOptimizer>(invalidConfig);
  EXPECT_TRUE(invalidOptimizer->initialize()); // Should handle gracefully

  // Test with minimal configuration
  MLSpreadOptimizer::Config minimalConfig;
  minimalConfig.maxTrainingDataPoints = 100;
  minimalConfig.minTrainingDataPoints = 10;
  minimalConfig.epochs = 1;
  minimalConfig.batchSize = 1;

  auto minimalOptimizer = std::make_unique<MLSpreadOptimizer>(minimalConfig);
  EXPECT_TRUE(minimalOptimizer->initialize());
}
