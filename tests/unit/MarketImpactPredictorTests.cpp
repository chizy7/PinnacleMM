#include "../../core/orderbook/OrderBook.h"
#include "../../core/utils/TimeUtils.h"
#include "../../strategies/analytics/MarketImpactPredictor.h"
#include "../../strategies/analytics/OrderBookAnalyzer.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace pinnacle;
using namespace pinnacle::analytics;
using namespace pinnacle::utils;

class MarketImpactPredictorTest : public ::testing::Test {
protected:
  void SetUp() override {
    symbol_ = "BTC-USD";
    maxHistorySize_ = 1000;
    modelUpdateInterval_ = 60000; // 1 minute

    predictor_ = std::make_unique<MarketImpactPredictor>(
        symbol_, maxHistorySize_, modelUpdateInterval_);

    // Create test order book
    orderBook_ = std::make_shared<OrderBook>(symbol_);

    // Create flow analyzer for enhanced predictions
    flowAnalyzer_ = std::make_shared<OrderBookAnalyzer>(symbol_, 1000, 1000);

    // Initialize predictor
    ASSERT_TRUE(predictor_->initialize(orderBook_, flowAnalyzer_));
    ASSERT_TRUE(predictor_->start());

    // Populate order book with test data
    populateTestOrderBook();
  }

  void TearDown() override {
    if (predictor_) {
      predictor_->stop();
    }
    predictor_.reset();
    flowAnalyzer_.reset();
    orderBook_.reset();
  }

  void populateTestOrderBook() {
    uint64_t timestamp = TimeUtils::getCurrentNanos();

    // Add bid orders
    orderBook_->addOrder(
        std::make_shared<Order>("bid1", symbol_, OrderSide::BUY,
                                OrderType::LIMIT, 50000.0, 1.0, timestamp));
    orderBook_->addOrder(
        std::make_shared<Order>("bid2", symbol_, OrderSide::BUY,
                                OrderType::LIMIT, 49990.0, 2.0, timestamp));
    orderBook_->addOrder(
        std::make_shared<Order>("bid3", symbol_, OrderSide::BUY,
                                OrderType::LIMIT, 49980.0, 3.0, timestamp));

    // Add ask orders
    orderBook_->addOrder(
        std::make_shared<Order>("ask1", symbol_, OrderSide::SELL,
                                OrderType::LIMIT, 50010.0, 1.0, timestamp));
    orderBook_->addOrder(
        std::make_shared<Order>("ask2", symbol_, OrderSide::SELL,
                                OrderType::LIMIT, 50020.0, 2.0, timestamp));
    orderBook_->addOrder(
        std::make_shared<Order>("ask3", symbol_, OrderSide::SELL,
                                OrderType::LIMIT, 50030.0, 3.0, timestamp));
  }

  MarketImpactEvent
  createTestImpactEvent(const std::string& orderId = "test_order",
                        OrderSide side = OrderSide::BUY, double orderSize = 1.0,
                        double priceBefore = 50000.0,
                        double priceAfter = 50005.0) {
    uint64_t timestamp = TimeUtils::getCurrentNanos();
    return MarketImpactEvent(timestamp, orderId, side, orderSize, priceBefore,
                             priceAfter);
  }

  void addTestImpactEvents(size_t count = 10) {
    for (size_t i = 0; i < count; ++i) {
      double orderSize = 0.5 + (i % 5) * 0.2; // Vary order sizes
      double priceBefore = 50000.0 + (i % 3) * 10.0;
      double priceAfter =
          priceBefore + (i % 2 == 0 ? 1.0 : -1.0) * (orderSize * 2.0);

      auto event =
          createTestImpactEvent("order_" + std::to_string(i),
                                i % 2 == 0 ? OrderSide::BUY : OrderSide::SELL,
                                orderSize, priceBefore, priceAfter);

      predictor_->recordImpactEvent(event);
    }
  }

  std::string symbol_;
  size_t maxHistorySize_;
  uint64_t modelUpdateInterval_;
  std::unique_ptr<MarketImpactPredictor> predictor_;
  std::shared_ptr<OrderBook> orderBook_;
  std::shared_ptr<OrderBookAnalyzer> flowAnalyzer_;
};

// Basic functionality tests
TEST_F(MarketImpactPredictorTest, InitializationTest) {
  EXPECT_TRUE(predictor_->isRunning());
  EXPECT_EQ(predictor_->getSymbol(), symbol_);
}

TEST_F(MarketImpactPredictorTest, ImpactEventRecording) {
  auto event = createTestImpactEvent();
  predictor_->recordImpactEvent(event);

  // Verify event was recorded (check statistics)
  auto stats = predictor_->getImpactStatistics();
  EXPECT_FALSE(stats.empty());
}

TEST_F(MarketImpactPredictorTest, BasicImpactPrediction) {
  // Add some historical data
  addTestImpactEvents(20);

  // Test impact prediction
  auto prediction = predictor_->predictImpact(OrderSide::BUY, 1.0, 0.5);

  EXPECT_GE(prediction.predictedImpact, 0.0);
  EXPECT_GE(prediction.confidence, 0.0);
  EXPECT_LE(prediction.confidence, 1.0);
  EXPECT_GT(prediction.timestamp, 0);
}

TEST_F(MarketImpactPredictorTest, ImpactPredictionWithDifferentSizes) {
  addTestImpactEvents(50);

  // Test predictions for different order sizes
  std::vector<double> orderSizes = {0.1, 0.5, 1.0, 2.0, 5.0};

  for (double size : orderSizes) {
    auto buyPrediction = predictor_->predictImpact(OrderSide::BUY, size);
    auto sellPrediction = predictor_->predictImpact(OrderSide::SELL, size);

    EXPECT_GE(buyPrediction.predictedImpact, 0.0);
    EXPECT_GE(sellPrediction.predictedImpact, 0.0);
    EXPECT_GT(buyPrediction.timestamp, 0);
    EXPECT_GT(sellPrediction.timestamp, 0);
  }
}

TEST_F(MarketImpactPredictorTest, ImpactPredictionWithDifferentUrgency) {
  addTestImpactEvents(30);

  // Test predictions with different urgency levels
  std::vector<double> urgencyLevels = {0.0, 0.25, 0.5, 0.75, 1.0};
  double orderSize = 1.0;

  for (double urgency : urgencyLevels) {
    auto prediction =
        predictor_->predictImpact(OrderSide::BUY, orderSize, urgency);

    EXPECT_GE(prediction.predictedImpact, 0.0);
    EXPECT_GE(prediction.confidence, 0.0);
    EXPECT_LE(prediction.confidence, 1.0);
    EXPECT_GE(prediction.urgencyFactor, 0.0);
    EXPECT_LE(prediction.urgencyFactor, 1.0);
  }
}

TEST_F(MarketImpactPredictorTest, OptimalOrderSizing) {
  addTestImpactEvents(40);

  double totalQuantity = 10.0;
  double maxImpact = 0.001;     // 0.1%
  uint64_t timeHorizon = 60000; // 1 minute

  auto recommendation = predictor_->getOptimalSizing(
      OrderSide::BUY, totalQuantity, maxImpact, timeHorizon);

  EXPECT_EQ(recommendation.targetQuantity, totalQuantity);
  EXPECT_FALSE(recommendation.sliceSizes.empty());
  EXPECT_FALSE(recommendation.sliceTiming.empty());
  EXPECT_GE(recommendation.totalExpectedImpact, 0.0);
  EXPECT_GE(recommendation.executionCost, 0.0);
  EXPECT_GT(recommendation.timeToComplete, 0);

  // Verify slices sum to total quantity
  double totalSlices = 0.0;
  for (double slice : recommendation.sliceSizes) {
    EXPECT_GT(slice, 0.0);
    totalSlices += slice;
  }
  EXPECT_NEAR(totalSlices, totalQuantity, 0.001);
}

TEST_F(MarketImpactPredictorTest, ExecutionCostCalculation) {
  addTestImpactEvents(25);

  double quantity = 2.0;
  double currentMidPrice = 50005.0;

  auto buyCost = predictor_->calculateExecutionCost(OrderSide::BUY, quantity,
                                                    currentMidPrice);
  auto sellCost = predictor_->calculateExecutionCost(OrderSide::SELL, quantity,
                                                     currentMidPrice);

  EXPECT_GE(buyCost, currentMidPrice);  // Buy cost should be >= mid price
  EXPECT_LE(sellCost, currentMidPrice); // Sell cost should be <= mid price
}

TEST_F(MarketImpactPredictorTest, HistoricalImpactAnalysis) {
  addTestImpactEvents(60);

  uint64_t lookbackPeriod = 3600000; // 1 hour
  auto analysis = predictor_->analyzeHistoricalImpact(lookbackPeriod);

  EXPECT_GT(analysis.sampleCount, 0);
  EXPECT_GE(analysis.averageImpact, 0.0);
  EXPECT_GE(analysis.medianImpact, 0.0);
  EXPECT_GE(analysis.impactVolatility, 0.0);
  EXPECT_GE(analysis.averageRecoveryTime, 0.0);
  EXPECT_GE(analysis.temporaryImpactRatio, 0.0);
  EXPECT_LE(analysis.temporaryImpactRatio, 1.0);
  EXPECT_EQ(analysis.impactPercentiles.size(), 5); // 5%, 25%, 50%, 75%, 95%
}

TEST_F(MarketImpactPredictorTest, ImpactModelRetrieval) {
  addTestImpactEvents(15);

  auto model = predictor_->getCurrentModel();

  EXPECT_GT(model.alpha, 0.0);
  EXPECT_GT(model.beta, 0.0);
  EXPECT_GT(model.gamma, 0.0);
  EXPECT_GE(model.rsquared, 0.0);
  EXPECT_LE(model.rsquared, 1.0);
  EXPECT_GE(model.meanAbsoluteError, 0.0);
  EXPECT_GE(model.meanSquaredError, 0.0);
}

TEST_F(MarketImpactPredictorTest, ModelRetraining) {
  addTestImpactEvents(100);

  auto modelBefore = predictor_->getCurrentModel();

  // Force model retraining
  EXPECT_TRUE(predictor_->retrainModel());

  auto modelAfter = predictor_->getCurrentModel();

  // Model should have been updated
  EXPECT_GT(modelAfter.lastUpdate, modelBefore.lastUpdate);
}

TEST_F(MarketImpactPredictorTest, MarketRegimeUpdate) {
  double volatility = 0.5;
  double liquidity = 0.8;
  double momentum = 0.3;

  predictor_->updateMarketRegime(volatility, liquidity, momentum);

  auto model = predictor_->getCurrentModel();
  EXPECT_GE(model.volatilityFactor, 0.0);
  EXPECT_GE(model.liquidityFactor, 0.0);
  EXPECT_GE(model.momentumFactor, 0.0);
}

TEST_F(MarketImpactPredictorTest, StatisticsGeneration) {
  addTestImpactEvents(35);

  auto stats = predictor_->getImpactStatistics();
  EXPECT_FALSE(stats.empty());

  // Statistics should contain key information
  EXPECT_NE(stats.find("Impact"), std::string::npos);
  EXPECT_NE(stats.find("Model"), std::string::npos);
}

TEST_F(MarketImpactPredictorTest, ResetFunctionality) {
  addTestImpactEvents(20);

  // Verify data exists
  auto analysis = predictor_->analyzeHistoricalImpact();
  EXPECT_GT(analysis.sampleCount, 0);

  // Reset predictor
  predictor_->reset();

  // Verify data was cleared
  auto analysisAfterReset = predictor_->analyzeHistoricalImpact();
  EXPECT_EQ(analysisAfterReset.sampleCount, 0);
}

// Performance tests
TEST_F(MarketImpactPredictorTest, PredictionLatency) {
  addTestImpactEvents(50);

  const int numPredictions = 1000;
  auto startTime = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < numPredictions; ++i) {
    auto prediction = predictor_->predictImpact(OrderSide::BUY, 1.0);
    EXPECT_GE(prediction.confidence, 0.0);
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime);

  double avgLatencyUs = static_cast<double>(duration.count()) / numPredictions;

  // Should be under 100μs per prediction (target was <100μs)
  EXPECT_LT(avgLatencyUs, 100.0);

  std::cout << "Average prediction latency: " << avgLatencyUs << " μs"
            << std::endl;
}

TEST_F(MarketImpactPredictorTest, ConcurrentPredictions) {
  addTestImpactEvents(30);

  const int numThreads = 4;
  const int predictionsPerThread = 100;
  std::vector<std::thread> threads;
  std::atomic<int> successCount{0};

  for (int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < predictionsPerThread; ++i) {
        auto prediction = predictor_->predictImpact(
            t % 2 == 0 ? OrderSide::BUY : OrderSide::SELL, 1.0 + (i % 3) * 0.5);
        if (prediction.confidence >= 0.0) {
          successCount++;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(successCount.load(), numThreads * predictionsPerThread);
}

// Edge cases and error handling
TEST_F(MarketImpactPredictorTest, InvalidOrderSizes) {
  addTestImpactEvents(10);

  // Test with zero and negative order sizes
  auto prediction1 = predictor_->predictImpact(OrderSide::BUY, 0.0);
  auto prediction2 = predictor_->predictImpact(OrderSide::BUY, -1.0);

  // Should handle gracefully (predictions should be safe)
  EXPECT_GE(prediction1.confidence, 0.0);
  EXPECT_GE(prediction2.confidence, 0.0);
}

TEST_F(MarketImpactPredictorTest, ExtremeUrgencyValues) {
  addTestImpactEvents(15);

  // Test with urgency values outside normal range
  auto prediction1 = predictor_->predictImpact(OrderSide::BUY, 1.0, -0.5);
  auto prediction2 = predictor_->predictImpact(OrderSide::BUY, 1.0, 1.5);

  // Should clamp to valid range
  EXPECT_GE(prediction1.urgencyFactor, 0.0);
  EXPECT_LE(prediction1.urgencyFactor, 1.0);
  EXPECT_GE(prediction2.urgencyFactor, 0.0);
  EXPECT_LE(prediction2.urgencyFactor, 1.0);
}

TEST_F(MarketImpactPredictorTest, EmptyOrderBook) {
  // Create predictor with empty order book
  auto emptyOrderBook = std::make_shared<OrderBook>("TEST");
  auto emptyPredictor =
      std::make_unique<MarketImpactPredictor>("TEST", 1000, 60000);

  ASSERT_TRUE(emptyPredictor->initialize(emptyOrderBook));
  ASSERT_TRUE(emptyPredictor->start());

  // Should handle empty order book gracefully
  auto prediction = emptyPredictor->predictImpact(OrderSide::BUY, 1.0);
  EXPECT_GE(prediction.confidence, 0.0);

  emptyPredictor->stop();
}
