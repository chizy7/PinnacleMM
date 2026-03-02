#include "../../strategies/analytics/CrossMarketCorrelation.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace pinnacle::analytics;

class CrossMarketCorrelationTest : public ::testing::Test {
protected:
  CrossMarketConfig makeConfig() {
    CrossMarketConfig cfg;
    cfg.returnWindowSize = 100;
    cfg.rollingWindowSize = 30;
    cfg.maxLagBars = 5;
    cfg.minCorrelation = 0.3;
    cfg.signalThreshold = 0.01;
    return cfg;
  }
};

TEST_F(CrossMarketCorrelationTest, PerfectlyCorrelatedSeries) {
  auto cfg = makeConfig();
  CrossMarketCorrelation engine(cfg);

  engine.addPair("A", "B");

  // Both series move exactly the same way (sinusoidal prices for varying
  // returns)
  for (int i = 0; i < 50; ++i) {
    double price = 100.0 * std::exp(std::sin(i * 0.3) * 0.05);
    engine.addPriceObservation("A", price, 100.0, i * 1000000ULL);
    engine.addPriceObservation("B", price, 100.0, i * 1000000ULL);
  }

  auto corr = engine.getCorrelation("A", "B");
  EXPECT_NEAR(corr.pearsonCorrelation, 1.0, 0.1);
  EXPECT_NEAR(corr.rollingCorrelation, 1.0, 0.1);
}

TEST_F(CrossMarketCorrelationTest, InverselyCorrelatedSeries) {
  auto cfg = makeConfig();
  CrossMarketCorrelation engine(cfg);

  engine.addPair("A", "B");

  // Use exponential prices from opposite sinusoidal log-returns:
  //   A price = 100 * exp(cumulative sin), B = 100 * exp(-cumulative sin)
  // This ensures log returns are exactly negatively correlated.
  double cumA = 0.0, cumB = 0.0;
  for (int i = 0; i < 50; ++i) {
    double ret = std::sin(i * 0.4) * 0.02; // oscillating return
    cumA += ret;
    cumB -= ret;
    double priceA = 100.0 * std::exp(cumA);
    double priceB = 100.0 * std::exp(cumB);
    engine.addPriceObservation("A", priceA, 100.0, i * 1000000ULL);
    engine.addPriceObservation("B", priceB, 100.0, i * 1000000ULL);
  }

  auto corr = engine.getCorrelation("A", "B");
  EXPECT_LT(corr.pearsonCorrelation, -0.8);
}

TEST_F(CrossMarketCorrelationTest, LeadLagDetection) {
  auto cfg = makeConfig();
  cfg.maxLagBars = 5;
  CrossMarketCorrelation engine(cfg);

  engine.addPair("LEADER", "FOLLOWER");

  // Leader moves first, follower follows 2 bars later
  std::vector<double> leaderPrices;
  for (int i = 0; i < 60; ++i) {
    double val = 100.0 + std::sin(i * 0.3) * 5.0;
    leaderPrices.push_back(val);
  }

  for (int i = 0; i < 60; ++i) {
    engine.addPriceObservation("LEADER", leaderPrices[i], 100.0,
                               i * 1000000ULL);

    // Follower uses leader's price from 2 bars ago
    double followerPrice = (i >= 2) ? leaderPrices[i - 2] : 100.0;
    engine.addPriceObservation("FOLLOWER", followerPrice, 100.0,
                               i * 1000000ULL);
  }

  auto corr = engine.getCorrelation("LEADER", "FOLLOWER");
  // The lead-lag should detect that LEADER leads FOLLOWER
  EXPECT_NE(corr.leadLagBarsA, 0);
  EXPECT_GT(std::abs(corr.leadLagCoefficient), 0.3);
}

TEST_F(CrossMarketCorrelationTest, CointegrationDetection) {
  auto cfg = makeConfig();
  CrossMarketCorrelation engine(cfg);

  engine.addPair("COINT_A", "COINT_B");

  // Cointegrated series: B = 2*A + noise (mean-reverting spread)
  for (int i = 0; i < 100; ++i) {
    double priceA = 100.0 + i * 0.1;
    double noise = (i % 5 == 0) ? 0.3 : -0.1; // Small mean-reverting noise
    double priceB = 200.0 + i * 0.2 + noise;
    engine.addPriceObservation("COINT_A", priceA, 100.0, i * 1000000ULL);
    engine.addPriceObservation("COINT_B", priceB, 100.0, i * 1000000ULL);
  }

  auto corr = engine.getCorrelation("COINT_A", "COINT_B");
  // Cointegration score should be significantly negative
  // The exact value depends on the noise pattern
  EXPECT_NE(corr.cointegrationScore, 0.0);
}

TEST_F(CrossMarketCorrelationTest, UncorrelatedData) {
  auto cfg = makeConfig();
  CrossMarketCorrelation engine(cfg);

  engine.addPair("X", "Y");

  // Use deterministic but uncorrelated patterns
  for (int i = 0; i < 50; ++i) {
    double priceX = 100.0 + std::sin(i * 0.7) * 3.0;
    double priceY = 50.0 + std::cos(i * 1.3) * 2.0;
    engine.addPriceObservation("X", priceX, 100.0, i * 1000000ULL);
    engine.addPriceObservation("Y", priceY, 100.0, i * 1000000ULL);
  }

  auto corr = engine.getCorrelation("X", "Y");
  // Correlation should be weak for these uncorrelated patterns
  EXPECT_LT(std::abs(corr.pearsonCorrelation), 0.8);
}

TEST_F(CrossMarketCorrelationTest, SignalGeneration) {
  auto cfg = makeConfig();
  cfg.signalThreshold = 0.001; // Very low threshold for test
  CrossMarketCorrelation engine(cfg);

  engine.addPair("LEADER", "FOLLOWER");

  // Create strong lead-lag pattern
  std::vector<double> leaderPrices;
  for (int i = 0; i < 60; ++i) {
    leaderPrices.push_back(100.0 + std::sin(i * 0.3) * 5.0);
  }

  for (int i = 0; i < 60; ++i) {
    engine.addPriceObservation("LEADER", leaderPrices[i], 100.0,
                               i * 1000000ULL);
    double followerPrice = (i >= 2) ? leaderPrices[i - 2] : 100.0;
    engine.addPriceObservation("FOLLOWER", followerPrice, 100.0,
                               i * 1000000ULL);
  }

  auto signals = engine.getActiveSignals();
  // Strong lead-lag pattern with very low threshold should generate signals
  ASSERT_FALSE(signals.empty());
  EXPECT_FALSE(signals[0].leadSymbol.empty());
  EXPECT_FALSE(signals[0].lagSymbol.empty());
  EXPECT_GT(signals[0].signalStrength, 0.0);
}

TEST_F(CrossMarketCorrelationTest, Statistics) {
  auto cfg = makeConfig();
  CrossMarketCorrelation engine(cfg);

  engine.addPair("A", "B");
  for (int i = 0; i < 20; ++i) {
    engine.addPriceObservation("A", 100.0 + i, 100.0, i * 1000000ULL);
    engine.addPriceObservation("B", 200.0 + i, 100.0, i * 1000000ULL);
  }

  std::string stats = engine.getStatistics();
  EXPECT_FALSE(stats.empty());
  EXPECT_NE(stats.find("CrossMarketCorrelation"), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
