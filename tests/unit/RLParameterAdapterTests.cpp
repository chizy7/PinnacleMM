#include "../../core/utils/TimeUtils.h"
#include "../../strategies/rl/RLParameterAdapter.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace pinnacle;
using namespace pinnacle::rl;
using namespace pinnacle::utils;

class RLParameterAdapterTest : public ::testing::Test {
protected:
  void SetUp() override {
    symbol_ = "BTC-USD";

    // Create test configuration
    config_.adaptationIntervalMs = 100;
    config_.episodeIntervalMs = 1000;
    config_.minActionsPerEpisode = 2;
    config_.pnlRewardWeight = 0.4;
    config_.fillRateRewardWeight = 0.3;
    config_.riskRewardWeight = 0.2;
    config_.stabilityRewardWeight = 0.1;
    config_.maxParameterChangePerEpisode = 0.1;

    // Configure Q-learning
    config_.qLearningConfig.learningRate = 0.1;
    config_.qLearningConfig.discountFactor = 0.95;
    config_.qLearningConfig.maxQTableSize = 1000;
    config_.qLearningConfig.stateDiscretization = 5;

    // Configure bandit
    config_.banditConfig.ucbConstant = 1.4;
    config_.banditConfig.decayFactor = 0.99;
    config_.banditConfig.warmupPeriod = 10;

    adapter_ = std::make_unique<RLParameterAdapter>(symbol_, config_);
  }

  void TearDown() override {
    if (adapter_ && adapter_->isRunning()) {
      adapter_->stop();
    }
    adapter_.reset();
  }

  MarketState createTestMarketState() {
    MarketState state;
    state.volatility = 0.02;
    state.spread = 0.001;
    state.volume = 100.0;
    state.imbalance = 0.1;
    state.momentum = 0.05;
    state.liquidity = 50.0;
    state.timeOfDay = 0.5;
    state.dayOfWeek = 0.3;
    state.currentPosition = 0.0;
    state.unrealizedPnL = 10.0;
    return state;
  }

  std::string symbol_;
  RLParameterAdapter::Config config_;
  std::unique_ptr<RLParameterAdapter> adapter_;
};

TEST_F(RLParameterAdapterTest, InitializationTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_FALSE(adapter_->isRunning());
}

TEST_F(RLParameterAdapterTest, StartStopTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());
  EXPECT_TRUE(adapter_->isRunning());

  EXPECT_TRUE(adapter_->stop());
  EXPECT_FALSE(adapter_->isRunning());
}

TEST_F(RLParameterAdapterTest, MarketStateUpdateTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  MarketState state = createTestMarketState();
  adapter_->updateMarketState(state);

  // Should not throw any exceptions
  EXPECT_TRUE(adapter_->isRunning());
}

TEST_F(RLParameterAdapterTest, PerformanceRecordingTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  uint64_t timestamp = TimeUtils::getCurrentNanos();
  adapter_->recordPerformance(100.0, 0.8, 0.2, timestamp);

  // Should not throw any exceptions
  EXPECT_TRUE(adapter_->isRunning());
}

TEST_F(RLParameterAdapterTest, ParameterValueSetGetTest) {
  EXPECT_TRUE(adapter_->initialize());

  // Test setting and getting parameter values
  adapter_->setParameterValue(ParameterType::BASE_SPREAD_BPS, 10.0);
  EXPECT_DOUBLE_EQ(10.0,
                   adapter_->getParameterValue(ParameterType::BASE_SPREAD_BPS));

  adapter_->setParameterValue(ParameterType::ORDER_QUANTITY, 1.5);
  EXPECT_DOUBLE_EQ(1.5,
                   adapter_->getParameterValue(ParameterType::ORDER_QUANTITY));
}

TEST_F(RLParameterAdapterTest, ParameterAdaptationEnableDisableTest) {
  EXPECT_TRUE(adapter_->initialize());

  // Test enabling and disabling parameter adaptation
  adapter_->enableParameterAdaptation(ParameterType::BASE_SPREAD_BPS, true);
  EXPECT_TRUE(
      adapter_->isParameterAdaptationEnabled(ParameterType::BASE_SPREAD_BPS));

  adapter_->enableParameterAdaptation(ParameterType::BASE_SPREAD_BPS, false);
  EXPECT_FALSE(
      adapter_->isParameterAdaptationEnabled(ParameterType::BASE_SPREAD_BPS));
}

TEST_F(RLParameterAdapterTest, StrategySelectionTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  std::vector<std::string> strategies = {"aggressive", "conservative",
                                         "balanced"};
  std::string selected = adapter_->selectStrategy(strategies);

  EXPECT_FALSE(selected.empty());
  EXPECT_TRUE(std::find(strategies.begin(), strategies.end(), selected) !=
              strategies.end());
}

TEST_F(RLParameterAdapterTest, StrategyPerformanceTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  std::vector<std::string> strategies = {"strategy1", "strategy2"};
  adapter_->selectStrategy(strategies);

  // Record performance for a strategy
  adapter_->recordStrategyPerformance("strategy1", 0.5);

  // Should not throw any exceptions
  EXPECT_TRUE(adapter_->isRunning());
}

TEST_F(RLParameterAdapterTest, ParameterAdaptationTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Create a strategy config
  pinnacle::strategy::StrategyConfig config;
  config.baseSpreadBps = 5;
  config.orderQuantity = 1.0;
  config.maxPosition = 10.0;
  config.inventorySkewFactor = Factor(1.0);

  // Update market state and record performance to trigger adaptation
  MarketState state = createTestMarketState();
  adapter_->updateMarketState(state);

  uint64_t timestamp = TimeUtils::getCurrentNanos();
  adapter_->recordPerformance(50.0, 0.7, 0.3, timestamp);

  // Trigger parameter adaptation
  adapter_->adaptParameters(config);

  // Configuration should be potentially modified (within constraints)
  EXPECT_GE(config.baseSpreadBps, 1);
  EXPECT_LE(config.baseSpreadBps, 50);
}

TEST_F(RLParameterAdapterTest, EpisodeManagementTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  uint32_t initialEpisode = adapter_->getCurrentEpisode();

  // Force episode end
  adapter_->forceEpisodeEnd();

  uint32_t newEpisode = adapter_->getCurrentEpisode();
  EXPECT_GT(newEpisode, initialEpisode);
}

TEST_F(RLParameterAdapterTest, StatisticsTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Generate some activity
  MarketState state = createTestMarketState();
  adapter_->updateMarketState(state);

  uint64_t timestamp = TimeUtils::getCurrentNanos();
  adapter_->recordPerformance(25.0, 0.6, 0.4, timestamp);

  // Get statistics
  std::string perfStats = adapter_->getPerformanceStatistics();
  std::string qStats = adapter_->getQLearningStatistics();
  std::string banditStats = adapter_->getBanditStatistics();

  EXPECT_FALSE(perfStats.empty());
  EXPECT_FALSE(qStats.empty());
  EXPECT_FALSE(banditStats.empty());
}

TEST_F(RLParameterAdapterTest, ModelPersistenceTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Generate some learning data
  MarketState state = createTestMarketState();
  adapter_->updateMarketState(state);

  uint64_t timestamp = TimeUtils::getCurrentNanos();
  adapter_->recordPerformance(75.0, 0.9, 0.1, timestamp);

  // Test saving
  std::string filename = "/tmp/test_rl_model";
  EXPECT_TRUE(adapter_->saveModel(filename));

  // Create new adapter and load
  auto newAdapter = std::make_unique<RLParameterAdapter>(symbol_, config_);
  EXPECT_TRUE(newAdapter->initialize());
  EXPECT_TRUE(newAdapter->loadModel(filename));

  // Episode count should be preserved
  EXPECT_EQ(adapter_->getCurrentEpisode(), newAdapter->getCurrentEpisode());
}

TEST_F(RLParameterAdapterTest, ResetTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Generate some activity
  MarketState state = createTestMarketState();
  adapter_->updateMarketState(state);

  uint64_t timestamp = TimeUtils::getCurrentNanos();
  adapter_->recordPerformance(30.0, 0.5, 0.5, timestamp);

  adapter_->forceEpisodeEnd();
  uint32_t episodeBeforeReset = adapter_->getCurrentEpisode();
  uint64_t actionsBeforeReset = adapter_->getActionCount();

  // Reset
  adapter_->reset();

  // Counters should be reset
  EXPECT_LT(adapter_->getCurrentEpisode(), episodeBeforeReset);
  EXPECT_LT(adapter_->getActionCount(), actionsBeforeReset);
}

TEST_F(RLParameterAdapterTest, ConfigUpdateTest) {
  EXPECT_TRUE(adapter_->initialize());

  // Adapter should not be running for config update
  EXPECT_FALSE(adapter_->isRunning());

  // Update configuration
  RLParameterAdapter::Config newConfig = config_;
  newConfig.adaptationIntervalMs = 200;
  newConfig.episodeIntervalMs = 2000;

  EXPECT_TRUE(adapter_->updateConfig(newConfig));
  EXPECT_EQ(200ULL, adapter_->getCurrentConfig().adaptationIntervalMs);
  EXPECT_EQ(2000ULL, adapter_->getCurrentConfig().episodeIntervalMs);
}

TEST_F(RLParameterAdapterTest, ConcurrentOperationsTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  std::atomic<bool> stopFlag{false};
  std::vector<std::thread> threads;

  // Thread 1: Market state updates
  threads.emplace_back([&]() {
    while (!stopFlag.load()) {
      MarketState state = createTestMarketState();
      state.volatility = 0.01 + (rand() % 100) / 10000.0; // Add some variation
      adapter_->updateMarketState(state);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  // Thread 2: Performance recording
  threads.emplace_back([&]() {
    while (!stopFlag.load()) {
      uint64_t timestamp = TimeUtils::getCurrentNanos();
      double pnl = (rand() % 200) - 100; // Random P&L
      double fillRate = (rand() % 100) / 100.0;
      double risk = (rand() % 50) / 100.0;
      adapter_->recordPerformance(pnl, fillRate, risk, timestamp);
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  });

  // Thread 3: Strategy selection
  threads.emplace_back([&]() {
    std::vector<std::string> strategies = {"test1", "test2", "test3"};
    while (!stopFlag.load()) {
      std::string selected = adapter_->selectStrategy(strategies);
      adapter_->recordStrategyPerformance(selected, (rand() % 100) / 100.0);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });

  // Let threads run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  stopFlag.store(true);

  // Join all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // System should still be operational
  EXPECT_TRUE(adapter_->isRunning());

  // Get final statistics
  std::string stats = adapter_->getPerformanceStatistics();
  EXPECT_FALSE(stats.empty());
}

// Test parameter constraint enforcement
TEST_F(RLParameterAdapterTest, ParameterConstraintsTest) {
  EXPECT_TRUE(adapter_->initialize());

  // Test that parameter values are clamped to constraints
  adapter_->setParameterValue(ParameterType::BASE_SPREAD_BPS,
                              -10.0); // Below minimum
  double value = adapter_->getParameterValue(ParameterType::BASE_SPREAD_BPS);
  EXPECT_GE(value, 1.0); // Should be clamped to minimum

  adapter_->setParameterValue(ParameterType::BASE_SPREAD_BPS,
                              1000.0); // Above maximum
  value = adapter_->getParameterValue(ParameterType::BASE_SPREAD_BPS);
  EXPECT_LE(value, 50.0); // Should be clamped to maximum
}

// Test edge cases
TEST_F(RLParameterAdapterTest, EdgeCasesTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Test with empty strategy list
  std::vector<std::string> emptyStrategies;
  std::string selected = adapter_->selectStrategy(emptyStrategies);
  EXPECT_TRUE(selected.empty());

  // Test with single strategy
  std::vector<std::string> singleStrategy = {"only_one"};
  selected = adapter_->selectStrategy(singleStrategy);
  EXPECT_EQ("only_one", selected);

  // Test parameter adaptation with no previous data
  pinnacle::strategy::StrategyConfig config;
  config.baseSpreadBps = 5;
  config.orderQuantity = 1.0;
  config.maxPosition = 10.0;
  config.inventorySkewFactor = Factor(1.0);

  // Should not crash with no previous market state
  adapter_->adaptParameters(config);
}

// Helper functions for parameter type conversions
TEST_F(RLParameterAdapterTest, HelperFunctionsTest) {
  // Test parameter type to string conversion
  EXPECT_EQ("BASE_SPREAD_BPS",
            parameterTypeToString(ParameterType::BASE_SPREAD_BPS));
  EXPECT_EQ("ORDER_QUANTITY",
            parameterTypeToString(ParameterType::ORDER_QUANTITY));
  EXPECT_EQ("MAX_POSITION", parameterTypeToString(ParameterType::MAX_POSITION));

  // Test action type to string conversion
  EXPECT_EQ("INCREASE_SPREAD", actionTypeToString(ActionType::INCREASE_SPREAD));
  EXPECT_EQ("DECREASE_SPREAD", actionTypeToString(ActionType::DECREASE_SPREAD));
  EXPECT_EQ("MAINTAIN_CURRENT",
            actionTypeToString(ActionType::MAINTAIN_CURRENT));

  // Test default parameter constraints creation
  auto constraints = createDefaultParameterConstraints();
  EXPECT_FALSE(constraints.empty());
  EXPECT_GT(constraints.size(), 0);
}

// Test Q-learning specific functionality
TEST_F(RLParameterAdapterTest, QLearningFunctionalityTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  // Generate several episodes of learning data
  for (int episode = 0; episode < 5; ++episode) {
    for (int action = 0; action < 3; ++action) {
      MarketState state = createTestMarketState();
      state.volatility = 0.01 + episode * 0.005; // Vary state across episodes
      adapter_->updateMarketState(state);

      uint64_t timestamp = TimeUtils::getCurrentNanos();
      double reward = (episode % 2 == 0) ? 50.0 : -25.0; // Alternating rewards
      adapter_->recordPerformance(reward, 0.8, 0.2, timestamp);

      // Create a strategy config for adaptation
      pinnacle::strategy::StrategyConfig config;
      config.baseSpreadBps = 5;
      config.orderQuantity = 1.0;
      config.maxPosition = 10.0;
      config.inventorySkewFactor = Factor(1.0);

      adapter_->adaptParameters(config);

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    adapter_->forceEpisodeEnd();
  }

  // After learning, Q-learning statistics should show some activity
  std::string qStats = adapter_->getQLearningStatistics();
  EXPECT_TRUE(qStats.find("Q-Table Size") != std::string::npos);
  EXPECT_TRUE(qStats.find("Total Actions") != std::string::npos);
}

// Test multi-armed bandit functionality
TEST_F(RLParameterAdapterTest, MultiArmedBanditFunctionalityTest) {
  EXPECT_TRUE(adapter_->initialize());
  EXPECT_TRUE(adapter_->start());

  std::vector<std::string> strategies = {"strategy_a", "strategy_b",
                                         "strategy_c"};

  // Simulate strategy selection and reward feedback
  for (int round = 0; round < 20; ++round) {
    std::string selected = adapter_->selectStrategy(strategies);
    EXPECT_FALSE(selected.empty());

    // Give different rewards to different strategies
    double reward = 0.0;
    if (selected == "strategy_a") {
      reward = 0.8; // High reward
    } else if (selected == "strategy_b") {
      reward = 0.5; // Medium reward
    } else {
      reward = 0.2; // Low reward
    }

    adapter_->recordStrategyPerformance(selected, reward);
  }

  // After multiple rounds, the bandit should prefer strategy_a
  std::string banditStats = adapter_->getBanditStatistics();
  EXPECT_TRUE(banditStats.find("Total Strategies") != std::string::npos);
  EXPECT_TRUE(banditStats.find("Total Selections") != std::string::npos);
}
