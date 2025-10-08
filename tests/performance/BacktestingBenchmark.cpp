#include "../../core/utils/TimeUtils.h"
#include "../../strategies/backtesting/BacktestEngine.h"

#include <benchmark/benchmark.h>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace pinnacle::backtesting;
using namespace pinnacle::utils;

// Helper function to create test data
void createBenchmarkData(const std::string& directory,
                         const std::string& symbol, size_t numDataPoints) {
  std::filesystem::create_directories(directory);

  std::string filename = directory + "/" + symbol + ".csv";
  std::ofstream file(filename);

  file << "timestamp,symbol,price,bid,ask,volume\n";

  uint64_t baseTime = TimeUtils::getCurrentNanos();
  double basePrice = 10000.0;
  uint64_t timeStep = 1000000000ULL; // 1 second

  for (size_t i = 0; i < numDataPoints; ++i) {
    uint64_t timestamp = baseTime + i * timeStep;

    // Simulate realistic price movement
    double priceChange = (i % 7 == 0) ? ((i % 2 == 0) ? 5.0 : -3.0)
                                      : ((i % 3 == 0) ? 1.0 : -0.5);
    basePrice += priceChange * (0.1 + (i % 10) * 0.01);

    double bid = basePrice - 0.1 - (i % 5) * 0.01;
    double ask = basePrice + 0.1 + (i % 5) * 0.01;
    double volume = 100.0 + (i % 20) * 10.0;

    file << timestamp << "," << symbol << "," << basePrice << "," << bid << ","
         << ask << "," << volume << "\n";
  }
}

// Benchmark: Basic backtest engine initialization
static void BM_BacktestEngineInitialization(benchmark::State& state) {
  BacktestConfiguration config;
  config.startTimestamp = TimeUtils::getCurrentNanos();
  config.endTimestamp = config.startTimestamp + 3600000000000ULL; // 1 hour
  config.initialBalance = 100000.0;
  config.outputDirectory = "benchmark_output_init";

  for (auto _ : state) {
    std::unique_ptr<BacktestEngine> engine =
        std::make_unique<BacktestEngine>(config);
    benchmark::DoNotOptimize(engine->initialize());
    engine.reset();
  }

  // Cleanup
  std::filesystem::remove_all(config.outputDirectory);
}
BENCHMARK(BM_BacktestEngineInitialization);

// Benchmark: Historical data loading performance
static void BM_HistoricalDataLoading(benchmark::State& state) {
  const size_t numDataPoints = state.range(0);
  std::string dataDir = "benchmark_data_loading";

  // Create test data once
  createBenchmarkData(dataDir, "BENCHMARK", numDataPoints);

  for (auto _ : state) {
    HistoricalDataManager dataManager(dataDir);
    uint64_t startTime = TimeUtils::getCurrentNanos();
    uint64_t endTime = startTime + 3600000000000ULL;

    state.PauseTiming();
    // Reset for each iteration
    state.ResumeTiming();

    benchmark::DoNotOptimize(
        dataManager.loadData("BENCHMARK", startTime, endTime));

    benchmark::DoNotOptimize(dataManager.validateDataIntegrity());
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * numDataPoints));

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_HistoricalDataLoading)
    ->Range(1000, 100000)
    ->Unit(benchmark::kMicrosecond);

// Benchmark: Data point processing speed
static void BM_DataPointProcessing(benchmark::State& state) {
  const size_t numDataPoints = state.range(0);
  std::string dataDir = "benchmark_data_processing";

  createBenchmarkData(dataDir, "PROCESSING", numDataPoints);

  HistoricalDataManager dataManager(dataDir);
  uint64_t startTime = TimeUtils::getCurrentNanos();
  uint64_t endTime = startTime + numDataPoints * 1000000000ULL;

  dataManager.loadData("PROCESSING", startTime, endTime);

  for (auto _ : state) {
    state.PauseTiming();
    // Reset data manager position (in a real implementation)
    HistoricalDataManager testManager(dataDir);
    testManager.loadData("PROCESSING", startTime, endTime);
    state.ResumeTiming();

    size_t processedCount = 0;
    while (testManager.hasMoreData()) {
      auto dataPoint = testManager.getNextDataPoint();
      benchmark::DoNotOptimize(dataPoint);
      processedCount++;
    }

    benchmark::DoNotOptimize(processedCount);
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * numDataPoints));

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_DataPointProcessing)
    ->Range(1000, 50000)
    ->Unit(benchmark::kMicrosecond);

// Benchmark: Performance analytics calculation
static void BM_PerformanceAnalytics(benchmark::State& state) {
  const size_t numTrades = state.range(0);

  PerformanceAnalyzer analyzer;

  // Pre-populate with trades
  for (size_t i = 0; i < numTrades; ++i) {
    BacktestTrade trade;
    trade.timestamp = TimeUtils::getCurrentNanos() + i * 1000000000ULL;
    trade.orderId = "trade_" + std::to_string(i);
    trade.symbol = "BENCHMARK";
    trade.side =
        (i % 2 == 0) ? pinnacle::OrderSide::BUY : pinnacle::OrderSide::SELL;
    trade.quantity = 10.0 + (i % 10);
    trade.price = 1000.0 + (i % 100) * 0.1;
    trade.fee = trade.quantity * trade.price * 0.001;

    // Simulate realistic P&L distribution
    double pnlFactor = (i % 7 == 0) ? -1.0 : 1.0;
    trade.pnl = pnlFactor * (1.0 + (i % 20)) * ((i % 5) + 1);

    trade.position = (i % 2 == 0) ? i * 0.1 : -(i * 0.1);
    trade.balance = 100000.0 + trade.pnl;
    trade.strategy = "benchmark_strategy";
    trade.regime = "TRENDING_UP";

    analyzer.recordTrade(trade);
  }

  for (auto _ : state) {
    auto statistics = analyzer.calculateStatistics();
    benchmark::DoNotOptimize(statistics);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * numTrades));
}
BENCHMARK(BM_PerformanceAnalytics)
    ->Range(100, 10000)
    ->Unit(benchmark::kMicrosecond);

// Benchmark: Risk metrics calculation
static void BM_RiskMetricsCalculation(benchmark::State& state) {
  const size_t numTrades = state.range(0);

  PerformanceAnalyzer analyzer;

  // Create trades with varying P&L for meaningful risk calculation
  std::vector<double> pnlPattern = {100, -50, 200, -75,  150, -25, 300, -100,
                                    80,  -30, 250, -120, 180, -45, 350, -90};

  for (size_t i = 0; i < numTrades; ++i) {
    BacktestTrade trade;
    trade.timestamp = TimeUtils::getCurrentNanos() + i * 1000000000ULL;
    trade.orderId = "risk_trade_" + std::to_string(i);
    trade.symbol = "RISK_BENCHMARK";
    trade.side =
        (i % 2 == 0) ? pinnacle::OrderSide::BUY : pinnacle::OrderSide::SELL;
    trade.quantity = 10.0;
    trade.price = 1000.0;
    trade.fee = 10.0;
    trade.pnl = pnlPattern[i % pnlPattern.size()] * (1.0 + (i % 10) * 0.1);
    trade.position = (i % 2 == 0) ? 10.0 : -10.0;
    trade.balance = 100000.0 + trade.pnl;
    trade.strategy = "risk_strategy";
    trade.regime = "VOLATILE";

    analyzer.recordTrade(trade);
  }

  for (auto _ : state) {
    // Test individual risk metric calculations
    state.PauseTiming();
    auto startTime = std::chrono::high_resolution_clock::now();
    state.ResumeTiming();

    double sharpe = analyzer.calculateSharpeRatio();
    double maxDD = analyzer.calculateMaxDrawdown();
    double var95 = analyzer.calculateValueAtRisk(0.95);
    double var99 = analyzer.calculateValueAtRisk(0.99);

    benchmark::DoNotOptimize(sharpe);
    benchmark::DoNotOptimize(maxDD);
    benchmark::DoNotOptimize(var95);
    benchmark::DoNotOptimize(var99);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * numTrades));
}
BENCHMARK(BM_RiskMetricsCalculation)
    ->Range(100, 5000)
    ->Unit(benchmark::kMicrosecond);

// Benchmark: Full backtest execution
static void BM_FullBacktestExecution(benchmark::State& state) {
  const size_t numDataPoints = state.range(0);
  std::string dataDir = "benchmark_full_backtest";

  BacktestConfiguration config;
  config.startTimestamp = TimeUtils::getCurrentNanos();
  config.endTimestamp = config.startTimestamp + numDataPoints * 1000000000ULL;
  config.initialBalance = 100000.0;
  config.tradingFee = 0.001;
  config.speedMultiplier = 1000000.0; // Very fast simulation
  config.outputDirectory = dataDir;

  // Create test data
  createBenchmarkData(dataDir + "/data", "FULLTEST", numDataPoints);

  for (auto _ : state) {
    BacktestEngine engine(config);

    state.PauseTiming();
    bool initialized = engine.initialize();
    state.ResumeTiming();

    if (initialized) {
      benchmark::DoNotOptimize(engine.runBacktest("FULLTEST"));
      auto results = engine.getResults();
      benchmark::DoNotOptimize(results);
    }
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * numDataPoints));

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_FullBacktestExecution)
    ->Range(1000, 10000)
    ->Unit(benchmark::kMillisecond);

// Benchmark: Batch backtest performance
static void BM_BatchBacktestExecution(benchmark::State& state) {
  const size_t numConfigs = state.range(0);
  std::string dataDir = "benchmark_batch_backtest";

  // Create test data
  createBenchmarkData(dataDir + "/data", "BATCHTEST", 1000);

  // Create multiple configurations
  std::vector<std::pair<std::string, BacktestConfiguration>> configs;
  for (size_t i = 0; i < numConfigs; ++i) {
    BacktestConfiguration config;
    config.startTimestamp = TimeUtils::getCurrentNanos();
    config.endTimestamp = config.startTimestamp + 1000 * 1000000000ULL;
    config.initialBalance = 100000.0;
    config.tradingFee = 0.001 + i * 0.0001; // Vary trading fee
    config.speedMultiplier = 100000.0;      // Fast simulation
    config.outputDirectory = dataDir;

    configs.emplace_back("config_" + std::to_string(i), config);
  }

  for (auto _ : state) {
    BacktestRunner runner;
    auto results = runner.runBatchBacktests(configs, "BATCHTEST");
    benchmark::DoNotOptimize(results);
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * numConfigs));

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_BatchBacktestExecution)
    ->Range(2, 10)
    ->Unit(benchmark::kMillisecond);

// Benchmark: Data export performance
static void BM_ResultsExport(benchmark::State& state) {
  std::string dataDir = "benchmark_export";
  std::filesystem::create_directories(dataDir);

  BacktestConfiguration config;
  config.outputDirectory = dataDir;
  config.startTimestamp = TimeUtils::getCurrentNanos();
  config.endTimestamp = config.startTimestamp + 1000000000ULL;

  BacktestEngine engine(config);
  engine.initialize();

  for (auto _ : state) {
    std::string exportFile =
        dataDir + "/export_" + std::to_string(state.iterations()) + ".json";
    benchmark::DoNotOptimize(engine.exportResults(exportFile));
  }

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_ResultsExport);

// Benchmark: Memory usage efficiency
static void BM_MemoryEfficiency(benchmark::State& state) {
  const size_t numDataPoints = state.range(0);
  std::string dataDir = "benchmark_memory";

  createBenchmarkData(dataDir, "MEMORY", numDataPoints);

  for (auto _ : state) {
    state.PauseTiming();
    // Measure memory before allocation
    auto beforeMemory = std::chrono::high_resolution_clock::now();
    state.ResumeTiming();

    HistoricalDataManager dataManager(dataDir);
    uint64_t startTime = TimeUtils::getCurrentNanos();
    uint64_t endTime = startTime + numDataPoints * 1000000000ULL;

    dataManager.loadData("MEMORY", startTime, endTime);

    // Process all data
    size_t processedCount = 0;
    while (dataManager.hasMoreData()) {
      auto dataPoint = dataManager.getNextDataPoint();
      benchmark::DoNotOptimize(dataPoint);
      processedCount++;
    }

    benchmark::DoNotOptimize(processedCount);
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * numDataPoints));

  // Cleanup
  std::filesystem::remove_all(dataDir);
}
BENCHMARK(BM_MemoryEfficiency)
    ->Range(1000, 50000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
