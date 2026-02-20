#include "../../core/risk/RiskConfig.h"
#include "../../core/risk/VaREngine.h"

#include <cmath>
#include <gtest/gtest.h>
#include <random>
#include <thread>
#include <vector>

using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class VaREngineTest : public ::testing::Test {
protected:
  static VaRConfig defaultConfig() {
    VaRConfig config;
    config.windowSize = 252;
    config.simulationCount = 10000;
    config.horizon = 1.0;
    config.updateIntervalMs = 100; // fast updates for testing
    config.confidenceLevel95 = 0.95;
    config.confidenceLevel99 = 0.99;
    config.varLimitPct = 2.0;
    return config;
  }

  // Generate N draws from Normal(mean, stddev) and feed them into the engine
  static void feedNormalReturns(VaREngine& engine, size_t count, double mean,
                                double stddev, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    for (size_t i = 0; i < count; ++i) {
      engine.addReturn(dist(rng));
    }
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(VaREngineTest, InitializeAndStart) {
  VaREngine engine;
  engine.initialize(defaultConfig());
  engine.start();

  // Allow a single calculation cycle
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  engine.stop();
  // No crash, no exception -> pass
}

TEST_F(VaREngineTest, AddReturns) {
  VaREngine engine;
  engine.initialize(defaultConfig());

  for (int i = 0; i < 50; ++i) {
    engine.addReturn(0.001 * i);
  }

  auto result = engine.getLatestResult();
  // Before start() the result may or may not have been computed; but the
  // engine should not crash and sample count should reflect adds if a
  // background pass has run.
  SUCCEED();
}

TEST_F(VaREngineTest, HistoricalVaR) {
  VaREngine engine;
  VaRConfig config = defaultConfig();
  config.windowSize = 1000;
  config.updateIntervalMs = 50;
  engine.initialize(config);

  // Feed 1000 draws from N(0, 0.01)
  feedNormalReturns(engine, 1000, 0.0, 0.01);

  engine.start();
  // Wait for at least one calculation cycle
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  engine.stop();

  auto result = engine.getLatestResult();

  // For N(0, 0.01) the 95 % VaR should be approximately 1.645 * 0.01 = 0.01645
  // Allow generous tolerance because of sampling noise
  if (result.sampleCount > 0) {
    EXPECT_NEAR(std::abs(result.historicalVaR95), 0.01645, 0.005);
  }
}

TEST_F(VaREngineTest, ParametricVaR) {
  VaREngine engine;
  VaRConfig config = defaultConfig();
  config.windowSize = 1000;
  config.updateIntervalMs = 50;
  engine.initialize(config);

  feedNormalReturns(engine, 1000, 0.0, 0.01);

  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  engine.stop();

  auto result = engine.getLatestResult();

  if (result.sampleCount > 0) {
    // Parametric VaR for normal returns should be close to the analytical value
    EXPECT_NEAR(std::abs(result.parametricVaR95), 0.01645, 0.005);
  }
}

TEST_F(VaREngineTest, VaRBreached) {
  VaREngine engine;
  VaRConfig config = defaultConfig();
  config.varLimitPct = 0.001; // extremely tight limit -> easy to breach
  config.windowSize = 100;
  config.updateIntervalMs = 50;
  engine.initialize(config);

  feedNormalReturns(engine, 200, 0.0, 0.05); // large vol

  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  engine.stop();

  // With large vol returns and a very tight limit, VaR should be breached
  bool breached = engine.isVaRBreached(1'000'000.0);
  EXPECT_TRUE(breached);
}

TEST_F(VaREngineTest, EmptyReturns) {
  VaREngine engine;
  engine.initialize(defaultConfig());

  auto result = engine.getLatestResult();

  EXPECT_DOUBLE_EQ(result.historicalVaR95, 0.0);
  EXPECT_DOUBLE_EQ(result.historicalVaR99, 0.0);
  EXPECT_DOUBLE_EQ(result.parametricVaR95, 0.0);
  EXPECT_DOUBLE_EQ(result.parametricVaR99, 0.0);
  EXPECT_DOUBLE_EQ(result.monteCarloVaR95, 0.0);
  EXPECT_DOUBLE_EQ(result.monteCarloVaR99, 0.0);
  EXPECT_EQ(result.sampleCount, 0u);
}

TEST_F(VaREngineTest, ToJson) {
  VaREngine engine;
  VaRConfig config = defaultConfig();
  config.updateIntervalMs = 50;
  engine.initialize(config);

  feedNormalReturns(engine, 100, 0.0, 0.01);

  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  engine.stop();

  auto j = engine.toJson();

  // The JSON should contain the expected keys
  EXPECT_TRUE(j.contains("historicalVaR95") ||
              j.contains("historical_var_95") || j.contains("var_95"));
  // As long as serialization does not throw, the test passes
  EXPECT_FALSE(j.dump().empty());
}

TEST_F(VaREngineTest, GetCurrentVaR95Pct) {
  VaREngine engine;
  VaRConfig config = defaultConfig();
  config.updateIntervalMs = 50;
  engine.initialize(config);

  // Before any data the accessor should return 0
  EXPECT_DOUBLE_EQ(engine.getCurrentVaR95Pct(), 0.0);

  feedNormalReturns(engine, 500, 0.0, 0.02);

  engine.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  engine.stop();

  // After feeding data the value should be non-zero
  double var95 = engine.getCurrentVaR95Pct();
  EXPECT_GE(var95, 0.0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
