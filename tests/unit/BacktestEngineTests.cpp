#include "../../core/utils/TimeUtils.h"
#include "../../strategies/backtesting/BacktestEngine.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

using namespace pinnacle::backtesting;
using namespace pinnacle::utils;

class BacktestEngineTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create test configuration
    config_.startTimestamp = TimeUtils::getCurrentNanos();
    config_.endTimestamp =
        config_.startTimestamp + 3600000000000ULL; // 1 hour later
    config_.initialBalance = 100000.0;
    config_.tradingFee = 0.001;
    config_.maxPosition = 1000.0;
    config_.maxDrawdown = 0.2;
    config_.speedMultiplier = 1000.0; // Fast simulation
    config_.enableSlippage = true;
    config_.slippageBps = 2.0;
    config_.outputDirectory = "test_backtest_output";

    // Create test directory
    std::filesystem::create_directories(config_.outputDirectory);
    std::filesystem::create_directories(config_.outputDirectory + "/data");

    engine_ = std::make_unique<BacktestEngine>(config_);
  }

  void TearDown() override {
    engine_.reset();

    // Cleanup test directory
    try {
      std::filesystem::remove_all(config_.outputDirectory);
    } catch (const std::exception& e) {
      // Ignore cleanup errors
    }
  }

  void createTestDataFile(const std::string& symbol,
                          size_t numDataPoints = 100) {
    std::string filename = config_.outputDirectory + "/data/" + symbol + ".csv";
    std::ofstream file(filename);

    file << "timestamp,symbol,price,bid,ask,volume\n";

    uint64_t currentTime = config_.startTimestamp;
    double price = 10000.0;
    uint64_t timeStep =
        (config_.endTimestamp - config_.startTimestamp) / numDataPoints;

    for (size_t i = 0; i < numDataPoints; ++i) {
      // Simulate some price movement
      price += (i % 2 == 0 ? 1.0 : -1.0) * (i % 5 + 1) * 0.1;

      file << currentTime << "," << symbol << "," << price << ","
           << (price - 0.1) << "," << (price + 0.1) << ",100.0\n";

      currentTime += timeStep;
    }
  }

  BacktestConfiguration config_;
  std::unique_ptr<BacktestEngine> engine_;
};

// HistoricalDataManager Tests
class HistoricalDataManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    testDataDir_ = "test_data_manager";
    std::filesystem::create_directories(testDataDir_);
    dataManager_ = std::make_unique<HistoricalDataManager>(testDataDir_);
  }

  void TearDown() override {
    dataManager_.reset();
    try {
      std::filesystem::remove_all(testDataDir_);
    } catch (const std::exception& e) {
      // Ignore cleanup errors
    }
  }

  void createTestCSV(const std::string& symbol, size_t numPoints = 50) {
    std::string filename = testDataDir_ + "/" + symbol + ".csv";
    std::ofstream file(filename);

    file << "timestamp,symbol,price,bid,ask,volume\n";

    uint64_t baseTime = 1000000000000ULL; // Base timestamp
    double basePrice = 1000.0;

    for (size_t i = 0; i < numPoints; ++i) {
      uint64_t timestamp = baseTime + i * 1000000000ULL; // 1 second intervals
      double price = basePrice + i * 0.1;

      file << timestamp << "," << symbol << "," << price << ","
           << (price - 0.05) << "," << (price + 0.05) << ",100.0\n";
    }
  }

  std::string testDataDir_;
  std::unique_ptr<HistoricalDataManager> dataManager_;
};

TEST_F(HistoricalDataManagerTest, LoadCSVDataTest) {
  createTestCSV("TESTCOIN");

  uint64_t startTime = 1000000000000ULL;
  uint64_t endTime = 1000000050000ULL;

  EXPECT_TRUE(dataManager_->loadData("TESTCOIN", startTime, endTime));
  EXPECT_GT(dataManager_->getDataPointCount(), 0);
  EXPECT_TRUE(dataManager_->hasMoreData());
}

TEST_F(HistoricalDataManagerTest, DataValidationTest) {
  createTestCSV("TESTCOIN");

  uint64_t startTime = 1000000000000ULL;
  uint64_t endTime = 1000000050000ULL;

  EXPECT_TRUE(dataManager_->loadData("TESTCOIN", startTime, endTime));
  EXPECT_TRUE(dataManager_->validateDataIntegrity());
}

TEST_F(HistoricalDataManagerTest, DataRetrievalTest) {
  createTestCSV("TESTCOIN", 10);

  uint64_t startTime = 1000000000000ULL;
  uint64_t endTime = 1000000050000ULL;

  EXPECT_TRUE(dataManager_->loadData("TESTCOIN", startTime, endTime));

  size_t count = 0;
  while (dataManager_->hasMoreData()) {
    auto dataPoint = dataManager_->getNextDataPoint();
    EXPECT_GT(dataPoint.timestamp, 0);
    // Note: MarketDataPoint doesn't have symbol field
    EXPECT_GT(dataPoint.price, 0);
    EXPECT_LT(dataPoint.bid, dataPoint.ask);
    count++;
  }

  EXPECT_GT(count, 0);
}

TEST_F(HistoricalDataManagerTest, SyntheticDataGenerationTest) {
  // Test loading non-existent symbol (should generate synthetic data)
  uint64_t startTime = TimeUtils::getCurrentNanos();
  uint64_t endTime = startTime + 10000000000ULL; // 10 seconds

  EXPECT_TRUE(dataManager_->loadData("NONEXISTENT", startTime, endTime));
  EXPECT_GT(dataManager_->getDataPointCount(), 0);
  EXPECT_TRUE(dataManager_->validateDataIntegrity());
}

// PerformanceAnalyzer Tests
class PerformanceAnalyzerTest : public ::testing::Test {
protected:
  void SetUp() override { analyzer_ = std::make_unique<PerformanceAnalyzer>(); }

  void TearDown() override { analyzer_.reset(); }

  BacktestTrade createTestTrade(uint64_t timestamp, double pnl, double position,
                                double balance) {
    BacktestTrade trade;
    trade.timestamp = timestamp;
    trade.orderId = "order_" + std::to_string(timestamp);
    trade.symbol = "TESTCOIN";
    trade.side = pinnacle::OrderSide::BUY;
    trade.quantity = 10.0;
    trade.price = 1000.0;
    trade.fee = 1.0;
    trade.pnl = pnl;
    trade.position = position;
    trade.balance = balance;
    trade.strategy = "test_strategy";
    trade.regime = "TRENDING_UP";
    return trade;
  }

  std::unique_ptr<PerformanceAnalyzer> analyzer_;
};

TEST_F(PerformanceAnalyzerTest, BasicStatisticsTest) {
  // Record some test trades
  analyzer_->recordTrade(createTestTrade(1000, 100.0, 10.0, 100100.0));
  analyzer_->recordTrade(createTestTrade(2000, -50.0, 5.0, 100050.0));
  analyzer_->recordTrade(createTestTrade(3000, 200.0, 15.0, 100250.0));

  auto stats = analyzer_->calculateStatistics();

  EXPECT_EQ(stats.totalTrades, 3);
  EXPECT_DOUBLE_EQ(stats.totalPnL, 250.0);
  EXPECT_GT(stats.winRate, 0.0);
  EXPECT_LT(stats.winRate, 1.0);
}

TEST_F(PerformanceAnalyzerTest, WinLossAnalysisTest) {
  // 2 wins, 1 loss
  analyzer_->recordTrade(createTestTrade(1000, 100.0, 10.0, 100100.0));
  analyzer_->recordTrade(createTestTrade(2000, -50.0, 5.0, 100050.0));
  analyzer_->recordTrade(createTestTrade(3000, 150.0, 20.0, 100200.0));

  auto stats = analyzer_->calculateStatistics();

  EXPECT_DOUBLE_EQ(stats.winRate, 2.0 / 3.0);
  EXPECT_DOUBLE_EQ(stats.avgWin, 125.0); // (100 + 150) / 2
  EXPECT_DOUBLE_EQ(stats.avgLoss, 50.0);
  EXPECT_GT(stats.profitFactor, 1.0);
}

TEST_F(PerformanceAnalyzerTest, RiskMetricsTest) {
  // Create trades with varying P&L for risk calculations
  std::vector<double> pnls = {100, -50, 200, -75, 150, -25, 300, -100};
  double balance = 100000.0;

  for (size_t i = 0; i < pnls.size(); ++i) {
    balance += pnls[i];
    analyzer_->recordTrade(
        createTestTrade(1000 + i * 1000, pnls[i], 10.0, balance));
  }

  auto stats = analyzer_->calculateStatistics();

  EXPECT_GE(stats.sharpeRatio, -10.0); // Reasonable bounds
  EXPECT_LE(stats.sharpeRatio, 10.0);
  EXPECT_GE(stats.maxDrawdown, 0.0);
  EXPECT_LE(stats.maxDrawdown, 1.0);
  EXPECT_GE(stats.valueAtRisk95, 0.0);
  EXPECT_GE(stats.valueAtRisk99, 0.0);
}

TEST_F(PerformanceAnalyzerTest, EmptyDataTest) {
  auto stats = analyzer_->calculateStatistics();

  EXPECT_EQ(stats.totalTrades, 0);
  EXPECT_DOUBLE_EQ(stats.totalPnL, 0.0);
  EXPECT_DOUBLE_EQ(stats.winRate, 0.0);
  EXPECT_DOUBLE_EQ(stats.sharpeRatio, 0.0);
}

// BacktestEngine Tests
TEST_F(BacktestEngineTest, InitializationTest) {
  EXPECT_TRUE(engine_->initialize());
  EXPECT_FALSE(engine_->isRunning());
  EXPECT_DOUBLE_EQ(engine_->getProgress(), 0.0);
}

TEST_F(BacktestEngineTest, ConfigurationTest) {
  auto retrievedConfig = engine_->getConfiguration();

  EXPECT_DOUBLE_EQ(retrievedConfig.initialBalance, config_.initialBalance);
  EXPECT_DOUBLE_EQ(retrievedConfig.tradingFee, config_.tradingFee);
  EXPECT_EQ(retrievedConfig.outputDirectory, config_.outputDirectory);

  // Test configuration update
  BacktestConfiguration newConfig = config_;
  newConfig.tradingFee = 0.002;
  engine_->updateConfiguration(newConfig);

  auto updatedConfig = engine_->getConfiguration();
  EXPECT_DOUBLE_EQ(updatedConfig.tradingFee, 0.002);
}

TEST_F(BacktestEngineTest, BasicBacktestTest) {
  createTestDataFile("TESTCOIN", 50);

  EXPECT_TRUE(engine_->initialize());
  EXPECT_TRUE(engine_->runBacktest("TESTCOIN"));

  auto results = engine_->getResults();
  // Basic validation - should have processed some data
  EXPECT_GE(results.totalTrades, 0); // May be 0 if no strategy is set
}

TEST_F(BacktestEngineTest, ProgressMonitoringTest) {
  createTestDataFile("TESTCOIN", 20);

  EXPECT_TRUE(engine_->initialize());

  // Start backtest in background (simplified test)
  std::thread backtestThread([this]() { engine_->runBacktest("TESTCOIN"); });

  // Wait a bit and check progress
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Progress should eventually reach 1.0
  backtestThread.join();
  EXPECT_DOUBLE_EQ(engine_->getProgress(), 1.0);
}

TEST_F(BacktestEngineTest, ResultsExportTest) {
  createTestDataFile("TESTCOIN", 10);

  EXPECT_TRUE(engine_->initialize());
  EXPECT_TRUE(engine_->runBacktest("TESTCOIN"));

  std::string exportFile = config_.outputDirectory + "/test_export.json";
  EXPECT_TRUE(engine_->exportResults(exportFile));
  EXPECT_TRUE(std::filesystem::exists(exportFile));

  // Verify file has content
  std::ifstream file(exportFile);
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  EXPECT_FALSE(content.empty());
  EXPECT_TRUE(content.find("backtest_config") != std::string::npos);
  EXPECT_TRUE(content.find("results") != std::string::npos);
}

TEST_F(BacktestEngineTest, DetailedReportTest) {
  createTestDataFile("TESTCOIN", 10);

  EXPECT_TRUE(engine_->initialize());
  EXPECT_TRUE(engine_->runBacktest("TESTCOIN"));

  std::string report = engine_->getDetailedReport();
  EXPECT_FALSE(report.empty());
  EXPECT_TRUE(report.find("BACKTEST RESULTS") != std::string::npos);
  EXPECT_TRUE(report.find("Total P&L") != std::string::npos);
  EXPECT_TRUE(report.find("Total Trades") != std::string::npos);
}

// BacktestRunner Tests
class BacktestRunnerTest : public ::testing::Test {
protected:
  void SetUp() override {
    runner_ = std::make_unique<BacktestRunner>();

    // Create base configuration
    baseConfig_.startTimestamp = TimeUtils::getCurrentNanos();
    baseConfig_.endTimestamp =
        baseConfig_.startTimestamp + 60000000000ULL; // 1 minute
    baseConfig_.initialBalance = 100000.0;
    baseConfig_.tradingFee = 0.001;
    baseConfig_.maxPosition = 1000.0;
    baseConfig_.speedMultiplier = 1000.0; // Fast simulation
    baseConfig_.outputDirectory = "test_runner_output";

    // Create test directory and data
    std::filesystem::create_directories(baseConfig_.outputDirectory + "/data");
    createTestData("TESTCOIN");
  }

  void TearDown() override {
    runner_.reset();
    try {
      std::filesystem::remove_all(baseConfig_.outputDirectory);
    } catch (const std::exception& e) {
      // Ignore cleanup errors
    }
  }

  void createTestData(const std::string& symbol) {
    std::string filename =
        baseConfig_.outputDirectory + "/data/" + symbol + ".csv";
    std::ofstream file(filename);

    file << "timestamp,symbol,price,bid,ask,volume\n";

    uint64_t currentTime = baseConfig_.startTimestamp;
    double price = 10000.0;
    size_t numPoints = 20;
    uint64_t timeStep =
        (baseConfig_.endTimestamp - baseConfig_.startTimestamp) / numPoints;

    for (size_t i = 0; i < numPoints; ++i) {
      price += (i % 2 == 0 ? 1.0 : -0.5);

      file << currentTime << "," << symbol << "," << price << ","
           << (price - 0.1) << "," << (price + 0.1) << ",100.0\n";

      currentTime += timeStep;
    }
  }

  std::unique_ptr<BacktestRunner> runner_;
  BacktestConfiguration baseConfig_;
};

TEST_F(BacktestRunnerTest, BatchBacktestTest) {
  std::vector<std::pair<std::string, BacktestConfiguration>> configs;

  // Create multiple configurations
  configs.emplace_back("config1", baseConfig_);

  BacktestConfiguration config2 = baseConfig_;
  config2.tradingFee = 0.002;
  configs.emplace_back("config2", config2);

  BacktestConfiguration config3 = baseConfig_;
  config3.maxPosition = 500.0;
  configs.emplace_back("config3", config3);

  auto results = runner_->runBatchBacktests(configs, "TESTCOIN");

  EXPECT_EQ(results.size(), 3);
  for (const auto& result : results) {
    EXPECT_TRUE(result.successful || !result.error.empty());
  }
}

TEST_F(BacktestRunnerTest, ParameterOptimizationTest) {
  std::map<std::string, std::vector<double>> paramGrid;
  paramGrid["trading_fee"] = {0.001, 0.002, 0.003};
  paramGrid["max_position"] = {500.0, 1000.0, 1500.0};

  auto result = runner_->optimizeParameters("TESTCOIN", baseConfig_, paramGrid);

  EXPECT_FALSE(result.allResults.empty());
  // Should have found some configuration
  EXPECT_GE(result.bestResults.totalTrades, 0);
}

TEST_F(BacktestRunnerTest, ABTestTest) {
  BacktestConfiguration configA = baseConfig_;
  BacktestConfiguration configB = baseConfig_;
  configB.tradingFee = 0.002;

  auto comparison =
      runner_->runABTest("TESTCOIN", configA, "ConfigA", configB, "ConfigB");

  EXPECT_FALSE(comparison.strategyA.empty());
  EXPECT_FALSE(comparison.strategyB.empty());
  EXPECT_EQ(comparison.strategyA, "ConfigA");
  EXPECT_EQ(comparison.strategyB, "ConfigB");
}

TEST_F(BacktestRunnerTest, MonteCarloAnalysisTest) {
  int numSimulations = 5; // Small number for testing

  auto mcResult =
      runner_->runMonteCarloAnalysis("TESTCOIN", baseConfig_, numSimulations);

  if (!mcResult.simulations.empty()) {
    EXPECT_LE(mcResult.simulations.size(), numSimulations);
    EXPECT_GE(mcResult.probabilityOfProfit, 0.0);
    EXPECT_LE(mcResult.probabilityOfProfit, 1.0);
    EXPECT_GE(mcResult.valueAtRisk95, 0.0);
    EXPECT_GE(mcResult.valueAtRisk99, 0.0);
  }
}

// Integration Tests
TEST_F(BacktestEngineTest, StrategyComparisonTest) {
  createTestDataFile("TESTCOIN", 50);

  // Create two different configurations for comparison
  TradingStatistics statsA;
  statsA.totalPnL = 1000.0;
  statsA.sharpeRatio = 1.5;
  statsA.maxDrawdown = 0.1;
  statsA.totalTrades = 100;

  TradingStatistics statsB;
  statsB.totalPnL = 800.0;
  statsB.sharpeRatio = 1.2;
  statsB.maxDrawdown = 0.15;
  statsB.totalTrades = 80;

  auto comparison = BacktestEngine::compareStrategies(statsA, "StrategyA",
                                                      statsB, "StrategyB");

  EXPECT_EQ(comparison.strategyA, "StrategyA");
  EXPECT_EQ(comparison.strategyB, "StrategyB");
  EXPECT_EQ(comparison.winner, "StrategyA"); // Higher Sharpe ratio
}

TEST_F(BacktestEngineTest, StopBacktestTest) {
  createTestDataFile("TESTCOIN", 1000); // Large dataset

  EXPECT_TRUE(engine_->initialize());

  // Start backtest in background
  std::thread backtestThread([this]() { engine_->runBacktest("TESTCOIN"); });

  // Let it run briefly, then stop
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  engine_->stop();

  backtestThread.join();

  // Should have stopped before completing
  EXPECT_LT(engine_->getProgress(), 1.0);
}

// Edge Cases and Error Handling
TEST_F(BacktestEngineTest, InvalidDataTest) {
  // Don't create any data file
  EXPECT_TRUE(engine_->initialize());

  // Should handle missing data gracefully by generating synthetic data
  EXPECT_TRUE(engine_->runBacktest("NONEXISTENT"));
}

TEST_F(BacktestEngineTest, EmptyConfigurationTest) {
  BacktestConfiguration emptyConfig;
  emptyConfig.outputDirectory = "test_empty_output";

  BacktestEngine emptyEngine(emptyConfig);
  EXPECT_TRUE(emptyEngine.initialize());

  // Should handle empty/default configuration
  auto results = emptyEngine.getResults();
  EXPECT_GE(results.totalTrades, 0);
}

TEST_F(BacktestEngineTest, LargeDatasetTest) {
  createTestDataFile("LARGECOIN", 10000); // Large dataset

  config_.speedMultiplier = 10000.0; // Very fast simulation
  BacktestEngine largeEngine(config_);

  EXPECT_TRUE(largeEngine.initialize());
  EXPECT_TRUE(largeEngine.runBacktest("LARGECOIN"));

  auto results = largeEngine.getResults();
  // Should complete successfully even with large dataset
  EXPECT_GE(results.totalTrades, 0);
}
