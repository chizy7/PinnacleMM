#include "../../core/utils/TimeUtils.h"
#include "../../strategies/arbitrage/ArbitrageDetector.h"
#include "../../strategies/arbitrage/ArbitrageExecutor.h"

#include <gtest/gtest.h>
#include <thread>

using namespace pinnacle;
using namespace pinnacle::arbitrage;

class ArbitrageDetectorTest : public ::testing::Test {
protected:
  static ArbitrageConfig makeConfig() {
    ArbitrageConfig cfg;
    cfg.minSpreadBps = 5.0;
    cfg.minProfitUsd = 0.01;
    cfg.maxStalenessNs = 5000000000ULL; // 5 seconds for tests
    cfg.scanIntervalMs = 50;
    cfg.dryRun = true;
    cfg.symbols = {"BTC-USD"};
    cfg.venues = {"coinbase", "kraken"};
    cfg.venueFees = {{"coinbase", 0.001}, {"kraken", 0.0015}};
    return cfg;
  }
};

TEST_F(ArbitrageDetectorTest, DetectsOpportunity) {
  auto cfg = makeConfig();
  ArbitrageDetector detector(cfg);

  uint64_t now = utils::TimeUtils::getCurrentNanos();

  // Coinbase: ask = 100.00 (buy here)
  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            now);

  // Kraken: bid = 100.50 (sell here) — clear arbitrage after fees
  detector.updateVenueQuote("kraken", "BTC-USD", 100.50, 1.0, 100.60, 1.0, now);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto opps = detector.getCurrentOpportunities();
  EXPECT_GE(opps.size(), 1u);

  if (!opps.empty()) {
    EXPECT_EQ(opps[0].symbol, "BTC-USD");
    EXPECT_GT(opps[0].spreadBps, 0.0);
    EXPECT_GT(opps[0].estimatedProfit, 0.0);
  }

  detector.stop();
}

TEST_F(ArbitrageDetectorTest, FeeAdjustment) {
  ArbitrageConfig cfg = makeConfig();
  cfg.venueFees = {{"coinbase", 0.05}, {"kraken", 0.05}}; // 5% fee each!
  ArbitrageDetector detector(cfg);

  uint64_t now = utils::TimeUtils::getCurrentNanos();

  // Small spread that gets eaten by fees
  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            now);
  detector.updateVenueQuote("kraken", "BTC-USD", 100.10, 1.0, 100.20, 1.0, now);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto opps = detector.getCurrentOpportunities();
  // Fees should eliminate this small spread opportunity
  EXPECT_EQ(opps.size(), 0u);

  detector.stop();
}

TEST_F(ArbitrageDetectorTest, StalenessFilter) {
  auto cfg = makeConfig();
  cfg.maxStalenessNs = 1; // 1 nanosecond — effectively all quotes are stale
  ArbitrageDetector detector(cfg);

  uint64_t old = utils::TimeUtils::getCurrentNanos() - 1000000000ULL;

  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            old);
  detector.updateVenueQuote("kraken", "BTC-USD", 100.50, 1.0, 100.60, 1.0, old);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto opps = detector.getCurrentOpportunities();
  EXPECT_EQ(opps.size(), 0u); // All stale

  detector.stop();
}

TEST_F(ArbitrageDetectorTest, MinSpreadThreshold) {
  ArbitrageConfig cfg = makeConfig();
  cfg.minSpreadBps = 1000.0; // Very high threshold
  ArbitrageDetector detector(cfg);

  uint64_t now = utils::TimeUtils::getCurrentNanos();

  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            now);
  detector.updateVenueQuote("kraken", "BTC-USD", 100.10, 1.0, 100.20, 1.0, now);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto opps = detector.getCurrentOpportunities();
  EXPECT_EQ(opps.size(), 0u); // Below threshold

  detector.stop();
}

TEST_F(ArbitrageDetectorTest, DryRunExecution) {
  ArbitrageExecutor executor(true); // dry run

  ArbitrageOpportunity opp;
  opp.symbol = "BTC-USD";
  opp.buyVenue = "coinbase";
  opp.sellVenue = "kraken";
  opp.buyPrice = 100.00;
  opp.sellPrice = 100.50;
  opp.spread = 0.50;
  opp.spreadBps = 50.0;
  opp.maxQuantity = 1.0;
  opp.estimatedProfit = 0.50;

  auto result = executor.execute(opp);
  EXPECT_TRUE(result.buyFilled);
  EXPECT_TRUE(result.sellFilled);
  EXPECT_NEAR(result.realizedProfit, 0.50, 0.01);
  EXPECT_GT(result.executionTimeNs, 0u);
}

TEST_F(ArbitrageDetectorTest, OpportunityCallback) {
  auto cfg = makeConfig();
  ArbitrageDetector detector(cfg);

  std::atomic<int> callbackCount{0};
  detector.setOpportunityCallback(
      [&callbackCount](const ArbitrageOpportunity& /*opp*/) {
        callbackCount.fetch_add(1, std::memory_order_relaxed);
      });

  uint64_t now = utils::TimeUtils::getCurrentNanos();

  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            now);
  detector.updateVenueQuote("kraken", "BTC-USD", 100.50, 1.0, 100.60, 1.0, now);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  detector.stop();

  EXPECT_GT(callbackCount.load(), 0);
}

TEST_F(ArbitrageDetectorTest, Statistics) {
  auto cfg = makeConfig();
  ArbitrageDetector detector(cfg);

  std::string stats = detector.getStatistics();
  EXPECT_FALSE(stats.empty());
  EXPECT_NE(stats.find("ArbitrageDetector"), std::string::npos);
}

TEST_F(ArbitrageDetectorTest, NoOpportunityWhenSameVenue) {
  ArbitrageConfig cfg = makeConfig();
  cfg.venues = {"coinbase"}; // Only one venue
  ArbitrageDetector detector(cfg);

  uint64_t now = utils::TimeUtils::getCurrentNanos();
  detector.updateVenueQuote("coinbase", "BTC-USD", 99.90, 1.0, 100.00, 1.0,
                            now);

  detector.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto opps = detector.getCurrentOpportunities();
  EXPECT_EQ(opps.size(), 0u); // Can't arb against yourself

  detector.stop();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
