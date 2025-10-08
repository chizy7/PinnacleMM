#include "../../core/utils/TimeUtils.h"
#include "../../strategies/analytics/MarketRegimeDetector.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace pinnacle::analytics;
using namespace pinnacle::utils;

class MarketRegimeDetectorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create test configuration
    config_.shortWindow = 10;
    config_.mediumWindow = 20;
    config_.longWindow = 50;
    config_.minDataPoints = 10;
    config_.updateIntervalMs = 100;

    detector_ = std::make_unique<MarketRegimeDetector>(config_);
  }

  void TearDown() override { detector_.reset(); }

  MarketDataPoint createTestDataPoint(double price, double volume = 100.0) {
    MarketDataPoint dataPoint;
    dataPoint.price = price;
    dataPoint.volume = volume;
    dataPoint.bid = price - 0.1;
    dataPoint.ask = price + 0.1;
    dataPoint.spread = dataPoint.ask - dataPoint.bid;
    dataPoint.timestamp = TimeUtils::getCurrentNanos();
    return dataPoint;
  }

  void feedTrendingUpData() {
    // Feed increasing price data to simulate trending up
    for (int i = 0; i < 30; ++i) {
      double price = 100.0 + i * 0.5; // Increasing trend
      auto dataPoint = createTestDataPoint(price);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void feedTrendingDownData() {
    // Feed decreasing price data to simulate trending down
    for (int i = 0; i < 30; ++i) {
      double price = 100.0 - i * 0.5; // Decreasing trend
      auto dataPoint = createTestDataPoint(price);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void feedHighVolatilityData() {
    // Feed volatile price data
    for (int i = 0; i < 30; ++i) {
      double price = 100.0 + ((i % 2 == 0) ? 5.0 : -5.0); // High volatility
      auto dataPoint = createTestDataPoint(price);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void feedLowVolatilityData() {
    // Feed stable price data
    for (int i = 0; i < 30; ++i) {
      double price = 100.0 + (i % 3 - 1) * 0.01; // Very low volatility
      auto dataPoint = createTestDataPoint(price);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void feedMeanRevertingData() {
    // Feed oscillating price data around a mean
    for (int i = 0; i < 40; ++i) {
      double price = 100.0 + 2.0 * sin(i * 0.5); // Sine wave around 100
      auto dataPoint = createTestDataPoint(price);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  RegimeConfiguration config_;
  std::unique_ptr<MarketRegimeDetector> detector_;
};

TEST_F(MarketRegimeDetectorTest, InitializationTest) {
  EXPECT_TRUE(detector_->initialize());
  EXPECT_EQ(MarketRegime::UNKNOWN, detector_->getCurrentRegime());
  EXPECT_EQ(0.0, detector_->getRegimeConfidence());
}

TEST_F(MarketRegimeDetectorTest, BasicDataIngestionTest) {
  EXPECT_TRUE(detector_->initialize());

  auto dataPoint = createTestDataPoint(100.0);
  detector_->updateMarketData(dataPoint);

  // Should not crash
  auto metrics = detector_->getCurrentMetrics();
  EXPECT_EQ(dataPoint.timestamp, metrics.timestamp);
}

TEST_F(MarketRegimeDetectorTest, TrendingUpDetectionTest) {
  EXPECT_TRUE(detector_->initialize());

  feedTrendingUpData();

  auto regime = detector_->getCurrentRegime();
  auto metrics = detector_->getCurrentMetrics();

  // Should detect upward trend
  EXPECT_TRUE(regime == MarketRegime::TRENDING_UP ||
              regime == MarketRegime::HIGH_VOLATILITY);
  EXPECT_GT(metrics.trendStrength, 0.0); // Positive trend strength
  EXPECT_GT(detector_->getRegimeConfidence(), 0.0);
}

TEST_F(MarketRegimeDetectorTest, TrendingDownDetectionTest) {
  EXPECT_TRUE(detector_->initialize());

  feedTrendingDownData();

  auto regime = detector_->getCurrentRegime();
  auto metrics = detector_->getCurrentMetrics();

  // Should detect downward trend
  EXPECT_TRUE(regime == MarketRegime::TRENDING_DOWN ||
              regime == MarketRegime::HIGH_VOLATILITY);
  EXPECT_LT(metrics.trendStrength, 0.0); // Negative trend strength
  EXPECT_GT(detector_->getRegimeConfidence(), 0.0);
}

TEST_F(MarketRegimeDetectorTest, HighVolatilityDetectionTest) {
  EXPECT_TRUE(detector_->initialize());

  feedHighVolatilityData();

  auto regime = detector_->getCurrentRegime();
  auto metrics = detector_->getCurrentMetrics();

  // Should detect high volatility
  EXPECT_TRUE(regime == MarketRegime::HIGH_VOLATILITY ||
              regime == MarketRegime::CRISIS);
  EXPECT_GT(metrics.volatility, config_.lowVolatilityThreshold);
  EXPECT_GT(detector_->getRegimeConfidence(), 0.0);
}

TEST_F(MarketRegimeDetectorTest, LowVolatilityDetectionTest) {
  EXPECT_TRUE(detector_->initialize());

  feedLowVolatilityData();

  auto regime = detector_->getCurrentRegime();
  auto metrics = detector_->getCurrentMetrics();

  // Should detect low volatility or consolidation
  EXPECT_TRUE(regime == MarketRegime::LOW_VOLATILITY ||
              regime == MarketRegime::CONSOLIDATION ||
              regime == MarketRegime::UNKNOWN);
  EXPECT_LT(metrics.volatility, config_.highVolatilityThreshold);
}

TEST_F(MarketRegimeDetectorTest, MeanReversionDetectionTest) {
  EXPECT_TRUE(detector_->initialize());

  feedMeanRevertingData();

  auto regime = detector_->getCurrentRegime();
  auto metrics = detector_->getCurrentMetrics();

  // Should have some mean reversion characteristics
  EXPECT_GE(metrics.meanReversion, 0.0);
  EXPECT_LE(metrics.meanReversion, 1.0);
  EXPECT_GT(detector_->getRegimeConfidence(), 0.0);
}

TEST_F(MarketRegimeDetectorTest, RegimeTransitionTest) {
  EXPECT_TRUE(detector_->initialize());

  // Start with trending up
  feedTrendingUpData();
  auto initialRegime = detector_->getCurrentRegime();

  // Switch to trending down
  feedTrendingDownData();
  auto finalRegime = detector_->getCurrentRegime();

  // Should have recorded transitions
  auto transitions = detector_->getRecentTransitions(5);
  EXPECT_FALSE(transitions.empty());

  // Regimes should be different (unless both are unknown)
  if (initialRegime != MarketRegime::UNKNOWN &&
      finalRegime != MarketRegime::UNKNOWN) {
    EXPECT_NE(initialRegime, finalRegime);
  }
}

TEST_F(MarketRegimeDetectorTest, MetricsCalculationTest) {
  EXPECT_TRUE(detector_->initialize());

  feedTrendingUpData();

  auto metrics = detector_->getCurrentMetrics();

  // All metrics should be within reasonable bounds
  EXPECT_GE(metrics.trendStrength, -1.0);
  EXPECT_LE(metrics.trendStrength, 1.0);
  EXPECT_GE(metrics.volatility, 0.0);
  EXPECT_GE(metrics.meanReversion, 0.0);
  EXPECT_LE(metrics.meanReversion, 1.0);
  EXPECT_GE(metrics.stress, 0.0);
  EXPECT_LE(metrics.stress, 1.0);
  EXPECT_GE(metrics.autocorrelation, -1.0);
  EXPECT_LE(metrics.autocorrelation, 1.0);
  EXPECT_GT(metrics.varianceRatio, 0.0);
}

TEST_F(MarketRegimeDetectorTest, ConfigurationUpdateTest) {
  EXPECT_TRUE(detector_->initialize());

  RegimeConfiguration newConfig = config_;
  newConfig.highVolatilityThreshold = 0.1;
  newConfig.trendStrengthThreshold = 0.5;

  detector_->updateConfiguration(newConfig);
  auto updatedConfig = detector_->getConfiguration();

  EXPECT_EQ(0.1, updatedConfig.highVolatilityThreshold);
  EXPECT_EQ(0.5, updatedConfig.trendStrengthThreshold);
}

TEST_F(MarketRegimeDetectorTest, StatisticsTest) {
  EXPECT_TRUE(detector_->initialize());

  feedTrendingUpData();

  std::string stats = detector_->getRegimeStatistics();
  EXPECT_FALSE(stats.empty());
  EXPECT_TRUE(stats.find("Current Regime") != std::string::npos);
  EXPECT_TRUE(stats.find("Confidence") != std::string::npos);
}

TEST_F(MarketRegimeDetectorTest, EdgeCasesTest) {
  EXPECT_TRUE(detector_->initialize());

  // Test with zero price
  auto zeroDataPoint = createTestDataPoint(0.0);
  detector_->updateMarketData(zeroDataPoint);

  // Should handle gracefully
  auto regime = detector_->getCurrentRegime();
  (void)regime;      // Suppress unused warning
  EXPECT_TRUE(true); // Just verify it doesn't crash

  // Test with very large price
  auto largeDataPoint = createTestDataPoint(1e9);
  detector_->updateMarketData(largeDataPoint);

  // Should not crash
  auto metrics = detector_->getCurrentMetrics();
  EXPECT_TRUE(std::isfinite(metrics.trendStrength));
  EXPECT_TRUE(std::isfinite(metrics.volatility));
}

TEST_F(MarketRegimeDetectorTest, ConcurrentOperationsTest) {
  EXPECT_TRUE(detector_->initialize());

  std::atomic<bool> stopFlag{false};
  std::vector<std::thread> threads;

  // Thread 1: Feed data
  threads.emplace_back([&]() {
    int price = 100;
    while (!stopFlag.load()) {
      auto dataPoint = createTestDataPoint(price++);
      detector_->updateMarketData(dataPoint);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });

  // Thread 2: Query regime
  threads.emplace_back([&]() {
    while (!stopFlag.load()) {
      auto regime = detector_->getCurrentRegime();
      auto confidence = detector_->getRegimeConfidence();
      (void)regime;
      (void)confidence; // Suppress unused warnings
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  // Thread 3: Get statistics
  threads.emplace_back([&]() {
    while (!stopFlag.load()) {
      auto stats = detector_->getRegimeStatistics();
      auto metrics = detector_->getCurrentMetrics();
      (void)stats;
      (void)metrics; // Suppress unused warnings
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  });

  // Let threads run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stopFlag.store(true);

  // Join all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // System should still be operational
  auto finalStats = detector_->getRegimeStatistics();
  EXPECT_FALSE(finalStats.empty());
}

TEST_F(MarketRegimeDetectorTest, UtilityFunctionsTest) {
  // Test regime to string conversion
  EXPECT_EQ("TRENDING_UP", regimeToString(MarketRegime::TRENDING_UP));
  EXPECT_EQ("TRENDING_DOWN", regimeToString(MarketRegime::TRENDING_DOWN));
  EXPECT_EQ("HIGH_VOLATILITY", regimeToString(MarketRegime::HIGH_VOLATILITY));
  EXPECT_EQ("LOW_VOLATILITY", regimeToString(MarketRegime::LOW_VOLATILITY));
  EXPECT_EQ("MEAN_REVERTING", regimeToString(MarketRegime::MEAN_REVERTING));
  EXPECT_EQ("CRISIS", regimeToString(MarketRegime::CRISIS));
  EXPECT_EQ("CONSOLIDATION", regimeToString(MarketRegime::CONSOLIDATION));
  EXPECT_EQ("UNKNOWN", regimeToString(MarketRegime::UNKNOWN));

  // Test string to regime conversion
  EXPECT_EQ(MarketRegime::TRENDING_UP, stringToRegime("TRENDING_UP"));
  EXPECT_EQ(MarketRegime::TRENDING_DOWN, stringToRegime("TRENDING_DOWN"));
  EXPECT_EQ(MarketRegime::HIGH_VOLATILITY, stringToRegime("HIGH_VOLATILITY"));
  EXPECT_EQ(MarketRegime::LOW_VOLATILITY, stringToRegime("LOW_VOLATILITY"));
  EXPECT_EQ(MarketRegime::MEAN_REVERTING, stringToRegime("MEAN_REVERTING"));
  EXPECT_EQ(MarketRegime::CRISIS, stringToRegime("CRISIS"));
  EXPECT_EQ(MarketRegime::CONSOLIDATION, stringToRegime("CONSOLIDATION"));
  EXPECT_EQ(MarketRegime::UNKNOWN, stringToRegime("UNKNOWN"));
  EXPECT_EQ(MarketRegime::UNKNOWN, stringToRegime("INVALID"));

  // Test regime metrics to string
  RegimeMetrics metrics;
  metrics.trendStrength = 0.5;
  metrics.volatility = 0.02;
  metrics.meanReversion = 0.7;
  metrics.momentum = 0.1;
  metrics.stress = 0.3;

  std::string metricsStr = regimeMetricsToString(metrics);
  EXPECT_FALSE(metricsStr.empty());
  EXPECT_TRUE(metricsStr.find("Trend") != std::string::npos);
  EXPECT_TRUE(metricsStr.find("Vol") != std::string::npos);
}
