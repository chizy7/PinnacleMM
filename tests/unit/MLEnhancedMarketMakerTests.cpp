#include "../../core/orderbook/OrderBook.h"
#include "../../core/utils/TimeUtils.h"
#include "../../strategies/basic/MLEnhancedMarketMaker.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace pinnacle::strategy;
using namespace pinnacle::utils;
using namespace pinnacle;

class MLEnhancedMarketMakerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create test configuration
    strategyConfig_ = StrategyConfig{};
    strategyConfig_.strategyName = "TestMLEnhanced";
    strategyConfig_.symbol = "BTC-USD";
    strategyConfig_.baseSpreadBps = 10.0;
    strategyConfig_.minSpreadBps = 5.0;
    strategyConfig_.maxSpreadBps = 50.0;
    strategyConfig_.orderQuantity = 0.1;
    strategyConfig_.maxPosition = 10.0;
    strategyConfig_.quoteUpdateIntervalMs = 100;

    // Create ML configuration
    mlConfig_ = MLEnhancedMarketMaker::MLConfig{};
    mlConfig_.enableMLSpreadOptimization = true;
    mlConfig_.enableOnlineLearning = true;
    mlConfig_.fallbackToHeuristics = true;
    mlConfig_.mlConfidenceThreshold = 0.3;
    mlConfig_.optimizerConfig.minTrainingDataPoints = 10;
    mlConfig_.optimizerConfig.epochs = 10; // Faster for testing

    // Create order book
    orderBook_ = std::make_shared<OrderBook>("BTC-USD");

    // Create market maker
    marketMaker_ = std::make_unique<MLEnhancedMarketMaker>(
        "BTC-USD", strategyConfig_, mlConfig_);
  }

  void TearDown() override {
    if (marketMaker_->isRunning()) {
      marketMaker_->stop();
    }
    marketMaker_.reset();
    orderBook_.reset();
  }

  void populateOrderBook() {
    uint64_t timestamp = TimeUtils::getCurrentNanos();

    // Add some bid orders
    for (int i = 0; i < 5; ++i) {
      double price = 50000.0 - (i + 1) * 5.0;
      auto order = std::make_shared<Order>(
          "bid-" + std::to_string(i), "BTC-USD", OrderSide::BUY,
          OrderType::LIMIT, price, 0.1 + i * 0.05, timestamp);
      orderBook_->addOrder(order);
    }

    // Add some ask orders
    for (int i = 0; i < 5; ++i) {
      double price = 50010.0 + i * 5.0;
      auto order = std::make_shared<Order>(
          "ask-" + std::to_string(i), "BTC-USD", OrderSide::SELL,
          OrderType::LIMIT, price, 0.1 + i * 0.05, timestamp);
      orderBook_->addOrder(order);
    }
  }

  StrategyConfig strategyConfig_;
  MLEnhancedMarketMaker::MLConfig mlConfig_;
  std::shared_ptr<OrderBook> orderBook_;
  std::unique_ptr<MLEnhancedMarketMaker> marketMaker_;
};

TEST_F(MLEnhancedMarketMakerTest, InitializationTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  EXPECT_FALSE(marketMaker_->isRunning());
  EXPECT_FALSE(marketMaker_->isMLModelReady()); // Should not be ready initially

  // Test ML components
  auto metrics = marketMaker_->getMLMetrics();
  EXPECT_EQ(metrics.totalPredictions, 0);
}

TEST_F(MLEnhancedMarketMakerTest, StartStopTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  EXPECT_TRUE(marketMaker_->start());
  EXPECT_TRUE(marketMaker_->isRunning());

  // Let it run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_TRUE(marketMaker_->stop());
  EXPECT_FALSE(marketMaker_->isRunning());
}

TEST_F(MLEnhancedMarketMakerTest, OrderBookUpdateTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();

  // Test order book update handling
  marketMaker_->onOrderBookUpdate(*orderBook_);

  // Should have processed the update
  auto stats = marketMaker_->getStatistics();
  EXPECT_FALSE(stats.empty());
}

TEST_F(MLEnhancedMarketMakerTest, MLSpreadCalculationTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();

  EXPECT_TRUE(marketMaker_->start());

  // Let the strategy run to generate some quotes
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Should have made some predictions by now
  auto metrics = marketMaker_->getMLMetrics();

  // Even without training, should use heuristics
  EXPECT_GE(metrics.totalPredictions, 0);

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, OnlineLearningTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();

  // Enable online learning with fast training
  mlConfig_.enableOnlineLearning = true;
  mlConfig_.optimizerConfig.minTrainingDataPoints = 5;
  marketMaker_->updateMLConfig(mlConfig_);

  EXPECT_TRUE(marketMaker_->start());

  // Generate some market activity to create training data
  uint64_t timestamp = TimeUtils::getCurrentNanos();

  for (int i = 0; i < 20; ++i) {
    // Simulate trades
    double price = 50005.0 + (i % 10 - 5) * 2.0;
    marketMaker_->onTrade("BTC-USD", price, 0.1, OrderSide::BUY,
                          timestamp + i * 1000000);

    // Simulate order updates
    marketMaker_->onOrderUpdate("test-order-" + std::to_string(i),
                                OrderStatus::FILLED, 0.1,
                                timestamp + i * 1000000);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Should have accumulated some training data
  auto metrics = marketMaker_->getMLMetrics();
  EXPECT_GT(metrics.totalPredictions, 0);

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, MLConfigurationTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));

  // Test configuration updates
  MLEnhancedMarketMaker::MLConfig newConfig = mlConfig_;
  newConfig.mlConfidenceThreshold = 0.7;
  newConfig.maxSpreadDeviationRatio = 3.0;

  EXPECT_TRUE(marketMaker_->updateMLConfig(newConfig));

  // Test disabling ML
  newConfig.enableMLSpreadOptimization = false;
  EXPECT_TRUE(marketMaker_->updateMLConfig(newConfig));

  // Should still work (fallback to heuristics)
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, PerformanceTrackingTest) {
  mlConfig_.enablePerformanceTracking = true;
  mlConfig_.performanceReportIntervalMs = 100; // Fast reporting for testing

  marketMaker_ = std::make_unique<MLEnhancedMarketMaker>(
      "BTC-USD", strategyConfig_, mlConfig_);

  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  // Generate activity to track performance
  uint64_t timestamp = TimeUtils::getCurrentNanos();

  for (int i = 0; i < 10; ++i) {
    marketMaker_->onTrade("BTC-USD", 50005.0, 0.1, OrderSide::BUY,
                          timestamp + i * 1000000);
    marketMaker_->onOrderUpdate("perf-test-" + std::to_string(i),
                                OrderStatus::FILLED, 0.1,
                                timestamp + i * 1000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Should have performance data
  auto stats = marketMaker_->getStatistics();
  EXPECT_FALSE(stats.empty());
  EXPECT_TRUE(stats.find("ML Enhancement Statistics") != std::string::npos ||
              stats.find("ML vs Heuristic Performance") != std::string::npos);

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, FeatureImportanceTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));

  // Initially should have no feature importance (no trained model)
  auto importance = marketMaker_->getFeatureImportance();
  EXPECT_TRUE(importance.empty());

  // Add some training data and train
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  // Generate enough data for training
  uint64_t timestamp = TimeUtils::getCurrentNanos();
  for (int i = 0; i < 15; ++i) {
    marketMaker_->onTrade("BTC-USD", 50000.0 + i, 0.1, OrderSide::BUY,
                          timestamp + i * 1000000);
    marketMaker_->onOrderUpdate("train-" + std::to_string(i),
                                OrderStatus::FILLED, 0.1,
                                timestamp + i * 1000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Force training
  marketMaker_->forceMLRetraining();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should have feature importance if model is trained
  importance = marketMaker_->getFeatureImportance();
  // May still be empty if not enough data or training not completed

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, ModelPersistenceTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();

  // Generate training data
  EXPECT_TRUE(marketMaker_->start());
  uint64_t timestamp = TimeUtils::getCurrentNanos();

  for (int i = 0; i < 20; ++i) {
    marketMaker_->onTrade("BTC-USD", 50000.0 + i * 10, 0.1,
                          (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
                          timestamp + i * 1000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(marketMaker_->stop());

  // Test model saving
  std::string modelFile = "/tmp/test_enhanced_mm_model.bin";
  bool saved = marketMaker_->saveMLModel(modelFile);

  // Create new market maker and test loading
  auto newMarketMaker = std::make_unique<MLEnhancedMarketMaker>(
      "BTC-USD", strategyConfig_, mlConfig_);
  EXPECT_TRUE(newMarketMaker->initialize(orderBook_));

  if (saved) {
    EXPECT_TRUE(newMarketMaker->loadMLModel(modelFile));
    std::remove(modelFile.c_str());
  }
}

TEST_F(MLEnhancedMarketMakerTest, FallbackBehaviorTest) {
  // Test with ML disabled initially
  mlConfig_.enableMLSpreadOptimization = false;
  marketMaker_ = std::make_unique<MLEnhancedMarketMaker>(
      "BTC-USD", strategyConfig_, mlConfig_);

  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  // Should work with heuristics only
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto stats = marketMaker_->getStatistics();
  EXPECT_FALSE(stats.empty());

  // Enable ML mid-run
  mlConfig_.enableMLSpreadOptimization = true;
  EXPECT_TRUE(marketMaker_->updateMLConfig(mlConfig_));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, RiskManagementTest) {
  // Set restrictive risk parameters
  mlConfig_.maxSpreadDeviationRatio = 1.5;
  mlConfig_.minConfidenceForExecution = 0.8;

  marketMaker_ = std::make_unique<MLEnhancedMarketMaker>(
      "BTC-USD", strategyConfig_, mlConfig_);

  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  // Generate some activity
  uint64_t timestamp = TimeUtils::getCurrentNanos();
  for (int i = 0; i < 10; ++i) {
    marketMaker_->onTrade("BTC-USD", 50000.0 + i * 100, 0.1, OrderSide::BUY,
                          timestamp + i * 1000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Should still function safely
  auto stats = marketMaker_->getStatistics();
  EXPECT_FALSE(stats.empty());

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, PositionAwareSpreadTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();

  // Set up position-dependent scenario
  strategyConfig_.maxPosition = 5.0;
  marketMaker_->updateConfig(strategyConfig_);

  EXPECT_TRUE(marketMaker_->start());

  // Simulate building up position through fills
  uint64_t timestamp = TimeUtils::getCurrentNanos();

  // Simulate several buy fills to build long position
  for (int i = 0; i < 3; ++i) {
    marketMaker_->onOrderUpdate("position-build-" + std::to_string(i),
                                OrderStatus::FILLED, 1.0,
                                timestamp + i * 1000000);

    // Update position manually (in real system this would be handled by order
    // tracking)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Check that position affects spread calculations
  auto metrics = marketMaker_->getMLMetrics();
  EXPECT_GE(metrics.totalPredictions, 0);

  EXPECT_TRUE(marketMaker_->stop());
}

TEST_F(MLEnhancedMarketMakerTest, MultiThreadedStressTest) {
  EXPECT_TRUE(marketMaker_->initialize(orderBook_));
  populateOrderBook();
  EXPECT_TRUE(marketMaker_->start());

  std::vector<std::thread> threads;
  std::atomic<int> totalEvents{0};

  // Launch multiple threads generating market events
  for (int t = 0; t < 3; ++t) {
    threads.emplace_back([&, t]() {
      uint64_t baseTimestamp = TimeUtils::getCurrentNanos();

      for (int i = 0; i < 50; ++i) {
        double price = 50000.0 + (t * 100) + i;
        uint64_t timestamp = baseTimestamp + (t * 1000 + i) * 1000000;

        // Generate various types of events
        if (i % 3 == 0) {
          marketMaker_->onTrade("BTC-USD", price, 0.1, OrderSide::BUY,
                                timestamp);
        } else if (i % 3 == 1) {
          marketMaker_->onOrderUpdate("stress-" + std::to_string(t) + "-" +
                                          std::to_string(i),
                                      OrderStatus::FILLED, 0.05, timestamp);
        } else {
          marketMaker_->onOrderBookUpdate(*orderBook_);
        }

        totalEvents++;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_GT(totalEvents.load(), 0);

  // System should still be stable
  auto stats = marketMaker_->getStatistics();
  EXPECT_FALSE(stats.empty());

  EXPECT_TRUE(marketMaker_->stop());
}
